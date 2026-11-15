
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

#include "foundation/math/differentials.h"
#include "foundation/utility/test.h"
#include <algorithm>

using namespace foundation;

TEST_SUITE(Foundation_Math_Differentials)
{
    TEST_CASE(UnitVectorGradientsAgreeWithNeighboringValues)
    {
        for (const double scale : {0.001, 1.0, 1000.0})
        for (const Vector3d raw_direction : {Vector3d(0.2, 1.0, 0.1), Vector3d(1.0, -0.4, -0.2)})
        for (const Vector3d slope : {Vector3d(0.3, -0.2, 0.4), Vector3d(0.0), raw_direction})
        {
            const Vector3d raw = scale * raw_direction;
            const Vector3d unit = normalize(raw);
            const double step = 1.0e-5 * norm(raw);
            const Vector3d expected = (normalize(raw + step * slope) - normalize(raw - step * slope)) / (2.0 * step);
            const Vector3d actual = differentiate_normalize(raw, unit, slope);
            EXPECT_LT(1.0e-8 * std::max(1.0, norm(expected)), norm(actual - expected));
            EXPECT_LT(1.0e-10 * std::max(1.0, norm(actual)), std::abs(dot(unit, actual)));
        }
    }

    TEST_CASE(ReferenceFramesDifferentiateTheirRawTangents)
    {
        const double Step = 1.0e-5;
        const Vector3d normal_slope(0.3, -0.2, 0.4);
        const Vector3d tangent_slope(-0.4, 0.1, 0.3);
        for (const Vector3d raw_normal : {Vector3d(0.2, 1.0, 0.1), Vector3d(0.4, -0.2, -1.0), Vector3d(0.0, 0.0, 1.0)})
        for (const double scale : {0.3, 1.0, 3.0})
        {
            const Vector3d tangent = scale * Vector3d(1.0, 0.3, 0.2);
            const Vector3d n = normalize(raw_normal);
            const Vector3d dn = (normal_slope - n * dot(n, normal_slope)) / norm(raw_normal);
            const Basis3d basis(n, tangent);
            const Basis3d left(normalize(raw_normal - Step * normal_slope), tangent - Step * tangent_slope);
            const Basis3d right(normalize(raw_normal + Step * normal_slope), tangent + Step * tangent_slope);
            Vector3d du, dv;
            differentiate_basis(basis, tangent, dn, tangent_slope, du, dv);
            EXPECT_LT(1.0e-7, norm(du - (right.get_tangent_u() - left.get_tangent_u()) / (2.0 * Step)));
            EXPECT_LT(1.0e-7, norm(dv - (right.get_tangent_v() - left.get_tangent_v()) / (2.0 * Step)));
            EXPECT_LT(1.0e-12, std::abs(dot(du, basis.get_tangent_u())));
            EXPECT_LT(1.0e-12, std::abs(dot(dv, basis.get_tangent_v())));
            EXPECT_LT(1.0e-12, std::abs(dot(du, basis.get_tangent_v()) + dot(basis.get_tangent_u(), dv)));
            EXPECT_LT(1.0e-12, std::abs(dot(du, n) + dot(basis.get_tangent_u(), dn)));
        }
    }

    TEST_CASE(NormalOnlyFramesFollowBothHemisphereBranches)
    {
        const double Step = 1.0e-5;
        const Vector3d slope(0.3, -0.2, 0.4);
        for (const Vector3d raw : {Vector3d(0.2, 1.0, 0.1), Vector3d(0.4, -0.2, -1.0),
             Vector3d(0.0, 0.0, 1.0), Vector3d(0.0, 0.0, -1.0)})
        {
            const Vector3d n = normalize(raw);
            const Vector3d dn = (slope - n * dot(n, slope)) / norm(raw);
            const Basis3d left(normalize(raw - Step * slope));
            const Basis3d right(normalize(raw + Step * slope));
            Vector3d du, dv;
            differentiate_basis(n, dn, du, dv);
            EXPECT_LT(1.0e-8, norm(du - (right.get_tangent_u() - left.get_tangent_u()) / (2.0 * Step)));
            EXPECT_LT(1.0e-8, norm(dv - (right.get_tangent_v() - left.get_tangent_v()) / (2.0 * Step)));
        }
    }
}
