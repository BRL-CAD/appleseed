// This file comes from the original BCD implementation,
// with minor changes to remove dependencies, unused code
// and re-formatting. Original license follows:

// This file is part of the reference implementation for the paper
//   Bayesian Collaborative Denoising for Monte-Carlo Rendering
//   Malik Boughida and Tamy Boubekeur.
//   Computer Graphics Forum (Proc. EGSR 2017), vol. 36, no. 4, p. 137-153, 2017
//
// All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE.txt file.

// BCD headers.
#include "Denoiser.h"
#include "DenoisingUnit.h"
#include "Utils.h"

// Standard headers.
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <functional>
#include <random>
#include <thread>

using namespace std;

namespace bcd
{

bool Denoiser::denoise()
{
    if (!inputsOutputsAreOk())
        return false;
    if (!m_outputs.m_pDenoisedColors)
    {
        if (m_callbacks)
            m_callbacks->error() << "Aborting denoising: nullptr for output color image\n";
        return false;
    }

    m_width = m_inputs.m_pColors->getWidth();
    m_height = m_inputs.m_pColors->getHeight();
    if (m_parameters.m_patchRadius < 0 ||
        m_parameters.m_patchRadius > (m_width - 1) / 2 ||
        m_parameters.m_patchRadius > (m_height - 1) / 2 ||
        m_parameters.m_searchWindowRadius < 0 || m_parameters.m_nbOfCores < 0)
    {
        if (m_callbacks)
            m_callbacks->error() << "Aborting denoising: invalid patch radius, search radius, or worker count\n";
        return false;
    }

    m_nbOfPixels = m_width * m_height;
    int widthWithoutBorder = m_width - 2 * m_parameters.m_patchRadius;
    int heightWithoutBorder = m_height - 2 * m_parameters.m_patchRadius;
    int nbOfPixelsWithoutBorder = widthWithoutBorder * heightWithoutBorder;

    if (m_callbacks && m_callbacks->isAborted())
        return false;

    computeNbOfSamplesSqrt();
    computePixelCovFromSampleCov();

    if (m_parameters.m_nbOfCores == 0)
        m_parameters.m_nbOfCores = std::max(1u, thread::hardware_concurrency());
    m_parameters.m_nbOfCores = std::min(m_parameters.m_nbOfCores, nbOfPixelsWithoutBorder);

    vector<PixelPosition> pixelSet(nbOfPixelsWithoutBorder);
    {
        int k = 0;
        int yMin = m_parameters.m_patchRadius;
        int yMax = m_height - m_parameters.m_patchRadius - 1;
        int xMin = m_parameters.m_patchRadius;
        int xMax = m_width - m_parameters.m_patchRadius - 1;

        for (int y = yMin; y <= yMax; y++)
        {
            for (int x = xMin; x <= xMax; x++)
            {
                pixelSet[k++] = PixelPosition(y, x);
            }
        }
    }
    reorderPixelSet(pixelSet);

    if (m_callbacks && m_callbacks->isAborted())
        return false;

    m_outputSummedColorImages.resize(m_parameters.m_nbOfCores);
    m_outputSummedColorImages[0].resize(m_width, m_height, m_inputs.m_pColors->getDepth());
    m_outputSummedColorImages[0].fill(0.0f);

    m_estimatesCountImages.resize(m_parameters.m_nbOfCores);
    m_estimatesCountImages[0].resize(m_width, m_height, 1);
    m_estimatesCountImages[0].fill(0);

    for (int i = 1; i < m_parameters.m_nbOfCores; ++i)
    {
        m_outputSummedColorImages[i] = m_outputSummedColorImages[0];
        m_estimatesCountImages[i] = m_estimatesCountImages[0];
    }

    m_markedPatchCenters.reset(new std::atomic<bool>[m_nbOfPixels]);
    for (int i = 0; i < m_nbOfPixels; ++i)
        m_markedPatchCenters[i].store(false, std::memory_order_relaxed);

    std::atomic<int> nbOfPixelsComputed(0);

    std::atomic<bool> abortRequested(false);

    const size_t workerCount = static_cast<size_t>(m_parameters.m_nbOfCores);
    const size_t chunkSize = pixelSet.size() / workerCount;
    const size_t extraPixels = pixelSet.size() % workerCount;
    vector<thread> denoiseThreads;
    denoiseThreads.reserve(workerCount);

    {
        // Workers refer to local pixels, counters, and flags. Join them before
        // that state is destroyed, including when launching a worker throws.
        struct JoinThreads
        {
            vector<thread>& threads;
            ~JoinThreads()
            {
                for (auto& worker : threads)
                {
                    if (worker.joinable())
                        worker.join();
                }
            }
        } joiner{denoiseThreads};

        auto startPixelIndexIt = pixelSet.cbegin();
        for (size_t i = 0; i < workerCount; ++i)
        {
            const auto endPixelIndexIt = startPixelIndexIt + chunkSize + (i < extraPixels ? 1 : 0);
            denoiseThreads.emplace_back(
                &Denoiser::doDenoise,
                this,
                startPixelIndexIt,
                endPixelIndexIt,
                pixelSet.size(),
                std::ref(nbOfPixelsComputed),
                i,
                std::ref(abortRequested));
            startPixelIndexIt = endPixelIndexIt;
        }
    }

    m_outputs.m_pDenoisedColors->resize(m_width, m_height, 3);
    m_outputs.m_pDenoisedColors->fill(0.0f);

    if (m_callbacks && m_callbacks->isAborted())
        return false;

    finalAggregation();

    if (m_parameters.m_markInvalidPixels)
        markNegativeInfNaNValues();
    else
        fixNegativeInfNaNValues();

    if (m_callbacks)
        m_callbacks->progress(1.0f);

    return true;
}

void Denoiser::doDenoise(
    vector<PixelPosition>::const_iterator  i_pixelBegin,
    vector<PixelPosition>::const_iterator  i_pixelEnd,
    const size_t                           i_totalNbOfPixels,
    std::atomic<int>&                      i_nbOfPixelsComputed,
    const size_t                           i_threadIndex,
    std::atomic<bool>&                      i_abortRequested)
{
    DenoisingUnit denoisingUnit(*this, static_cast<int>(i_threadIndex));

    int currentPercentage = 0, newPercentage = 0;

    for (auto it = i_pixelBegin; it != i_pixelEnd; ++it)
    {
        if (i_abortRequested)
            break;

        PixelPosition mainPatchCenter = *it;
        denoisingUnit.denoisePatchAndSimilarPatches(mainPatchCenter);

        ++i_nbOfPixelsComputed;

        if (i_threadIndex == 0)
        {
            newPercentage = static_cast<int>(
                (static_cast<std::uint64_t>(i_nbOfPixelsComputed.load()) * 100) / i_totalNbOfPixels);
            if (newPercentage != currentPercentage)
            {
                if (m_callbacks)
                {
                    i_abortRequested = m_callbacks->isAborted();
                    m_callbacks->progress(float(currentPercentage) * 0.01f);
                }

                currentPercentage = newPercentage;
            }
        }
    }
}

void Denoiser::computeNbOfSamplesSqrt()
{
    m_nbOfSamplesSqrtImage = *(m_inputs.m_pNbOfSamples);

    for (float* pPixelValues : m_nbOfSamplesSqrtImage)
        pPixelValues[0] = sqrt(pPixelValues[0]);
}

void Denoiser::computePixelCovFromSampleCov()
{
    m_pixelCovarianceImage = *(m_inputs.m_pSampleCovariances);
    ImfIt covIt = m_pixelCovarianceImage.begin();
    float nbOfSamplesInv;

    for (const float* pPixelNbOfSamples : *(m_inputs.m_pNbOfSamples))
    {
        nbOfSamplesInv = 1.0f / *pPixelNbOfSamples;
        covIt[0] *= nbOfSamplesInv;
        covIt[1] *= nbOfSamplesInv;
        covIt[2] *= nbOfSamplesInv;
        covIt[3] *= nbOfSamplesInv;
        covIt[4] *= nbOfSamplesInv;
        covIt[5] *= nbOfSamplesInv;
        ++covIt;
    }
}

void Denoiser::reorderPixelSet(vector<PixelPosition>& io_rPixelSet) const
{
    if (m_parameters.m_useRandomPixelOrder)
        reorderPixelSetShuffle(io_rPixelSet);
    else if (m_parameters.m_nbOfCores > 1)
        reorderPixelSetJumpNextStrip(io_rPixelSet);
}

void Denoiser::reorderPixelSetJumpNextStrip(
    vector<PixelPosition>&  io_rPixelSet) const
{
    int widthWithoutBorder = m_width - 2 * m_parameters.m_patchRadius;

    BCD_UNUSED int heightWithoutBorder = m_height - 2 * m_parameters.m_patchRadius;
    BCD_UNUSED int nbOfPixelsWithoutBorder = widthWithoutBorder * heightWithoutBorder;

    assert(nbOfPixelsWithoutBorder == io_rPixelSet.size());

    int chunkSize = widthWithoutBorder * (2 * m_parameters.m_searchWindowRadius);
    // chunkSize is the number of pixels of a strip of 2*searchWindowRadius lines
    reorderPixelSetJumpNextChunk(io_rPixelSet, chunkSize);
}

void Denoiser::reorderPixelSetJumpNextChunk(
    vector<PixelPosition>&  io_rPixelSet,
    int                     i_chunkSize)
{
    if (i_chunkSize <= 0)
        return;
    const int nbOfFullChunks = static_cast<int>(io_rPixelSet.size()) / i_chunkSize;
    if (nbOfFullChunks < 2)
        return;
    const int doubleChunkSize = 2 * i_chunkSize;

    vector<PixelPosition> pixelSetCopy(io_rPixelSet);
    vector<PixelPosition>::iterator inputIt = pixelSetCopy.begin();
    vector<PixelPosition>::iterator outputIt = io_rPixelSet.begin();

    for (int chunkIndexStart = 0; chunkIndexStart < 2; ++chunkIndexStart)
    {
        inputIt = pixelSetCopy.begin() + chunkIndexStart * i_chunkSize;
        for (int chunkIndex = chunkIndexStart; chunkIndex < nbOfFullChunks;)
        {
            copy(inputIt, inputIt + i_chunkSize, outputIt);
            outputIt += i_chunkSize;
            chunkIndex += 2;
            if (chunkIndex < nbOfFullChunks)
                inputIt += doubleChunkSize;
        }
    }
}

void Denoiser::reorderPixelSetShuffle(vector<PixelPosition>& io_rPixelSet)
{
    shuffle(io_rPixelSet.begin(), io_rPixelSet.end(), std::mt19937(15383617));
}

void Denoiser::finalAggregation()
{
    int nbOfImages = static_cast<int>(m_outputSummedColorImages.size());

    for (int bufferIndex = 1; bufferIndex < nbOfImages; ++bufferIndex)
    {
        ImfIt it = m_outputSummedColorImages[bufferIndex].begin();

        for (float* pSum : m_outputSummedColorImages[0])
        {
            pSum[0] += it[0];
            pSum[1] += it[1];
            pSum[2] += it[2];
            ++it;
        }
    }

    for (int bufferIndex = 1; bufferIndex < nbOfImages; ++bufferIndex)
    {
        DeepImage<int>::iterator it = m_estimatesCountImages[bufferIndex].begin();

        for (int* pSum : m_estimatesCountImages[0])
        {
            pSum[0] += it[0];
            ++it;
        }
    }

    ImfIt sumIt = m_outputSummedColorImages[0].begin();
    DeepImage<int>::iterator countIt = m_estimatesCountImages[0].begin();
    float countInv;

    for (float* pFinalColor : *(m_outputs.m_pDenoisedColors))
    {
        countInv = 1.0f / countIt[0];
        pFinalColor[0] = countInv * sumIt[0];
        pFinalColor[1] = countInv * sumIt[1];
        pFinalColor[2] = countInv * sumIt[2];
        ++sumIt;
        ++countIt;
    }
}

namespace
{
    void reportInvalidPixels(
        ICallbacks*     i_callbacks,
        const size_t    i_nbInvalidPixels)
    {
        if (i_nbInvalidPixels != 0 && i_callbacks)
        {
            i_callbacks->debug()
                << i_nbInvalidPixels
                << " invalid pixels found after denoising.\n";
        }
    }
}

void Denoiser::fixNegativeInfNaNValues()
{
    const Deepimf& src = *m_inputs.m_pColors;
    Deepimf& dst = *m_outputs.m_pDenoisedColors;

    int width = dst.getWidth();
    int height = dst.getHeight();
    int depth = dst.getDepth();

    size_t nbInvalidPixels = 0;

    for (int line = 0; line < height; ++line)
    {
        for (int col = 0; col < width; ++col)
        {
            bool anyInvalidChannel = false;

            for (int z = 0; z < depth; ++z)
            {
                const float val = dst.get(line, col, z);

                if (val < 0.0f || !isFinite(val))
                {
                    // Recover the original, not denoised value.
                    dst.set(line, col, z, src.get(line, col, z));
                    anyInvalidChannel = true;
                }
            }

            if (anyInvalidChannel)
                ++nbInvalidPixels;
        }
    }

#ifndef NDEBUG
    reportInvalidPixels(m_callbacks, nbInvalidPixels);
#endif
}

void Denoiser::markNegativeInfNaNValues()
{
    Deepimf& dst = *m_outputs.m_pDenoisedColors;

    int width = dst.getWidth();
    int height = dst.getHeight();
    int depth = dst.getDepth();

    size_t nbInvalidPixels = 0;

    for (int line = 0; line < height; ++line)
    {
        for (int col = 0; col < width; ++col)
        {
            bool anyInvalidChannel = false;

            for (int z = 0; z < depth; ++z)
            {
                const float val = dst.get(line, col, z);

                if (val < 0.0f || !isFinite(val))
                {
                    // Set invalid pixel pink.
                    dst.set(line, col, z, z == 1 ? 0.0f : 1.0f);
                    anyInvalidChannel = true;
                }
            }

            if (anyInvalidChannel)
                ++nbInvalidPixels;
        }
    }

#ifndef NDEBUG
    reportInvalidPixels(m_callbacks, nbInvalidPixels);
#endif
}

} // namespace bcd
