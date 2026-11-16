
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

#include "renderer/kernel/shading/shadingpoint.h"
#include "renderer/kernel/shading/shadingpointbuilder.h"
#include "renderer/modeling/bsdf/bsdf.h"
#include "renderer/modeling/bsdf/bsdfsample.h"
#include "renderer/modeling/bsdf/orennayarbrdf.h"
#include "renderer/utility/paramarray.h"
#include "foundation/math/rng/xoroshiro128plus.h"
#include "foundation/utility/test.h"

#include <cmath>
#include <utility>

using namespace foundation;
using namespace renderer;

TEST_SUITE(Renderer_Modeling_BSDF_OrenNayar)
{
    struct Fixture
    {
        auto_release_ptr<BSDF> leaf;
        OrenNayarBRDFInputValues values;
        ShadingPoint point;
        BSDF::LocalGeometry geometry;
        Xoroshiro128plus rng;
        SamplingContext sampling;

        Fixture()
          : leaf(OrenNayarBRDFFactory().create("pole_diffuse", ParamArray()))
          , sampling(rng, SamplingContext::RNGMode)
        {
            values.m_reflectance.set(0.5f);
            values.m_reflectance_multiplier = 1.0f;
            values.m_roughness = 0.1f;
            ShadingPointBuilder builder(point);
            builder.set_ray(ShadingRay(Vector3d(0.0, 1.0, 0.0), Vector3d(0.0, -1.0, 0.0),
                ShadingRay::Time(), VisibilityFlags::CameraRay, 0));
            builder.set_primitive_type(ShadingPoint::PrimitiveTriangle);
            geometry.m_shading_point = &point;
            set_normal(Vector3f(0.0f, 1.0f, 0.0f));
        }

        void set_normal(const Vector3f& normal)
        {
            ShadingPointBuilder builder(point);
            builder.set_geometric_normal(Vector3d(normal));
            builder.set_shading_basis(Basis3d(Vector3d(normal)));
            geometry.m_geometric_normal = normal;
            geometry.m_shading_basis = Basis3f(normal);
        }

        float pole_value() const
        {
            const float sigma_squared = square(values.m_roughness);
            const float direct = 1.0f - 0.5f * sigma_squared / (sigma_squared + 0.33f);
            // At either pole beta is zero, eliminating all azimuth terms.
            const float indirect = 0.17f * sigma_squared / (sigma_squared + 0.13f);
            return RcpPi<float>() * (values.m_reflectance[0] * direct +
                square(values.m_reflectance[0]) * indirect);
        }
    };

    TEST_CASE_F(NormalIncidenceHasFiniteAnalyticLimitOnBothDirections, Fixture)
    {
        const float Tolerance = 2.0e-6f;
        for (const Vector3f normal : {Vector3f(0.0f, 1.0f, 0.0f),
            normalize(Vector3f(0.3f, 1.0f, 0.2f)), normalize(Vector3f(-0.4f, 0.7f, 0.8f))})
        {
            set_normal(normal);
            const Vector3f oblique = normalize(normal + 0.4f * geometry.m_shading_basis.get_tangent_u());
            const std::pair<Vector3f, Vector3f> directions[] = {
                {normal, normal}, {normal, oblique}, {oblique, normal}};
            for (const float sigma : {0.1f, 0.5f, 1.0f})
            {
                values.m_roughness = sigma;
                for (const auto& pair : directions)
                {
                    DirectShadingComponents value;
                    const float pdf = leaf->evaluate(&values, false, false, geometry,
                        pair.first, pair.second, ScatteringMode::All, value);
                    EXPECT_FEQ_EPS(std::abs(dot(pair.second, normal)) * RcpPi<float>(), pdf, Tolerance);
                    EXPECT_TRUE(std::isfinite(value.m_beauty[0]));
                    EXPECT_FEQ_EPS(pole_value(), value.m_beauty[0], Tolerance);
                }
            }
        }
    }

    TEST_CASE_F(SampledNormalIncidenceMatchesTheFiniteEvaluatedLimit, Fixture)
    {
        const float Tolerance = 2.0e-6f;
        const size_t SampleCount = 32;
        for (const float sigma : {0.1f, 0.5f, 1.0f})
        {
            values.m_roughness = sigma;
            for (size_t sample_index = 0; sample_index < SampleCount; ++sample_index)
            {
                BSDFSample result;
                leaf->sample(sampling, &values, false, false, geometry,
                    Dual3f(geometry.m_shading_basis.get_normal()), ScatteringMode::All, result);
                ASSERT_EQ(ScatteringMode::Diffuse, result.get_mode());
                EXPECT_TRUE(std::isfinite(result.m_value.m_beauty[0]));
                EXPECT_FEQ_EPS(pole_value(), result.m_value.m_beauty[0], Tolerance);
                DirectShadingComponents evaluated;
                const float pdf = leaf->evaluate(&values, false, false, geometry,
                    geometry.m_shading_basis.get_normal(), result.m_incoming.get_value(), ScatteringMode::All, evaluated);
                EXPECT_FEQ_EPS(pdf, result.get_probability(), Tolerance);
                EXPECT_FEQ_EPS(evaluated.m_beauty[0], result.m_value.m_beauty[0], Tolerance);
            }
        }
    }
}
