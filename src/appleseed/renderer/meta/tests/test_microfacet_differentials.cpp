
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

#include "renderer/kernel/shading/shadingpoint.h"
#include "renderer/kernel/shading/shadingpointbuilder.h"
#include "renderer/modeling/bsdf/bsdf.h"
#include "renderer/modeling/bsdf/bsdfsample.h"
#include "renderer/modeling/bsdf/glassbsdf.h"
#include "renderer/modeling/bsdf/glossybrdf.h"
#include "renderer/modeling/bsdf/glossylayerbsdf.h"
#include "renderer/modeling/bsdf/metalbrdf.h"
#include "renderer/modeling/bsdf/oslmicrofacetbrdf.h"
#include "renderer/modeling/bsdf/plasticbrdf.h"
#include "renderer/utility/paramarray.h"
#include "foundation/math/basis.h"
#include "foundation/math/rng/xoroshiro128plus.h"
#include "foundation/math/vector.h"
#include "foundation/memory/arena.h"
#include "foundation/utility/iostreamop.h"
#include "foundation/utility/test.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

using namespace renderer;
using namespace foundation;

TEST_SUITE(Renderer_Modeling_BSDF_MicrofacetDifferentials)
{
    enum class Material
    {
        Glossy, Metal, Plastic, Coat, GlassReflection, GlassTransmission,
        StandardGGX, StandardBeckmann
    };
    // Opposite, unequal screen directions expose accidental axis reuse.
    constexpr float SecondaryAxisScale = -0.7f;

    struct State
    {
        Vector3f normal = Vector3f(0.2f, 1.0f, 0.1f);
        Vector3f tangent = Vector3f(1.0f, 0.3f, 0.2f);
        Vector3f outgoing = Vector3f(0.3f, 1.0f, -0.1f);
        Vector3f geometric = Vector3f(0.0f, 1.0f, 0.0f);
    };

    Vector3f normalization_derivative(const Vector3f& raw, const Vector3f& slope)
    {
        const Vector3f n = normalize(raw);
        return (slope - n * dot(n, slope)) / norm(raw);
    }

    State shifted(const State& center, const State& slope, const float step)
    {
        State result;
        result.normal = center.normal + step * slope.normal;
        result.tangent = center.tangent + step * slope.tangent;
        result.outgoing = center.outgoing + step * slope.outgoing;
        result.geometric = center.geometric + step * slope.geometric;
        return result;
    }

    BSDFSample sample_direction(const Material material, const State& state,
        const State& slope, const float step, const float roughness, const float anisotropy,
        const std::uint64_t seed, const bool backfacing, const bool adjoint, const bool derivatives)
    {
        const float side = backfacing ? -1.0f : 1.0f;
        const Vector3f n = side * normalize(state.normal);
        const Vector3f wo = side * normalize(state.outgoing);
        const Basis3f basis(n, state.tangent);
        ShadingPoint point;
        ShadingPointBuilder builder(point);
        builder.set_ray(ShadingRay(Vector3d(0.0, side, 0.0), -Vector3d(wo),
            ShadingRay::Time(), VisibilityFlags::CameraRay, 0));
        builder.set_primitive_type(ShadingPoint::PrimitiveTriangle);
        builder.set_geometric_normal(Vector3d(normalize(state.geometric)));
        builder.set_shading_basis(Basis3d(basis));
        builder.set_side(backfacing ? ObjectInstance::BackSide : ObjectInstance::FrontSide);
        BSDF::NormalDerivatives dn, dg;
        dn.m_dx = side * step * normalization_derivative(state.normal, slope.normal);
        dn.m_dy = SecondaryAxisScale * dn.m_dx;
        dg.m_dx = step * normalization_derivative(state.geometric, slope.geometric);
        dg.m_dy = SecondaryAxisScale * dg.m_dx;
        BSDF::BasisReference reference;
        reference.m_tangent = state.tangent;
        reference.m_dx = step * slope.tangent;
        reference.m_dy = SecondaryAxisScale * reference.m_dx;
        BSDF::LocalGeometry geometry;
        geometry.m_shading_point = &point;
        geometry.m_shading_basis = basis;
        geometry.m_geometric_normal = normalize(state.geometric);
        geometry.m_normal_derivatives = &dn;
        geometry.m_basis_reference = &reference;
        geometry.m_geometric_normal_derivatives = &dg;

        auto_release_ptr<BSDF> bsdf;
        GlossyBRDFInputValues glossy{};
        MetalBRDFInputValues metal{};
        PlasticBRDFInputValues plastic{};
        GlossyLayerBSDFInputValues coat{};
        GlassBSDFInputValues glass{};
        OSLMicrofacetBRDFInputValues standard{};
        void* inputs = nullptr;
        switch (material)
        {
          case Material::Glossy:
            bsdf = GlossyBRDFFactory().create("rough_direction", ParamArray());
            glossy.m_reflectance.set(1.0f);
            glossy.m_reflectance_multiplier = 1.0f;
            glossy.m_ior = 1.5f;
            glossy.m_fresnel_weight = 1.0f;
            glossy.m_roughness = roughness;
            glossy.m_anisotropy = anisotropy;
            inputs = &glossy;
            break;
          case Material::Metal:
            bsdf = MetalBRDFFactory().create("rough_direction", ParamArray());
            metal.m_normal_reflectance.set(1.0f);
            metal.m_edge_tint.set(1.0f);
            metal.m_reflectance_multiplier = 1.0f;
            metal.m_roughness = roughness;
            metal.m_anisotropy = anisotropy;
            inputs = &metal;
            break;
          case Material::Plastic:
            bsdf = PlasticBRDFFactory().create("rough_direction", ParamArray());
            plastic.m_diffuse_reflectance.set(0.0f);
            plastic.m_specular_reflectance.set(1.0f);
            plastic.m_specular_reflectance_multiplier = 1.0f;
            plastic.m_roughness = roughness;
            plastic.m_ior = 1.5f;
            inputs = &plastic;
            break;
          case Material::Coat:
            bsdf = GlossyLayerBSDFFactory::create("rough_direction", ParamArray());
            coat.m_reflectance.set(1.0f);
            coat.m_transmittance.set(1.0f);
            coat.m_roughness = roughness;
            coat.m_anisotropy = anisotropy;
            coat.m_ior = 1.5f;
            inputs = &coat;
            break;
          case Material::StandardGGX:
          case Material::StandardBeckmann:
            bsdf = OSLMicrofacetBRDFFactory::create("rough_direction", ParamArray());
            standard.m_distribution = material == Material::StandardGGX ?
                OSLMicrofacetDistribution::GGX : OSLMicrofacetDistribution::Beckmann;
            standard.m_reflectance.set(1.0f);
            // These inputs are raw widths, unlike the perceptual roughness
            // and anisotropy controls used by the other native materials.
            standard.m_alpha_x = roughness;
            standard.m_alpha_y = roughness * (1.0f + anisotropy);
            standard.m_eta = 1.5f;
            inputs = &standard;
            break;
          case Material::GlassReflection:
          case Material::GlassTransmission:
            bsdf = GlassBSDFFactory().create("rough_direction", ParamArray());
            glass.m_precomputed.m_reflection_color.set(0.0f);
            glass.m_precomputed.m_refraction_color.set(0.0f);
            glass.m_roughness = roughness;
            glass.m_anisotropy = anisotropy;
            glass.m_ior = 1.5f;
            // These direction-only fixtures specify their optical media
            // directly, avoiding material or scene state in the reference.
            glass.m_precomputed.m_outside_ior = 1.0f;
            glass.m_precomputed.m_backfacing = backfacing;
            if (material == Material::GlassReflection)
            {
                glass.m_precomputed.m_reflection_weight = 1.0f;
                glass.m_precomputed.m_reflection_color.set(1.0f);
            }
            else
            {
                glass.m_precomputed.m_refraction_weight = 1.0f;
                glass.m_precomputed.m_refraction_color.set(1.0f);
            }
            inputs = &glass;
            break;
        }
        Arena arena;
        if (material != Material::GlassReflection && material != Material::GlassTransmission)
            bsdf->prepare_inputs(arena, point, inputs);
        constexpr std::uint64_t SeedA = 0x3141592653589793ULL;
        constexpr std::uint64_t SeedB = 0x2718281828459045ULL;
        constexpr std::uint64_t SeedStride = 0x9e3779b97f4a7c15ULL;
        Xoroshiro128plus rng(SeedA ^ ((seed + 1) * SeedStride), SeedB);
        SamplingContext sampling(rng, SamplingContext::RNGMode);
        const Vector3f dwo = side * step * normalization_derivative(state.outgoing, slope.outgoing);
        const Dual3f outgoing = derivatives ? Dual3f(wo, wo + dwo, wo + SecondaryAxisScale * dwo) : Dual3f(wo);
        BSDFSample result;
        bsdf->sample(sampling, inputs, adjoint, false, geometry, outgoing, ScatteringMode::All, result);
        return result;
    }

    struct Fixture
    {
        void check(const Material material, ITestListener& test_listener,
            TestResult& case_result, const char* test_name)
        {
            const auto get_name = [test_name]() { return test_name; };
            // A doubled step resolves the float-valued scalar reference's
            // cancellation near invariant isotropic tangent rotations.
            const float Step = 0.002f;
            // Float-valued directions quantize finite differences by roughly
            // one output ULP divided by Step, including at small gradients.
            const float AbsoluteTolerance = 0.0003f;
            const float RelativeTolerance = 0.004f;
            std::size_t comparisons = 0;
            std::size_t corrected = 0;
            const bool glass = material == Material::GlassReflection || material == Material::GlassTransmission;
            for (const float roughness : {0.1f, 0.3f, 0.6f})
            for (const float anisotropy : {-0.5f, 0.0f, 0.5f})
            for (const bool adjoint : {false, true})
            for (const bool backfacing : {false, true})
            {
                if ((backfacing && !glass) || (anisotropy != 0.0f && material == Material::Plastic))
                    continue;
                for (unsigned experiment = 0; experiment < 4; ++experiment)
                {
                    std::size_t group_comparisons = 0;
                    for (std::uint64_t seed = 0; seed < 8; ++seed)
                    {
                        State center, slope;
                        slope.normal = experiment == 1 || experiment == 3 ? Vector3f(0.3f, 0.1f, -0.2f) : Vector3f(0.0f);
                        slope.tangent = experiment >= 2 ? Vector3f(0.2f, 0.1f, -0.3f) : Vector3f(0.0f);
                        slope.outgoing = experiment == 0 || experiment == 3 ? Vector3f(-0.2f, 0.1f, 0.3f) : Vector3f(0.0f);
                        slope.geometric = experiment == 3 ? Vector3f(0.1f, -0.2f, 0.1f) : Vector3f(0.0f);
                        if (experiment == 3 && !glass && material != Material::Plastic)
                            center.geometric = Vector3f(0.9f, 0.3f, -0.1f);
                        const auto value = sample_direction(material, center, slope, Step, roughness,
                            anisotropy, seed, backfacing, adjoint, true);
                        const auto left = sample_direction(material, shifted(center, slope, -Step), slope,
                            Step, roughness, anisotropy, seed, backfacing, adjoint, false);
                        const auto right = sample_direction(material, shifted(center, slope, Step), slope,
                            Step, roughness, anisotropy, seed, backfacing, adjoint, false);
                        if (value.get_mode() == ScatteringMode::None || left.get_mode() == ScatteringMode::None ||
                            right.get_mode() == ScatteringMode::None)
                            continue;
                        ASSERT_TRUE(value.m_incoming.has_derivatives());
                        const Vector3f finite = (right.m_incoming.get_value() - left.m_incoming.get_value()) / (2.0f * Step);
                        const Vector3f predicted = (value.m_incoming.get_dx() - value.m_incoming.get_value()) / Step;
                        const Vector3f finite_y = SecondaryAxisScale * finite;
                        const Vector3f predicted_y = (value.m_incoming.get_dy() - value.m_incoming.get_value()) / Step;
                        const float error = norm(predicted - finite);
                        const float error_y = norm(predicted_y - finite_y);
                        const float bound = AbsoluteTolerance + RelativeTolerance * norm(finite);
                        const float bound_y = AbsoluteTolerance + RelativeTolerance * norm(finite_y);
                        if (error >= bound || error_y >= bound_y)
                        {
                            TestListenerHelper::write(test_listener, current_test_suite__(), get_name(),
                                __FILE__, __LINE__, TestMessage::AssertionFailure,
                                "roughness=%g anisotropy=%g adjoint=%d backfacing=%d field=%u seed=%llu error=%g/%g bound=%g/%g",
                                roughness, anisotropy, adjoint, backfacing, experiment,
                                static_cast<unsigned long long>(seed), error, error_y, bound, bound_y);
                        }
                        EXPECT_LT(bound, error);
                        EXPECT_LT(bound_y, error_y);
                        const auto scalar = sample_direction(material, center, slope, Step, roughness,
                            anisotropy, seed, backfacing, adjoint, false);
                        ASSERT_EQ(value.get_mode(), scalar.get_mode());
                        EXPECT_FEQ_EPS(scalar.m_incoming.get_value(), value.m_incoming.get_value(), 1.0e-6f);
                        EXPECT_FEQ_EPS(scalar.get_probability(), value.get_probability(), 1.0e-6f * std::max(1.0f, scalar.get_probability()));
                        EXPECT_FEQ_EPS(scalar.m_value.m_beauty[0], value.m_value.m_beauty[0],
                            1.0e-6f * std::max(1.0f, scalar.m_value.m_beauty[0]));
                        ++comparisons;
                        ++group_comparisons;
                        if (experiment == 3 && dot(normalize(center.geometric), value.m_incoming.get_value()) < 0.001f)
                            ++corrected;
                    }
                    EXPECT_TRUE(group_comparisons > 0);
                }
            }
            EXPECT_TRUE(comparisons > 100);
            if (material == Material::Glossy || material == Material::Metal || material == Material::Coat)
                EXPECT_TRUE(corrected > 0);
        }
    };

    TEST_CASE(IsotropicViewAlignedSamplingIsInvariantUnderTangentChanges)
    {
        State center, slope;
        slope.normal = slope.outgoing = slope.geometric = Vector3f(0.0f);
        slope.tangent = Vector3f(0.2f, 0.1f, -0.3f);
        const float DirectionTolerance = 4.0f * std::numeric_limits<float>::epsilon();
        for (const float step : {0.002f, 0.004f})
        {
            const auto value = sample_direction(Material::Glossy, center, slope, step,
                0.6f, 0.0f, 4, false, false, true);
            const auto left = sample_direction(Material::Glossy, shifted(center, slope, -step), slope,
                step, 0.6f, 0.0f, 4, false, false, false);
            const auto right = sample_direction(Material::Glossy, shifted(center, slope, step), slope,
                step, 0.6f, 0.0f, 4, false, false, false);
            ASSERT_EQ(ScatteringMode::Glossy, value.get_mode());
            ASSERT_EQ(ScatteringMode::Glossy, left.get_mode());
            ASSERT_EQ(ScatteringMode::Glossy, right.get_mode());
            EXPECT_LT(DirectionTolerance, norm(right.m_incoming.get_value() - left.m_incoming.get_value()));
            EXPECT_LT(DirectionTolerance, norm(value.m_incoming.get_dx() - value.m_incoming.get_value()));
        }
    }

    TEST_CASE_F(StandardGGXReflection, Fixture) { check(Material::StandardGGX, test_listener, case_result, get_name()); }
    TEST_CASE_F(StandardBeckmannReflection, Fixture) { check(Material::StandardBeckmann, test_listener, case_result, get_name()); }
    TEST_CASE_F(GlossyReflection, Fixture) { check(Material::Glossy, test_listener, case_result, get_name()); }
    TEST_CASE_F(MetalReflection, Fixture) { check(Material::Metal, test_listener, case_result, get_name()); }
    TEST_CASE_F(PlasticReflection, Fixture) { check(Material::Plastic, test_listener, case_result, get_name()); }
    TEST_CASE_F(CoatReflection, Fixture) { check(Material::Coat, test_listener, case_result, get_name()); }
    TEST_CASE_F(GlassReflection, Fixture) { check(Material::GlassReflection, test_listener, case_result, get_name()); }
    TEST_CASE_F(GlassTransmission, Fixture) { check(Material::GlassTransmission, test_listener, case_result, get_name()); }
}
