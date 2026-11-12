
//
// This source file is part of appleseed.
// Visit https://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2010-2013 Francois Beaune, Jupiter Jazz Limited
// Copyright (c) 2014-2018 Francois Beaune, The appleseedhq Organization
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

// appleseed.foundation headers.
#include "foundation/utility/test.h"

// BCD headers.
#include "bcd/DeepImage.h"
#include "bcd/Denoiser.h"

// Standard headers.
#include <atomic>
#include <cstddef>

TEST_SUITE(Renderer_Kernel_Denoising_BCD)
{
    struct Fixture
    {
        static const int Width = 7;
        bcd::DeepImage<float> colors;
        bcd::DeepImage<float> samples;
        bcd::DeepImage<float> histograms;
        bcd::DeepImage<float> covariance;
        bcd::DeepImage<float> output;
        bcd::Denoiser denoiser;

        Fixture()
        {
            initialize(Width, 1);
        }

        void initialize(const int width, const int height)
        {
            colors.resize(width, height, 3);
            samples.resize(width, height, 1);
            histograms.resize(width, height, 3);
            covariance.resize(width, height, 6);
            colors.fill(0.25f);
            samples.fill(8.0f);
            histograms.fill(8.0f);
            covariance.fill(0.0f);
            bcd::DenoiserInputs inputs;
            inputs.m_pColors = &colors;
            inputs.m_pNbOfSamples = &samples;
            inputs.m_pHistograms = &histograms;
            inputs.m_pSampleCovariances = &covariance;
            denoiser.setInputs(inputs);
            bcd::DenoiserOutputs outputs;
            outputs.m_pDenoisedColors = &output;
            denoiser.setOutputs(outputs);
            auto parameters = denoiser.getParameters();
            parameters.m_patchRadius = 0;
            parameters.m_searchWindowRadius = 1;
            parameters.m_useRandomPixelOrder = true;
            parameters.m_markedPixelsSkippingProbability = 0.0f;
            parameters.m_nbOfCores = 1;
            denoiser.setParameters(parameters);
        }
    };

    TEST_CASE_F(SingleWorker_VisitsEveryPixelAndPreservesConstantColor, Fixture)
    {
        ASSERT_TRUE(denoiser.denoise());
        for (int column = 0; column < Width; ++column)
        {
            EXPECT_EQ(1, denoiser.getEstimatesCountImage(0).get(0, column, 0));
            for (int channel = 0; channel < 3; ++channel)
                EXPECT_FEQ(0.25f, output.get(0, column, channel));
        }
    }
    TEST_CASE_F(MultipleWorkers_VisitRemainderPixels, Fixture)
    {
        auto parameters = denoiser.getParameters();
        parameters.m_nbOfCores = 3;
        denoiser.setParameters(parameters);
        ASSERT_TRUE(denoiser.denoise());
        for (int column = 0; column < Width; ++column)
            EXPECT_EQ(1, denoiser.getEstimatesCountImage(0).get(0, column, 0));
    }

    TEST_CASE_F(MoreWorkersThanPixels_StillVisitsEveryPixel, Fixture)
    {
        auto parameters = denoiser.getParameters();
        parameters.m_nbOfCores = Width + 1;
        denoiser.setParameters(parameters);
        ASSERT_TRUE(denoiser.denoise());
        for (int column = 0; column < Width; ++column)
            EXPECT_EQ(1, denoiser.getEstimatesCountImage(0).get(0, column, 0));
    }

    TEST_CASE_F(InvalidPatchRadius_ReturnsFailure, Fixture)
    {
        auto parameters = denoiser.getParameters();
        parameters.m_patchRadius = 1;
        denoiser.setParameters(parameters);
        EXPECT_FALSE(denoiser.denoise());
    }

    TEST_CASE_F(ShortImageWithStripOrdering_StillVisitsEveryPixel, Fixture)
    {
        auto parameters = denoiser.getParameters();
        parameters.m_nbOfCores = 3;
        parameters.m_useRandomPixelOrder = false;
        denoiser.setParameters(parameters);
        ASSERT_TRUE(denoiser.denoise());
        for (int column = 0; column < Width; ++column)
            EXPECT_EQ(1, denoiser.getEstimatesCountImage(0).get(0, column, 0));
    }

    TEST_CASE_F(ConcurrentMarkedPatches_PreserveConstantImage, Fixture)
    {
        initialize(17, 13);
        auto parameters = denoiser.getParameters();
        parameters.m_patchRadius = 1;
        parameters.m_searchWindowRadius = 3;
        parameters.m_nbOfCores = 8;
        parameters.m_markedPixelsSkippingProbability = 1.0f;
        denoiser.setParameters(parameters);
        ASSERT_TRUE(denoiser.denoise());
        for (int i = 0; i < output.getSize(); ++i)
            EXPECT_FEQ(0.25f, output.get(i));
    }

    struct CancelDuringWork : bcd::ICallbacks
    {
        mutable std::atomic<size_t> checks{0};

        void progress(const float) const override {}
        bool isAborted() const override { return checks.fetch_add(1) != 0; }
        void logInfo(const char*) const override {}
        void logWarning(const char*) const override {}
        void logError(const char*) const override {}
        void logDebug(const char*) const override {}
    };

    TEST_CASE_F(CancellationDuringWork_IsReported, Fixture)
    {
        auto parameters = denoiser.getParameters();
        parameters.m_nbOfCores = 3;
        denoiser.setParameters(parameters);
        CancelDuringWork callbacks;
        denoiser.setCallbacks(&callbacks);
        EXPECT_FALSE(denoiser.denoise());
        EXPECT_TRUE(callbacks.checks.load() > 1);
    }

}
