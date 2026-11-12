
//
// This source file is part of appleseed.
// Visit https://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2026 The appleseedhq Organization
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

// appleseed.renderer headers.
#include "renderer/kernel/shading/shadingpoint.h"
#include "renderer/modeling/bsdf/bsdf.h"
#include "renderer/modeling/bsdf/bsdfsample.h"
#include "renderer/modeling/bsdf/plasticbrdf.h"
#include "renderer/utility/paramarray.h"

// appleseed.foundation headers.
#include "foundation/math/rng/xoroshiro128plus.h"
#include "foundation/math/scalar.h"
#include "foundation/memory/arena.h"
#include "foundation/memory/autoreleaseptr.h"
#include "foundation/utility/test.h"

// Standard headers.
#include <algorithm>
#include <cmath>

using namespace foundation;
using namespace renderer;

TEST_SUITE(Renderer_Modeling_BSDF_Plastic)
{
    // Independent double-precision Fresnel and isotropic GGX equations.
    double fresnel(const double cosine, const double ior)
    {
        const double transmitted = std::sqrt(std::max(0.0,
            1.0 - (1.0 - cosine * cosine) / (ior * ior)));
        const double rs = (cosine - ior * transmitted) / (cosine + ior * transmitted);
        const double rp = (ior * cosine - transmitted) / (ior * cosine + transmitted);
        return (rs * rs + rp * rp) * 0.5;
    }

    double smith_lambda(const double cosine, const double alpha)
    {
        return (std::sqrt(1.0 + alpha * alpha * (1.0 - cosine * cosine)
            / (cosine * cosine)) - 1.0) * 0.5;
    }

    struct Fixture
    {
        auto_release_ptr<BSDF> bsdf;
        PlasticBRDFInputValues values{};
        ShadingPoint point;
        BSDF::LocalGeometry geometry;
        Xoroshiro128plus rng;
        SamplingContext sampling;

        Fixture()
          : bsdf(PlasticBRDFFactory().create("plastic", ParamArray()))
          , sampling(rng, SamplingContext::RNGMode)
        {
            values.m_diffuse_reflectance.set(0.5f);
            values.m_diffuse_reflectance_multiplier = 1.0f;
            values.m_specular_reflectance.set(1.0f);
            values.m_specular_reflectance_multiplier = 1.0f;
            values.m_roughness = 0.5f;
            values.m_ior = 1.5f;
            values.m_internal_scattering = 1.0f;
            point.set_ray(ShadingRay(Vector3d(0.0, 1.0, 0.0),
                Vector3d(0.0, -1.0, 0.0), ShadingRay::Time(), VisibilityFlags::CameraRay, 0));
            geometry.m_shading_point = &point;
            geometry.m_geometric_normal = Vector3f(0.0f, 1.0f, 0.0f);
            geometry.m_shading_basis = Basis3f(geometry.m_geometric_normal);
            prepare();
        }

        void prepare()
        {
            Arena arena;
            bsdf->prepare_inputs(arena, point, &values);
        }

        BSDFSample sample(const Vector3f& outgoing, const int modes = ScatteringMode::All)
        {
            BSDFSample result;
            bsdf->sample(sampling, &values, false, false, geometry, Dual3f(outgoing), modes, result);
            return result;
        }

        double reference(const Vector3d& wo, const Vector3d& wi) const
        {
            const double ior = values.m_ior;
            const double fdr = -1.4399 / (ior * ior) + 0.7099 / ior + 0.6681 + 0.0636 * ior;
            const double diffuse = values.m_diffuse_reflectance[0];
            const double denominator = 1.0 - (1.0 - values.m_internal_scattering +
                values.m_internal_scattering * diffuse) * fdr;
            const bool smooth = values.m_roughness == 0.0f;
            const Vector3d half = smooth ? Vector3d(0.0, 1.0, 0.0) : normalize(wi + wo);
            const double fo = fresnel(dot(wo, half), ior);
            const double fi = fresnel(dot(wi, half), ior);
            const double base = (1.0 - fo) * (1.0 - fi) * diffuse
                / (ior * ior * Pi<double>() * denominator);
            if (smooth)
                return base;
            const double alpha = std::max(0.001, double(values.m_roughness) * values.m_roughness);
            const double shape = 1.0 + (alpha * alpha - 1.0) * half.y * half.y;
            const double d = alpha * alpha / (Pi<double>() * shape * shape);
            const double g = 1.0 / (1.0 + smith_lambda(wo.y, alpha) + smith_lambda(wi.y, alpha));
            return base + values.m_specular_reflectance[0] * fo * d * g / (4.0 * wi.y * wo.y);
        }
    };

    TEST_CASE_F(RoughSamples_AgreeWithEvaluationAndMarginalDensity, Fixture)
    {
        const Vector3f outgoing = normalize(Vector3f(0.8f, 0.6f, 0.0f));
        size_t diffuse = 0, glossy = 0;
        const size_t SampleCount = 128;
        for (size_t i = 0; i < SampleCount; ++i)
        {
            const BSDFSample result = sample(outgoing);
            if (result.get_mode() == ScatteringMode::None)
                continue;
            DirectShadingComponents evaluated;
            const Vector3f& incoming = result.m_incoming.get_value();
            const float pdf = bsdf->evaluate(&values, false, false, geometry,
                outgoing, incoming, ScatteringMode::All, evaluated);
            EXPECT_FEQ_EPS(pdf, result.get_probability(), 1.0e-5f);
            EXPECT_FEQ_EPS(pdf, bsdf->evaluate_pdf(&values, false, geometry,
                outgoing, incoming, ScatteringMode::All), 1.0e-5f);
            EXPECT_FEQ_EPS(evaluated.m_beauty[0], result.m_value.m_beauty[0], 1.0e-5f);
            EXPECT_FEQ_EPS(float(reference(Vector3d(outgoing), Vector3d(incoming))),
                result.m_value.m_beauty[0], 1.0e-5f);
            if (result.get_mode() == ScatteringMode::Diffuse) ++diffuse;
            else ++glossy;
        }
        EXPECT_TRUE(diffuse > 0);
        EXPECT_TRUE(glossy > 0);
    }

    TEST_CASE_F(RestrictedRoughModes_ReturnOnlyTheRequestedLobe, Fixture)
    {
        const Vector3f outgoing(0.0f, 1.0f, 0.0f);
        for (const int mode : {ScatteringMode::Diffuse, ScatteringMode::Glossy})
        {
            size_t accepted = 0;
            for (size_t i = 0; i < 32; ++i)
            {
                const BSDFSample result = sample(outgoing, mode);
                if (result.get_mode() == ScatteringMode::None)
                    continue;
                ++accepted;
                EXPECT_EQ(mode, result.get_mode());
                const Vector3f& incoming = result.m_incoming.get_value();
                const float pdf = bsdf->evaluate_pdf(&values, false, geometry, outgoing, incoming, mode);
                EXPECT_FEQ_EPS(pdf, result.get_probability(), 1.0e-5f);
                if (mode == ScatteringMode::Diffuse)
                    EXPECT_FEQ_EPS(incoming.y * RcpPi<float>(), result.get_probability(), 1.0e-6f);
            }
            EXPECT_TRUE(accepted > 0);
        }
        EXPECT_EQ(ScatteringMode::None, sample(outgoing, ScatteringMode::Specular).get_mode());
        EXPECT_EQ(ScatteringMode::None, sample(outgoing, 0).get_mode());
    }

    TEST_CASE_F(SmoothSpecular_IsAnExactFresnelWeightedDelta, Fixture)
    {
        values.m_roughness = 0.0f;
        prepare();
        const Vector3f outgoing(0.6f, 0.8f, 0.0f);
        const BSDFSample result = sample(outgoing, ScatteringMode::Specular);
        ASSERT_EQ(ScatteringMode::Specular, result.get_mode());
        EXPECT_EQ(BSDF::DiracDelta, result.get_probability());
        EXPECT_FEQ_EPS(-0.6f, result.m_incoming.get_value().x, 1.0e-6f);
        EXPECT_FEQ_EPS(0.8f, result.m_incoming.get_value().y, 1.0e-6f);
        EXPECT_FEQ_EPS(float(fresnel(0.8, 1.5)), result.m_value.m_beauty[0] * 0.8f, 1.0e-6f);
        EXPECT_EQ(0.0f, bsdf->evaluate_pdf(&values, false, geometry,
            outgoing, result.m_incoming.get_value(), ScatteringMode::Specular));
    }

    TEST_CASE_F(SmoothMixture_CompensatesTheDeltaSelectionProbability, Fixture)
    {
        values.m_roughness = 0.0f;
        prepare();
        const Vector3f outgoing(0.0f, 1.0f, 0.0f);
        const float f = 0.04f;
        const float specular_probability = f / (f + (1.0f - f) * 0.5f);
        size_t diffuse = 0, specular = 0;
        for (size_t i = 0; i < 256; ++i)
        {
            const BSDFSample result = sample(outgoing);
            ASSERT_TRUE(result.get_mode() != ScatteringMode::None);
            if (result.get_mode() == ScatteringMode::Specular)
            {
                ++specular;
                EXPECT_EQ(BSDF::DiracDelta, result.get_probability());
                EXPECT_FEQ_EPS(f / specular_probability, result.m_value.m_beauty[0], 1.0e-6f);
            }
            else
            {
                ++diffuse;
                EXPECT_EQ(ScatteringMode::Diffuse, result.get_mode());
                EXPECT_FEQ_EPS((1.0f - specular_probability) * result.m_incoming.get_value().y
                    * RcpPi<float>(), result.get_probability(), 1.0e-6f);
            }
        }
        EXPECT_TRUE(diffuse > 0);
        EXPECT_TRUE(specular > 0);
    }

    TEST_CASE_F(RoughSampling_IntegratesToIndependentHemisphereQuadrature, Fixture)
    {
        const size_t PolarCount = 128, AzimuthCount = 256, SampleCount = 32768;
        for (const float roughness : {0.3f, 0.7f})
        {
            values.m_roughness = roughness;
            prepare();
            const Vector3f outgoing(0.8f, 0.6f, 0.0f);
            double integral = 0.0;
            for (size_t t = 0; t < PolarCount; ++t)
            {
                const double mu = (t + 0.5) / PolarCount;
                const double radius = std::sqrt(1.0 - mu * mu);
                for (size_t p = 0; p < AzimuthCount; ++p)
                {
                    const double phi = TwoPi<double>() * (p + 0.5) / AzimuthCount;
                    integral += reference(Vector3d(outgoing),
                        Vector3d(radius * std::cos(phi), mu, radius * std::sin(phi))) * mu;
                }
            }
            integral *= TwoPi<double>() / (PolarCount * AzimuthCount);
            double estimate = 0.0;
            for (size_t i = 0; i < SampleCount; ++i)
            {
                const BSDFSample result = sample(outgoing);
                if (result.get_mode() != ScatteringMode::None)
                    estimate += result.m_value.m_beauty[0] * result.m_incoming.get_value().y
                        / result.get_probability();
            }
            estimate /= SampleCount;
            EXPECT_FEQ_EPS(integral, estimate, 0.005);
        }
    }

    TEST_CASE_F(BlackOrExcludedMaterialsHaveNoScattering, Fixture)
    {
        values.m_diffuse_reflectance.set(0.0f);
        values.m_specular_reflectance.set(0.0f);
        prepare();
        EXPECT_EQ(ScatteringMode::None, sample(Vector3f(0.0f, 1.0f, 0.0f)).get_mode());
    }
}
