
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
#include "foundation/math/basis.h"
#include "foundation/math/fp.h"
#include "foundation/math/microfacet.h"
#include "foundation/math/qmc.h"
#include "foundation/math/sampling/mappings.h"
#include "foundation/math/scalar.h"
#include "foundation/math/vector.h"
#include "foundation/utility/test.h"
#include "foundation/utility/iostreamop.h"

// Standard headers.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <type_traits>

using namespace foundation;

TEST_SUITE(Foundation_Math_Microfacet)
{
    // A double-precision value path permits small finite differences at
    // grazing incidence without single-precision cancellation. It does not
    // evaluate or reuse the production sampling Jacobian.
    Vector3d ggx_sample_reference(const Vector3d& input, const Vector2d& random, const Vector2d& alpha)
    {
        const double side = input.y < 0.0 ? -1.0 : 1.0;
        const Vector3d view = normalize(side * Vector3d(input.x * alpha.x, input.y, input.z * alpha.y));
        const Vector3d tangent = view.y < static_cast<double>(0.9999f) ?
            normalize(cross(view, Vector3d(0.0, 1.0, 0.0))) : Vector3d(1.0, 0.0, 0.0);
        const Vector3d bitangent = cross(tangent, view);
        const double split = 1.0 / (1.0 + view.y);
        const double radius = std::sqrt(random.x);
        const double angle = random.y < split ? random.y * Pi<double>() / split :
            Pi<double>() + (random.y - split) * Pi<double>() / (1.0 - split);
        const double x = radius * std::cos(angle);
        const double y = radius * std::sin(angle) * (random.y < split ? 1.0 : view.y);
        const double z = std::sqrt(std::max(0.0, 1.0 - x * x - y * y));
        const Vector3d hemisphere = x * tangent + y * bitangent + z * view;
        return normalize(Vector3d(alpha.x * hemisphere.x, std::max(0.0, hemisphere.y), alpha.y * hemisphere.z));
    }

    TEST_CASE(VisibleNormalSamplingJacobiansMatchFixedSampleFiniteDifferences)
    {
        const auto check = [&](auto distribution)
        {
            using MDF = decltype(distribution);
            const float Step = 0.0005f;
            const float DerivativeTolerance = 0.003f;
            const Vector3f directions[] = {
                Vector3f(0.0f, 1.0f, 0.0f), Vector3f(0.0f, -1.0f, 0.0f),
                normalize(Vector3f(0.3f, 1.0f, 0.2f)),
                normalize(Vector3f(0.3f, -1.0f, 0.2f)),
                normalize(Vector3f(1.0f, 0.05f, 0.3f)),
                normalize(Vector3f(-1.0f, 0.01f, -0.5f))};
            const Vector2f roughness[] = {
                Vector2f(0.01f), Vector2f(0.1f), Vector2f(0.36f),
                Vector2f(0.8f), Vector2f(0.1f, 0.6f), Vector2f(0.7f, 0.2f)};
            const Vector2f samples[] = {
                Vector2f(0.0f, 0.2f), Vector2f(0.2f, 0.1f),
                Vector2f(0.6f, 0.4f), Vector2f(0.8f, 0.7f), Vector2f(0.97f, 0.9f)};
            for (const Vector3f& v : directions)
            {
                const Basis3f frame(v);
                for (const Vector2f& alpha : roughness)
                {
                    for (const Vector2f& random : samples)
                    {
                        Matrix3f jacobian;
                        const Vector3f normal = MDF::sample(v, random, alpha.x, alpha.y, jacobian);
                        EXPECT_EQ(MDF::sample(v, random, alpha.x, alpha.y), normal);
                        if constexpr (std::is_same<MDF, GGXMDF>::value)
                        {
                            constexpr double ValueTolerance = 1.0e-5;
                            EXPECT_LT(ValueTolerance, norm(Vector3d(normal) -
                                ggx_sample_reference(Vector3d(v), Vector2d(random), Vector2d(alpha))));
                        }
                        for (const Vector3f& dv : {frame.get_tangent_u(), frame.get_tangent_v()})
                        {
                            Vector3f finite;
                            if constexpr (std::is_same<MDF, GGXMDF>::value)
                            {
                                constexpr double ReferenceStep = 1.0e-5;
                                const Vector3d direction(v), tangent(dv);
                                const Vector3d left = ggx_sample_reference(
                                    normalize(direction - ReferenceStep * tangent), Vector2d(random), Vector2d(alpha));
                                const Vector3d right = ggx_sample_reference(
                                    normalize(direction + ReferenceStep * tangent), Vector2d(random), Vector2d(alpha));
                                finite = Vector3f((right - left) / (2.0 * ReferenceStep));
                            }
                            else
                            {
                                const Vector3f left = MDF::sample(normalize(v - Step * dv), random, alpha.x, alpha.y);
                                const Vector3f right = MDF::sample(normalize(v + Step * dv), random, alpha.x, alpha.y);
                                finite = (right - left) / (2.0f * Step);
                            }
                            const Vector3f predicted = jacobian * dv;
                            EXPECT_LT(DerivativeTolerance * std::max(1.0f, norm(finite)), norm(predicted - finite));
                            EXPECT_LT(DerivativeTolerance, std::abs(dot(normal, predicted)));
                        }
                        // Scaling the input direction does not change the sampled
                        // normal, because hemisphere stretching normalizes it.
                        EXPECT_LT(DerivativeTolerance, norm(jacobian * v));
                    }
                }
            }
        };
        check(GGXMDF());
        check(BeckmannMDF());
    }

    class StdMDFAdapter
    {
      public:
        explicit StdMDFAdapter(const float gamma)
          : m_gamma(gamma)
        {
        }

        float D(
            const Vector3f&     m,
            const float         alpha_x,
            const float         alpha_y) const
        {
            return StdMDF::D(m, alpha_x, alpha_y, m_gamma);
        }

        float G(
            const Vector3f&     wi,
            const Vector3f&     wo,
            const Vector3f&     m,
            const float         alpha_x,
            const float         alpha_y) const
        {
            return StdMDF::G(wi, wo, m, alpha_x, alpha_y, m_gamma);
        }

        float G1(
            const Vector3f&     v,
            const Vector3f&     m,
            const float         alpha_x,
            const float         alpha_y) const
        {
            return StdMDF::G1(v, m, alpha_x, alpha_y, m_gamma);
        }

        Vector3f sample(
            const Vector3f&     v,
            const Vector2f&     s,
            const float         alpha_x,
            const float         alpha_y) const
        {
            return StdMDF::sample(v, s, alpha_x, alpha_y, m_gamma);
        }

        float pdf(
            const Vector3f&     v,
            const Vector3f&     m,
            const float         alpha_x,
            const float         alpha_y) const
        {
            return StdMDF::pdf(v, m, alpha_x, alpha_y, m_gamma);
        }

      private:
        const float m_gamma;
    };

    template <typename MDF>
    bool is_positive(
        const MDF&    mdf,
        const float   alpha_x,
        const float   alpha_y,
        const size_t  sample_count)
    {
        for (size_t i = 0; i < sample_count; ++i)
        {
            static const size_t Bases[] = { 2 };
            const Vector2f s = hammersley_sequence<float, 2>(Bases, sample_count, i);

            const Vector3f h = sample_hemisphere_uniform(s);
            const float value = mdf.D(h, alpha_x, alpha_y);

            if (value < 0.0f)
                return false;
        }

        return true;
    }

    template <typename MDF>
    float integrate(
        const MDF&    mdf,
        const float   alpha,
        const size_t  sample_count)
    {
        float integral = 0.0f;

        for (size_t i = 0; i < sample_count; ++i)
        {
            const float theta = radical_inverse_base2<float>(i) * HalfPi<float>();
            const Vector3f h(0.0f, std::cos(theta), 0.0f);
            const float value = mdf.D(h, alpha, alpha);

            integral += value * h.y * std::sin(theta);
        }

        integral *= HalfPi<float>() / sample_count;     // integration over theta
        integral *= TwoPi<float>();                     // integration over phi

        return integral;
    }


    //
    // Weak white furnace test.
    //
    // Reference:
    //
    //   Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs
    //   http://hal.inria.fr/docs/00/96/78/44/PDF/RR-8468.pdf
    //

    struct WeakWhiteFurnaceTestResult
    {
        float m_min_G1;
        float m_max_G1;
        float m_min_result;
        float m_max_result;
    };

    template <typename MDF>
    void weak_white_furnace_test(
        const MDF&                  mdf,
        const size_t                num_runs,
        const float                 alpha_x,
        const float                 alpha_y,
        const float                 angle_step,
        WeakWhiteFurnaceTestResult& result)
    {
        result.m_min_G1 =  std::numeric_limits<float>::max();
        result.m_max_G1 = -std::numeric_limits<float>::max();
        result.m_min_result =  std::numeric_limits<float>::max();
        result.m_max_result = -std::numeric_limits<float>::max();

        for (size_t i = 0; i < num_runs; ++i)
        {
            static const size_t Bases[] = { 2 };
            const Vector2f s = hammersley_sequence<float, 2>(Bases, num_runs, i);
            const Vector3f v = sample_hemisphere_uniform(s);
            const float G1 = mdf.G1(v, Vector3f(0.0f, 1.0, 0.0f), alpha_x, alpha_y);

            result.m_min_G1 = std::min(result.m_min_G1, G1);
            result.m_max_G1 = std::max(result.m_max_G1, G1);

            const float cos_thetha_o_4 = std::abs(4.0f * v.y);

            float integral = 0.0f;

            for (float theta = 0.0f; theta < Pi<float>(); theta += angle_step)
            {
                const float cos_theta = std::cos(theta);
                const float sin_theta = std::sin(theta);

                for (float phi = 0.0f; phi < TwoPi<float>(); phi += angle_step)
                {
                    const float cos_phi = std::cos(phi);
                    const float sin_phi = std::sin(phi);

                    const Vector3f l =
                        Vector3f::make_unit_vector(
                            cos_theta,
                            sin_theta,
                            cos_phi,
                            sin_phi);

                    const Vector3f h = normalize(v + l);

                    if (h.y > 0.0f)
                        integral += sin_theta * mdf.D(h, alpha_x, alpha_y) * G1 / cos_thetha_o_4;
                }
            }

            // Result should be 1.
            integral *= square(angle_step);

            result.m_min_result = std::min(result.m_min_result, integral);
            result.m_max_result = std::max(result.m_max_result, integral);
        }
    }

#define EXPECT_WEAK_WHITE_FURNACE_PASS(result)                              \
    do                                                                      \
    {                                                                       \
        EXPECT_NEQ(result.m_min_G1, result.m_max_G1);                       \
        EXPECT_FEQ_EPS(1.0f, result.m_min_result, WeakWhiteFurnaceEps);     \
        EXPECT_FEQ_EPS(1.0f, result.m_max_result, WeakWhiteFurnaceEps);     \
    } while (false)


    //
    // Test settings.
    //

    const size_t PositivityTestSampleCount = 256;
    const size_t IntegrationSampleCount = 8192;
    const float IntegrationEps = 1.0e-3f;
    const size_t WeakWhiteFurnaceRuns = 128;
    const float WeakWhiteFurnaceAngleStep = 0.0125f;
    const float WeakWhiteFurnaceEps = 0.05f;


    //
    // Blinn-Phong MDF.
    //

    TEST_CASE(BlinnMDF_Evaluate_ReturnsNonNegativeValues)
    {
        const BlinnMDF mdf = {};

        EXPECT_TRUE(is_positive(mdf, 10.0f, 10.0f, PositivityTestSampleCount));
    }

    TEST_CASE(BlinnMDF_Evaluate_GivenCosThetaIsZero_ReturnsZero)
    {
        const float limit = BlinnMDF::D(Vector3f(0.0f), 10.0f, 10.0f);

        EXPECT_FEQ(0.0f, limit);
    }

    TEST_CASE(BlinnMDF_Integral_EqualsOne)
    {
        const BlinnMDF mdf = {};

        const float integral = integrate(mdf, 10.0f, IntegrationSampleCount);

        EXPECT_FEQ_EPS(1.0f, integral, IntegrationEps);
    }


    //
    // Beckmann MDF.
    //

    TEST_CASE(BeckmannMDF_Evaluate_ReturnsNonNegativeValues)
    {
        const BeckmannMDF mdf = {};

        EXPECT_TRUE(is_positive(mdf, 0.5f, 0.5f, PositivityTestSampleCount));
    }

    TEST_CASE(BeckmannMDF_Evaluate_GivenCosThetaIsZero_ReturnsZero)
    {
        const float limit = BeckmannMDF::D(Vector3f(0.0f), 0.5f, 0.5f);

        EXPECT_FEQ(0.0f, limit);
    }

    TEST_CASE(BeckmannMDF_Integral_EqualsOne)
    {
        const BeckmannMDF mdf = {};

        const float integral = integrate(mdf, 0.5f, IntegrationSampleCount);

        EXPECT_FEQ_EPS(1.0f, integral, IntegrationEps);
    }

    TEST_CASE(BeckmannMDF_Isotropic_WeakWhiteFurnace)
    {
        WeakWhiteFurnaceTestResult result;
        weak_white_furnace_test(
            BeckmannMDF(),
            WeakWhiteFurnaceRuns,
            0.6f,
            0.6f,
            WeakWhiteFurnaceAngleStep,
            result);

        EXPECT_WEAK_WHITE_FURNACE_PASS(result);
    }

    TEST_CASE(BeckmannMDF_Anisotropic_WeakWhiteFurnace)
    {
        WeakWhiteFurnaceTestResult result;
        weak_white_furnace_test(
            BeckmannMDF(),
            WeakWhiteFurnaceRuns,
            0.25f,
            0.5f,
            WeakWhiteFurnaceAngleStep,
            result);

        EXPECT_WEAK_WHITE_FURNACE_PASS(result);
    }


    //
    // GGX MDF.
    //

    TEST_CASE(GGXMDF_SampleHandlesBothNormalIncidenceDirections)
    {
        for (const float alpha : {0.001f, 0.25f, 1.0f})
        {
            for (const Vector2f s : {Vector2f(0.25f, 0.3f), Vector2f(0.75f, 0.9f)})
            {
                const Vector3f front = GGXMDF::sample(Vector3f(0.0f, 1.0f, 0.0f), s, alpha, alpha);
                const Vector3f back = GGXMDF::sample(Vector3f(0.0f, -1.0f, 0.0f), s, alpha, alpha);
                EXPECT_TRUE(is_normalized(front));
                EXPECT_TRUE(is_normalized(back));
                EXPECT_TRUE(back.y > 0.0f);
                EXPECT_FEQ(front, back);
            }
        }
    }

    TEST_CASE(GGXMDF_Evaluate_ReturnsNonNegativeValues)
    {
        const GGXMDF mdf = {};

        EXPECT_TRUE(is_positive(mdf, 0.5f, 0.5f, PositivityTestSampleCount));
    }

    TEST_CASE(GGXMDF_Evaluate_GivenCosThetaIsZero_ReturnsLimitValue)
    {
        const float AlphaG = 0.5f;
        const float ExpectedLimit = AlphaG * AlphaG * RcpPi<float>();

        const float limit = GGXMDF::D(Vector3f(0.0f), AlphaG, AlphaG);

        EXPECT_FEQ(ExpectedLimit, limit);
    }

    TEST_CASE(GGXMDF_Integral_EqualsOne)
    {
        const GGXMDF mdf = {};

        const float integral = integrate(mdf, 0.5f, IntegrationSampleCount);

        EXPECT_FEQ_EPS(1.0f, integral, IntegrationEps);
    }

    TEST_CASE(GGXMDF_Isotropic_WeakWhiteFurnace)
    {
        WeakWhiteFurnaceTestResult result;
        weak_white_furnace_test(
            GGXMDF(),
            WeakWhiteFurnaceRuns,
            0.35f,
            0.35f,
            WeakWhiteFurnaceAngleStep,
            result);

        EXPECT_WEAK_WHITE_FURNACE_PASS(result);
    }

    TEST_CASE(GGXMDF_Anisotropic_WeakWhiteFurnace)
    {
        WeakWhiteFurnaceTestResult result;
        weak_white_furnace_test(
            GGXMDF(),
            WeakWhiteFurnaceRuns,
            0.25f,
            0.5f,
            WeakWhiteFurnaceAngleStep,
            result);

        EXPECT_WEAK_WHITE_FURNACE_PASS(result);
    }


    //
    // Ward MDF.
    //

    TEST_CASE(WardMDF_Evaluate_ReturnsNonNegativeValues)
    {
        const WardMDF mdf = {};

        EXPECT_TRUE(is_positive(mdf, 0.5f, 0.5f, PositivityTestSampleCount));
    }

    TEST_CASE(WardMDF_Evaluate_GivenCosThetaIsZero_ReturnsZero)
    {
        const float limit = WardMDF::D(Vector3f(0.0f), 0.5f, 0.5f);

        EXPECT_FEQ(0.0f, limit);
    }


    //
    // GTR1 MDF.
    //

    TEST_CASE(GTR1MDF_Evaluate_ReturnsNonNegativeValues)
    {
        const GTR1MDF mdf = {};

        EXPECT_TRUE(is_positive(mdf, 10.0f, 10.0f, PositivityTestSampleCount));
    }

    TEST_CASE(GTR1MDF_Integral_EqualsOne)
    {
        const GTR1MDF mdf = {};

        const float integral = integrate(mdf, 10.0f, IntegrationSampleCount);

        EXPECT_FEQ_EPS(1.0f, integral, IntegrationEps);
    }

    TEST_CASE(GTR1MDF_Isotropic_WeakWhiteFurnace)
    {
        WeakWhiteFurnaceTestResult result;
        weak_white_furnace_test(
            GTR1MDF(),
            WeakWhiteFurnaceRuns,
            0.21f,
            0.21f,
            WeakWhiteFurnaceAngleStep,
            result);

        EXPECT_WEAK_WHITE_FURNACE_PASS(result);
    }


    //
    // STD MDF.
    //

    TEST_CASE(StdMDF_Evaluate_ReturnsNonNegativeValues)
    {
        const StdMDFAdapter mdf(2.0f);
        EXPECT_TRUE(is_positive(mdf, 0.5f, 0.5f, PositivityTestSampleCount));
    }

    TEST_CASE(StdMDF_Evaluate_GivenCosThetaIsZero_ReturnsZero)
    {
        const float limit = StdMDF::D(Vector3f(0.0f), 0.5f, 0.5f, 2.0f);

        EXPECT_FEQ(0.0f, limit);
    }

    TEST_CASE(StdMDF_Integral_EqualsOne)
    {
        const StdMDFAdapter mdf(2.0f);
        const float integral = integrate(mdf, 0.5f, IntegrationSampleCount);

        EXPECT_FEQ_EPS(1.0f, integral, IntegrationEps);
    }

    TEST_CASE(StdMDF_2_GGXMDF_G1_comparison)
    {
        const size_t SampleCount = 128;

        for (size_t i = 0; i < SampleCount; ++i)
        {
            static const size_t Bases[] = { 2 };
            const Vector2f s = hammersley_sequence<float, 2>(Bases, SampleCount, i);
            const Vector3f v = sample_hemisphere_uniform(s);
            const float std_G1 = StdMDF::G1(v, Vector3f(0.0f, 1.0f, 0.0f), 0.5f, 0.5f, 2.0f);
            const float ggx_G1 = GGXMDF::G1(v, Vector3f(0.0f, 1.0f, 0.0f), 0.5f, 0.5f);
            EXPECT_FEQ_EPS(std_G1, ggx_G1, 0.01f);
        }
    }

    TEST_CASE(StdMDF_lambda_overflow)
    {
        const float GammaMax = 40.0f;
        const float GammaStep = 2.0f;
        const size_t SampleCount = 128;

        for (float i = 2.0f; i <= GammaMax; i += GammaStep)
        {
            for (size_t j = 0; j < SampleCount; ++j)
            {
                static const size_t Bases[] = { 2 };
                const Vector2f s = hammersley_sequence<float, 2>(Bases, SampleCount, j);
                const Vector3f v = sample_hemisphere_uniform(s);
                const float std_G1 = StdMDF::G1(v, Vector3f(0.0f, 1.0f, 0.0f), 0.5f, 0.5f, i);
                EXPECT_TRUE(FP<float>::is_finite(std_G1));  // check that G1 doesn't produce NaN
            }
        }

    }

    TEST_CASE(StdMDF_D_overflow)
    {
        const float GammaMax = 40.0f;
        const float GammaStep = 2.0f;
        const size_t SampleCount = 128;

        for (float i = 2.0f; i <= GammaMax; i += GammaStep)
        {
            for (size_t j = 0; j < SampleCount; ++j)
            {
                static const size_t Bases[] = { 2 };
                const Vector2f s = hammersley_sequence<float, 2>(Bases, SampleCount, j);
                const Vector3f h = sample_hemisphere_uniform(s);
                const float std_D = StdMDF::D(h, 0.5f, 0.5f, i);
                EXPECT_TRUE(FP<float>::is_finite(std_D));   // check that D doesn't produce NaN
            }
        }
    }

    TEST_CASE(StdMDF_Isotropic_WeakWhiteFurnace)
    {
        const StdMDFAdapter mdf(2.0f);
        WeakWhiteFurnaceTestResult result;
        weak_white_furnace_test(
            mdf,
            WeakWhiteFurnaceRuns,
            0.6f,
            0.6f,
            WeakWhiteFurnaceAngleStep,
            result);

        EXPECT_WEAK_WHITE_FURNACE_PASS(result);
    }

#undef EXPECT_WEAK_WHITE_FURNACE_PASS
}
