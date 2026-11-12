
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
#include "renderer/kernel/shading/closures.h"
#include "renderer/kernel/shading/shadingpoint.h"
#include "renderer/modeling/bsdf/bsdf.h"
#include "renderer/modeling/bsdf/bsdfsample.h"
#include "renderer/modeling/bsdf/glossylayerbsdf.h"
#include "renderer/utility/paramarray.h"

// appleseed.foundation headers.
#include "foundation/math/rng/xoroshiro128plus.h"
#include "foundation/memory/arena.h"
#include "foundation/memory/autoreleaseptr.h"
#include "foundation/utility/test.h"

// Standard headers.
#include <cmath>

using namespace foundation;
using namespace renderer;

TEST_SUITE(Renderer_Modeling_BSDF_GlossyLayer)
{
    double fresnel(const double cosine, const double ior)
    {
        const double ct = std::sqrt(1.0 - (1.0 - cosine*cosine) / (ior*ior));
        const double rs = (cosine - ior*ct) / (cosine + ior*ct);
        const double rp = (ior*cosine - ct) / (ior*cosine + ct);
        return (rs*rs + rp*rp) * 0.5;
    }

    struct Fixture
    {
        auto_release_ptr<BSDF> bsdf;
        GlossyLayerBSDFInputValues values{};
        Basis3f basis;
        ShadingPoint point;
        BSDF::LocalGeometry geometry;
        Xoroshiro128plus rng;
        SamplingContext sampling;

        Fixture()
          : bsdf(GlossyLayerBSDFFactory::create("coat", ParamArray()))
          , basis(Vector3f(0.0f, 1.0f, 0.0f))
          , sampling(rng, SamplingContext::RNGMode)
        {
            values.m_reflectance.set(1.0f);
            values.m_roughness = 0.5f;
            values.m_ior = 1.5f;
            values.m_transmittance[0] = 0.25f;
            values.m_transmittance[1] = 0.5f;
            values.m_transmittance[2] = 0.8f;
            values.m_thickness = 0.4f;
            point.set_ray(ShadingRay(Vector3d(0.0, 1.0, 0.0), Vector3d(0.0, -1.0, 0.0),
                ShadingRay::Time::create_with_normalized_time(0.0f, 0.0f, 1.0f),
                VisibilityFlags::CameraRay, 0));
            geometry.m_shading_point = &point;
            geometry.m_geometric_normal = basis.get_normal();
            geometry.m_shading_basis = basis;
            prepare();
        }

        void prepare()
        {
            Arena arena;
            bsdf->prepare_inputs(arena, point, &values);
        }
    };

    TEST_CASE(MatchedIndicesHaveZeroDirectionalAndAverageReflectance)
    {
        for (const float roughness : {0.0f, 0.2f, 0.8f})
        {
            for (const float cosine : {0.0f, 0.1f, 0.6f, 1.0f})
                EXPECT_EQ(0.0f, get_dielectric_layer_directional_albedo(1.0f, roughness, cosine));
            EXPECT_EQ(0.0f, get_dielectric_layer_average_albedo(1.0f, roughness));
        }
    }

    TEST_CASE_F(MatchedNonAirIndicesHaveNoInterfaceLoss, Fixture)
    {
        values.m_precomputed.m_outside_ior = values.m_ior;
        values.m_thickness = 0.0f;
        Spectrum value;
        value.set(1.0f);
        bsdf->attenuate_substrate(&values, basis, basis.get_normal(), basis.get_normal(), value);
        EXPECT_EQ(1.0f, value[0]);
        value.set(1.0f);
        bsdf->attenuate_emission(&values, basis, basis.get_normal(), value);
        EXPECT_EQ(1.0f, value[0]);
    }

    TEST_CASE_F(MatchedIndexSubstrateObeysTwoPassBeerAttenuation, Fixture)
    {
        values.m_ior = 1.0f;
        const Vector3f outgoing(0.6f, 0.8f, 0.0f), incoming(0.8f, 0.6f, 0.0f);
        Spectrum value;
        value.set(1.0f);
        bsdf->attenuate_substrate(&values, basis, outgoing, incoming, value);
        for (size_t channel = 0; channel < 3; ++channel)
            EXPECT_FEQ_EPS(std::pow(values.m_transmittance[channel],
                values.m_thickness * (1.0f / outgoing.y + 1.0f / incoming.y)), value[channel], 1.0e-6f);
    }

    TEST_CASE_F(MatchedIndexEmissionObeysOnePassBeerAttenuation, Fixture)
    {
        values.m_ior = 1.0f;
        const Vector3f outgoing(0.6f, 0.8f, 0.0f);
        Spectrum value;
        value.set(1.0f);
        bsdf->attenuate_emission(&values, basis, outgoing, value);
        for (size_t channel = 0; channel < 3; ++channel)
            EXPECT_FEQ_EPS(std::pow(values.m_transmittance[channel],
                values.m_thickness / outgoing.y), value[channel], 1.0e-6f);
    }

    TEST_CASE_F(ZeroTransmittanceDoesNotLeakOrCreateNonfiniteValues, Fixture)
    {
        values.m_ior = 1.0f;
        values.m_thickness = 0.05f;
        values.m_transmittance[0] = 0.0f;
        Spectrum value;
        value.set(1.0f);
        bsdf->attenuate_emission(&values, basis, basis.get_normal(), value);
        EXPECT_EQ(0.0f, value[0]);
        EXPECT_TRUE(std::isfinite(value[1]));
        EXPECT_TRUE(std::isfinite(value[2]));
    }

    TEST_CASE_F(SmoothEmissionUsesExactFresnelTransmission, Fixture)
    {
        values.m_roughness = 0.0f;
        values.m_thickness = 0.0f;
        for (const float cosine : {0.1f, 0.6f, 1.0f})
        {
            Spectrum value;
            value.set(1.0f);
            const Vector3f outgoing(std::sqrt(1.0f - cosine*cosine), cosine, 0.0f);
            bsdf->attenuate_emission(&values, basis, outgoing, value);
            EXPECT_FEQ_EPS(float(1.0 - fresnel(cosine, 1.5)), value[0], 1.0e-6f);
        }
    }

    TEST_CASE_F(SmoothReflectionIsAnExactSpecularEvent, Fixture)
    {
        values.m_roughness = 0.0f;
        prepare();
        const Vector3f outgoing(0.6f, 0.8f, 0.0f);
        BSDFSample result;
        bsdf->sample(sampling, &values, false, false, geometry, Dual3f(outgoing),
            ScatteringMode::Specular, result);
        ASSERT_EQ(ScatteringMode::Specular, result.get_mode());
        EXPECT_EQ(BSDF::DiracDelta, result.get_probability());
        EXPECT_FEQ_EPS(-0.6f, result.m_incoming.get_value().x, 1.0e-6f);
        EXPECT_FEQ_EPS(0.8f, result.m_incoming.get_value().y, 1.0e-6f);
        EXPECT_FEQ_EPS(float(fresnel(0.8, 1.5)), result.m_value.m_beauty[0] * 0.8f, 1.0e-6f);
        EXPECT_EQ(0.0f, bsdf->evaluate_pdf(&values, false, geometry,
            outgoing, result.m_incoming.get_value(), ScatteringMode::All));
    }

    TEST_CASE_F(RoughReflectionSamplesMatchEvaluation, Fixture)
    {
        prepare();
        const Vector3f outgoing(0.6f, 0.8f, 0.0f);
        size_t accepted = 0;
        for (size_t i = 0; i < 64; ++i)
        {
            BSDFSample result;
            bsdf->sample(sampling, &values, false, false, geometry, Dual3f(outgoing),
                ScatteringMode::Glossy, result);
            if (result.get_mode() == ScatteringMode::None)
                continue;
            ++accepted;
            DirectShadingComponents evaluated;
            const float pdf = bsdf->evaluate(&values, false, false, geometry,
                outgoing, result.m_incoming.get_value(), ScatteringMode::Glossy, evaluated);
            EXPECT_FEQ_EPS(pdf, result.get_probability(), 1.0e-5f);
            EXPECT_FEQ_EPS(evaluated.m_beauty[0], result.m_value.m_beauty[0], 1.0e-5f);
        }
        EXPECT_TRUE(accepted > 0);
    }

    TEST_CASE(LayerDepthLimitThrowsWithoutChangingTheStack)
    {
        CompositeClosure::SmallClosureLayerIDStack stack;
        for (size_t i = 0; i < CompositeClosure::MaxClosureLayers; ++i)
            stack.push(static_cast<std::int8_t>(i));
        EXPECT_EQ(CompositeClosure::MaxClosureLayers, stack.size());
        EXPECT_EXCEPTION(ExceptionOSLRuntimeError, stack.push(0));
        EXPECT_EQ(CompositeClosure::MaxClosureLayers, stack.size());
    }
}
