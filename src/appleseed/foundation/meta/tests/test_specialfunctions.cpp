
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
#include "foundation/math/specialfunctions.h"
#include "foundation/utility/test.h"

// Standard headers.
#include <algorithm>
#include <cmath>

using namespace foundation;

TEST_SUITE(Foundation_Math_SpecialFunctions)
{
    TEST_CASE(InverseErrorDerivativeMatchesClampedPolynomialFiniteDifferences)
    {
        constexpr float MaxStep = 0.0005f;
        constexpr float DomainStepFraction = 0.02f;
        constexpr float RelativeTolerance = 0.005f;
        const float values[] = {
            -2.0f, -1.01f, -0.999f, -0.997f, -0.995f, -0.95f, -0.5f,
            -0.1f, 0.0f, 0.1f, 0.5f, 0.95f, 0.995f, 0.997f, 0.999f, 1.01f, 2.0f};
        for (const float x : values)
        {
            float derivative;
            EXPECT_EQ(erf_inv(x), erf_inv(x, derivative));
            // Remain inside each polynomial or the constant clamp branch.
            const float step = std::abs(x) >= 1.0f ? MaxStep :
                std::min(MaxStep, DomainStepFraction * (1.0f - std::abs(x)));
            const float finite = (erf_inv(x + step) - erf_inv(x - step)) / (2.0f * step);
            EXPECT_LT(RelativeTolerance * std::max(1.0f, std::abs(finite)), std::abs(derivative - finite));
            if (std::abs(x) >= 1.0f)
                EXPECT_EQ(0.0f, derivative);
        }
    }
}
