
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

TEST_SUITE(Renderer_Kernel_Denoising_BCD)
{
    struct Fixture
    {
        static const int Width = 7;
        bcd::DeepImage<float> colors{Width, 1, 3};
        bcd::DeepImage<float> samples{Width, 1, 1};
        bcd::DeepImage<float> histograms{Width, 1, 3};
        bcd::DeepImage<float> covariance{Width, 1, 6};
        bcd::DeepImage<float> output;
        bcd::Denoiser denoiser;

        Fixture()
        {
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
}
