
//
// This source file is part of appleseed.
// Visit https://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2017-2018 Esteban Tovagliari, The appleseedhq Organization
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
#include "renderer/kernel/lighting/scatteringmode.h"
#include "renderer/modeling/bsdf/bsdf.h"
#include "renderer/modeling/bsdf/bsdfsample.h"
#include "renderer/modeling/bsdf/glassbsdf.h"
#include "renderer/utility/paramarray.h"

// appleseed.foundation headers.
#include "foundation/math/basis.h"
#include "foundation/math/dual.h"
#include "foundation/math/rng/xoroshiro128plus.h"
#include "foundation/math/vector.h"
#include "foundation/memory/autoreleaseptr.h"
#include "foundation/utility/iostreamop.h"
#include "foundation/utility/test.h"

using namespace foundation;
using namespace renderer;

TEST_SUITE(Renderer_Modeling_BSDF_Glass)
{
    struct Fixture
    {
        auto_release_ptr<BSDF> bsdf;
        GlassBSDFInputValues values{};
        Xoroshiro128plus rng;
        SamplingContext sampling_context;
        BSDF::LocalGeometry geometry;

        Fixture()
          : bsdf(GlassBSDFFactory().create("glass", ParamArray()))
          , sampling_context(rng, SamplingContext::RNGMode)
        {
            values.m_ior = values.m_precomputed.m_outside_ior = 1.5f;
            values.m_roughness = 0.5f;
            values.m_precomputed.m_backfacing = false;
            values.m_precomputed.m_refraction_color.set(0.5f);
            values.m_precomputed.m_refraction_weight = 0.5f;
            geometry.m_shading_point = nullptr;
            geometry.m_geometric_normal = Vector3f(0.0f, 1.0f, 0.0f);
            geometry.m_shading_basis = Basis3f(geometry.m_geometric_normal);
        }
    };

    TEST_CASE_F(MatchingIndices_TransmitBothSidesAndPreserveDifferentials, Fixture)
    {
        for (const bool backfacing : {false, true})
        {
            values.m_precomputed.m_backfacing = backfacing;
            const float sign = backfacing ? -1.0f : 1.0f;
            const Dual3f outgoing(
                sign * Vector3f(0.6f, 0.8f, 0.0f),
                Vector3f(0.01f, -0.0075f, 0.0f),
                Vector3f(0.0f, 0.0f, 0.02f));
            for (const bool adjoint : {false, true})
            {
                BSDFSample sample;
                bsdf->sample(sampling_context, &values, adjoint, false, geometry,
                    outgoing, ScatteringMode::All, sample);
                ASSERT_EQ(ScatteringMode::Specular, sample.get_mode());
                EXPECT_EQ(BSDF::DiracDelta, sample.get_probability());
                EXPECT_FEQ(-outgoing.get_value(), sample.m_incoming.get_value());
                EXPECT_TRUE(sample.m_incoming.has_derivatives());
                EXPECT_FEQ(-outgoing.get_dx(), sample.m_incoming.get_dx());
                EXPECT_FEQ(-outgoing.get_dy(), sample.m_incoming.get_dy());
                EXPECT_FEQ(0.5f / 0.8f, sample.m_value.m_beauty[0]);
                EXPECT_TRUE(sample.m_value.is_valid());
            }
        }
    }

    TEST_CASE_F(MatchingIndices_CosineWeightedTransportPreservesSurfaceTint, Fixture)
    {
        const Dual3f outgoing(Vector3f(0.6f, 0.8f, 0.0f));
        BSDFSample sample;
        // Adjoint weighting uses the same flat normal without requiring a
        // scene-backed shading point for shadow-terminator controls.
        bsdf->sample(sampling_context, &values, true, true, geometry, outgoing,
            ScatteringMode::All, sample);
        ASSERT_EQ(ScatteringMode::Specular, sample.get_mode());
        EXPECT_FEQ(0.5f, sample.m_value.m_beauty[0]);
        EXPECT_FALSE(sample.m_incoming.has_derivatives());
    }

    TEST_CASE_F(MatchingIndices_RespectTheSpecularSamplingMask, Fixture)
    {
        EXPECT_TRUE(ScatteringMode::has_specular(bsdf->get_modes()));
        const Dual3f outgoing(Vector3f(0.0f, 1.0f, 0.0f));
        BSDFSample sample;
        bsdf->sample(sampling_context, &values, false, false, geometry, outgoing,
            ScatteringMode::Glossy, sample);
        EXPECT_EQ(ScatteringMode::None, sample.get_mode());
        bsdf->sample(sampling_context, &values, false, false, geometry, outgoing,
            ScatteringMode::Specular, sample);
        EXPECT_EQ(ScatteringMode::Specular, sample.get_mode());
    }

    TEST_CASE_F(MatchingIndices_RejectZeroTransmissionAndGrazingDirections, Fixture)
    {
        BSDFSample sample;
        bsdf->sample(sampling_context, &values, false, false, geometry,
            Dual3f(Vector3f(1.0f, 0.0f, 0.0f)), ScatteringMode::All, sample);
        EXPECT_EQ(ScatteringMode::None, sample.get_mode());
        values.m_precomputed.m_refraction_weight = 0.0f;
        values.m_precomputed.m_refraction_color.set(0.0f);
        bsdf->sample(sampling_context, &values, false, false, geometry,
            Dual3f(Vector3f(0.0f, 1.0f, 0.0f)), ScatteringMode::All, sample);
        EXPECT_EQ(ScatteringMode::None, sample.get_mode());
    }
}
