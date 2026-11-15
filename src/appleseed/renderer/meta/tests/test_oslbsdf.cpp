
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
#include "renderer/kernel/rendering/rendererservices.h"
#include "renderer/kernel/shading/closures.h"
#include "renderer/kernel/shading/oslshadingsystem.h"
#include "renderer/kernel/shading/shadingpoint.h"
#include "renderer/kernel/shading/shadingpointbuilder.h"
#include "renderer/kernel/texturing/oiiotexturesystem.h"
#include "renderer/modeling/bsdf/bsdf.h"
#include "renderer/modeling/bsdf/bsdfsample.h"
#include "renderer/modeling/bsdf/oslbsdf.h"
#include "renderer/modeling/bsdf/metalbrdf.h"
#include "renderer/modeling/bsdf/glossybrdf.h"
#include "renderer/utility/paramarray.h"
#include "renderer/modeling/project/project.h"

// appleseed.foundation headers.
#include "foundation/math/basis.h"
#include "foundation/math/rng/xoroshiro128plus.h"
#include "foundation/math/scalar.h"
#include "foundation/math/vector.h"
#include "foundation/memory/arena.h"
#include "foundation/memory/autoreleaseptr.h"
#include "foundation/utility/iostreamop.h"
#include "foundation/utility/test.h"

// OSL headers.
#include "foundation/platform/_beginoslheaders.h"
#include "OSL/oslclosure.h"
#include "OSL/genclosure.h"
#include "foundation/platform/_endoslheaders.h"

// Standard headers.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

using namespace foundation;
using namespace renderer;

TEST_SUITE(Renderer_Modeling_BSDF_OSL)
{
    struct Fixture
    {
        auto_release_ptr<Project> project;
        std::shared_ptr<OIIOTextureSystem> textures;
        RendererServices services;
        auto_release_ptr<OSLShadingSystem> system;
        auto_release_ptr<BSDF> bsdf;
        Arena arena;
        Basis3f basis;
        const OSL::ClosureColor* tree;
        ShadingPoint point;
        BSDF::LocalGeometry geometry;
        Xoroshiro128plus rng;
        SamplingContext sampling;

        Fixture()
          : project(ProjectFactory::create("mixture_test"))
          , textures(OIIOTextureSystemFactory::create())
          , services(*project, *textures)
          , system(OSLShadingSystemFactory::create(&services, textures.get()))
          , bsdf(OSLBSDFFactory().create())
          , basis(Vector3f(0.0f, 1.0f, 0.0f))
          , tree(nullptr)
          , sampling(rng, SamplingContext::RNGMode)
        {
            register_closures(*system);
            ShadingPointBuilder builder(point);
            builder.set_ray(ShadingRay(Vector3d(0.0, 1.0, 0.0),
                Vector3d(0.0, -1.0, 0.0), ShadingRay::Time(), VisibilityFlags::CameraRay, 0));
            builder.set_primitive_type(ShadingPoint::PrimitiveTriangle);
            builder.set_geometric_normal(Vector3d(0.0, 1.0, 0.0));
            builder.set_shading_basis(Basis3d(basis));
            geometry.m_shading_point = &point;
            geometry.m_geometric_normal = basis.get_normal();
            geometry.m_shading_basis = basis;
        }

        OSL::ClosureComponent* add_closure(
            const ClosureID     id,
            const float         weight,
            const Vector3f&     normal,
            const float         ior = 0.0f)
        {
            const OSL::ClosureParam* params = nullptr;
            int closure_id = id;
            if (!system->query_closure(nullptr, &closure_id, &params))
                throw Exception("test closure is not registered");
            const OSL::ClosureParam* finish = params;
            while (finish->type != OSL::TypeDesc())
                ++finish;
            // Keyword additions can grow a registered closure's parameter
            // layout. Allocate the actual layout instead of guessing its size.
            auto* node = static_cast<OSL::ClosureComponent*>(
                arena.allocate(sizeof(OSL::ClosureComponent) + finish->offset));
            new (node) OSL::ClosureComponent();
            node->id = id;
            node->w = OSL::Vec3(weight);
            std::memset(node->data(), 0, finish->offset);
            const OSL::Vec3 n(normal.x, normal.y, normal.z);
            std::memcpy(static_cast<char*>(node->data()) + params[0].offset, &n, sizeof(n));
            if (id == ReflectionID)
                std::memcpy(static_cast<char*>(node->data()) + params[1].offset, &ior, sizeof(ior));
            if (tree)
            {
                auto* sum = arena.allocate<OSL::ClosureAdd>();
                sum->id = OSL::ClosureColor::ADD;
                sum->closureA = tree;
                sum->closureB = node;
                tree = sum;
            }
            else tree = node;
            return node;
        }

        template <typename T>
        void set_keyword(OSL::ClosureComponent* node, const char* key, const T& value)
        {
            const OSL::ClosureParam* params = nullptr;
            if (!system->query_closure(nullptr, &node->id, &params))
                throw Exception("test closure is not registered");
            for (; params->type != OSL::TypeDesc(); ++params)
            {
                if (params->key && std::strcmp(params->key, key) == 0)
                {
                    if (params->field_size != sizeof(T))
                        throw Exception("test closure keyword has an unexpected size");
                    std::memcpy(static_cast<char*>(node->data()) + params->offset, &value, sizeof(T));
                    return;
                }
            }
            throw Exception("test closure keyword is not registered");
        }

        void add_diffuse(const float weight, const Vector3f& normal)
        {
            add_closure(DiffuseID, weight, normal);
        }

        void add_mirror(const float weight, const Vector3f& normal)
        {
            add_closure(ReflectionID, weight, normal);
        }

        BSDFSample sample(const int modes = ScatteringMode::All)
        {
            Arena inputs;
            CompositeSurfaceClosure closure(basis, tree, inputs);
            bsdf->prepare_inputs(inputs, point, &closure);
            BSDFSample result;
            bsdf->sample(sampling, &closure, false, false, geometry,
                Dual3f(basis.get_normal()), modes, result);
            return result;
        }
    };

    TEST_CASE_F(StandardReflection_UsesFresnelForExplicitIorAndPreservesTheMirrorDefault, Fixture)
    {
        const float EnergyTolerance = 1.0e-6f;
        auto mirror = GlossyBRDFFactory().create("mirror_energy", ParamArray());
        for (const float ior : {0.0f, 0.5f, 1.0f, 1.5f, 2.0f})
        {
            tree = nullptr;
            add_closure(ReflectionID, 1.0f, basis.get_normal(), ior);
            Arena inputs;
            CompositeSurfaceClosure closure(basis, tree, inputs);
            // OSLBSDF::evaluate_inputs prepares each converted native leaf.
            mirror->prepare_inputs(inputs, point, closure.get_closure_input_values(0));
            for (const float cosine : {1.0f, 0.6f})
            {
                float expected = 1.0f;
                if (ior > 0.0f)
                {
                    const float sine_transmitted_squared = (1.0f - cosine * cosine) / (ior * ior);
                    if (sine_transmitted_squared < 1.0f)
                    {
                        const float transmitted_cosine = std::sqrt(1.0f - sine_transmitted_squared);
                        const float perpendicular = (cosine - ior * transmitted_cosine) /
                            (cosine + ior * transmitted_cosine);
                        const float parallel = (ior * cosine - transmitted_cosine) /
                            (ior * cosine + transmitted_cosine);
                        expected = 0.5f * (perpendicular * perpendicular + parallel * parallel);
                    }
                }
                const Vector3f outgoing(std::sqrt(1.0f - cosine * cosine), cosine, 0.0f);
                for (const bool adjoint : {false, true})
                {
                    BSDFSample result;
                    bsdf->sample(sampling, &closure, adjoint, false, geometry,
                        Dual3f(outgoing), ScatteringMode::All, result);
                    ASSERT_EQ(ScatteringMode::Specular, result.get_mode());
                    EXPECT_FEQ_EPS(Vector3f(-outgoing.x, outgoing.y, 0.0f),
                        result.m_incoming.get_value(), EnergyTolerance);
                    EXPECT_FEQ_EPS(expected, result.m_value.m_beauty[0] * cosine, EnergyTolerance);
                }
            }
        }
    }

    TEST_CASE_F(StandardReflection_PropagatesTheSelectedNormalFootprint, Fixture)
    {
        const float DirectionTolerance = 2.0e-6f;
        const Vector3f raw_normal(0.3f, 1.0f, -0.1f);
        const Vector3f dx(0.0004f, 0.0001f, -0.0002f);
        const Vector3f dy(-0.0003f, 0.0002f, 0.0005f);
        const Vector3f outgoing = normalize(Vector3f(0.1f, 1.0f, 0.2f));
        const Vector3f outgoing_dx(0.0001f, -0.00002f, -0.0001f);
        const Vector3f outgoing_dy(-0.0001f, 0.00003f, 0.0002f);
        auto mirror = GlossyBRDFFactory().create("mirror_footprint", ParamArray());
        const auto reflected = [](const Vector3f& n, const Vector3f& wo)
        {
            const Vector3f unit_normal = normalize(n);
            return 2.0f * dot(wo, unit_normal) * unit_normal - wo;
        };
        const Vector3f center = reflected(raw_normal, outgoing);
        const Vector3f expected_dx = center + 0.5f *
            (reflected(raw_normal + dx, outgoing + outgoing_dx) -
             reflected(raw_normal - dx, outgoing - outgoing_dx));
        const Vector3f expected_dy = center + 0.5f *
            (reflected(raw_normal + dy, outgoing + outgoing_dy) -
             reflected(raw_normal - dy, outgoing - outgoing_dy));
        for (const float scale : {1.0f, 3.0f})
        {
            for (const float ior : {0.0f, 1.5f})
            {
                tree = nullptr;
                auto* node = add_closure(ReflectionID, 1.0f, scale * raw_normal, ior);
                set_keyword(node, "normal_derivatives", 1);
                set_keyword(node, "dNdx", OSL::Vec3(scale * dx.x, scale * dx.y, scale * dx.z));
                set_keyword(node, "dNdy", OSL::Vec3(scale * dy.x, scale * dy.y, scale * dy.z));
                Arena inputs;
                CompositeSurfaceClosure closure(basis, tree, inputs);
                ASSERT_TRUE(closure.get_closure_normal_derivatives(0) != nullptr);
                mirror->prepare_inputs(inputs, point, closure.get_closure_input_values(0));
                for (const bool adjoint : {false, true})
                {
                    BSDFSample result;
                    bsdf->sample(sampling, &closure, adjoint, false, geometry,
                        Dual3f(outgoing, outgoing + outgoing_dx, outgoing + outgoing_dy),
                        ScatteringMode::All, result);
                    ASSERT_EQ(ScatteringMode::Specular, result.get_mode());
                    ASSERT_TRUE(result.m_incoming.has_derivatives());
                    EXPECT_FEQ_EPS(center, result.m_incoming.get_value(), DirectionTolerance);
                    EXPECT_FEQ_EPS(expected_dx, result.m_incoming.get_dx(), DirectionTolerance);
                    EXPECT_FEQ_EPS(expected_dy, result.m_incoming.get_dy(), DirectionTolerance);
                }
            }
        }
    }

    TEST_CASE_F(SelectedTangentDerivativesFollowTheFrameFallback, Fixture)
    {
        const Vector3f tangent_dx(0.2f, 0.1f, -0.3f);
        const Vector3f tangent_dy(-0.1f, 0.4f, 0.2f);
        for (const Vector3f normal : {Vector3f(0.0f), Vector3f(0.2f, 1.0f, 0.1f)})
        for (const Vector3f tangent : {Vector3f(0.0f), Vector3f(1.0f, 0.3f, 0.2f)})
        {
            tree = nullptr;
            auto* node = add_closure(GlossyID, 1.0f, normal);
            const OSL::ClosureParam* params = nullptr;
            ASSERT_TRUE(system->query_closure(nullptr, &node->id, &params));
            // The glossy closure's second formal parameter is its tangent.
            ASSERT_EQ(sizeof(OSL::Vec3), params[1].field_size);
            const OSL::Vec3 raw_tangent(tangent.x, tangent.y, tangent.z);
            std::memcpy(static_cast<char*>(node->data()) + params[1].offset,
                &raw_tangent, sizeof(raw_tangent));
            set_keyword(node, "normal_derivatives", 1);
            set_keyword(node, "dTdx", OSL::Vec3(tangent_dx.x, tangent_dx.y, tangent_dx.z));
            set_keyword(node, "dTdy", OSL::Vec3(tangent_dy.x, tangent_dy.y, tangent_dy.z));
            Arena inputs;
            CompositeSurfaceClosure closure(basis, tree, inputs);
            const auto& reference = closure.get_closure_basis_reference(0);
            const bool selected = square_norm(normal) != 0.0f && square_norm(tangent) != 0.0f;
            EXPECT_EQ(selected ? tangent : basis.get_tangent_u(), reference.m_tangent);
            EXPECT_EQ(selected ? tangent_dx : Vector3f(0.0f), reference.m_dx);
            EXPECT_EQ(selected ? tangent_dy : Vector3f(0.0f), reference.m_dy);
        }
    }

    TEST_CASE_F(SurfaceTangentDerivativesRespectTheReferenceSource, Fixture)
    {
        const Vector3f surface_dx(0.7f, 0.8f, 0.9f), surface_dy(-0.5f, 0.4f, 0.3f);
        const OSL::Vec3 explicit_dx(0.1f, 0.2f, 0.3f), explicit_dy(-0.2f, 0.3f, 0.4f);
        for (const bool valid_normal : {false, true})
        for (const bool valid_tangent : {false, true})
        {
            tree = nullptr;
            auto* node = add_closure(GlossyID, 1.0f, valid_normal ? basis.get_normal() : Vector3f(0.0f));
            const OSL::ClosureParam* params = nullptr;
            ASSERT_TRUE(system->query_closure(nullptr, &node->id, &params));
            const Vector3f tangent = valid_tangent ? basis.get_tangent_u() : Vector3f(0.0f);
            const OSL::Vec3 raw_tangent(tangent.x, tangent.y, tangent.z);
            std::memcpy(static_cast<char*>(node->data()) + params[1].offset, &raw_tangent, sizeof(raw_tangent));
            set_keyword(node, "normal_derivatives", 1);
            set_keyword(node, "dTdx", explicit_dx);
            set_keyword(node, "dTdy", explicit_dy);
            Arena inputs;
            CompositeSurfaceClosure closure(basis, tree, inputs);
            closure.set_surface_tangent_derivatives(surface_dx, surface_dy);
            const auto& reference = closure.get_closure_basis_reference(0);
            // Equal tangent values do not imply equal derivative sources.
            const bool explicit_reference = valid_normal && valid_tangent;
            EXPECT_EQ(explicit_reference ? Vector3f(explicit_dx) : surface_dx, reference.m_dx);
            EXPECT_EQ(explicit_reference ? Vector3f(explicit_dy) : surface_dy, reference.m_dy);
        }
    }

    TEST_CASE_F(StandardReflection_LeavesUnspecifiedNormalDerivativesOnTheLegacyPath, Fixture)
    {
        add_mirror(1.0f, basis.get_normal());
        Arena inputs;
        CompositeSurfaceClosure closure(basis, tree, inputs);
        EXPECT_TRUE(closure.get_closure_normal_derivatives(0) == nullptr);
    }

    TEST_CASE_F(ReflectiveAndEmptyClosuresHaveNoInterior, Fixture)
    {
        Arena inputs;
        CompositeSurfaceClosure empty(basis, nullptr, inputs);
        EXPECT_FALSE(bsdf->has_interior(&empty));
        EXPECT_FALSE(bsdf->has_transmission(&empty));
        add_diffuse(0.5f, basis.get_normal());
        add_mirror(0.5f, basis.get_normal());
        CompositeSurfaceClosure reflective(basis, tree, inputs);
        EXPECT_FALSE(bsdf->has_interior(&reflective));
        EXPECT_FALSE(bsdf->has_transmission(&reflective));
    }

    TEST_CASE_F(DiffuseTransmissionDoesNotDefineAnOpticalInterior, Fixture)
    {
        add_closure(TranslucentID, 1.0f, basis.get_normal());
        Arena inputs;
        CompositeSurfaceClosure closure(basis, tree, inputs);
        EXPECT_TRUE(bsdf->has_transmission(&closure));
        EXPECT_FALSE(bsdf->has_interior(&closure));
    }

    TEST_CASE_F(IdealReflectionHasNoContinuousDensity, Fixture)
    {
        add_mirror(0.5f, basis.get_normal());
        Arena inputs;
        CompositeSurfaceClosure closure(basis, tree, inputs);
        bsdf->prepare_inputs(inputs, point, &closure);
        DirectShadingComponents value;
        EXPECT_EQ(0.0f, bsdf->evaluate(&closure, false, false, geometry,
            basis.get_normal(), basis.get_normal(), ScatteringMode::All, value));
        EXPECT_EQ(0.0f, bsdf->evaluate_pdf(&closure, false, geometry,
            basis.get_normal(), basis.get_normal(), ScatteringMode::All));
    }

    TEST_CASE_F(SmoothMetalHasNoContinuousDensity, Fixture)
    {
        auto metal = MetalBRDFFactory().create("smooth_metal", ParamArray());
        MetalBRDFInputValues values{};
        values.m_normal_reflectance.set(0.5f);
        values.m_reflectance_multiplier = 1.0f;
        values.m_precomputed.m_a.set(0.0f);
        DirectShadingComponents value;
        EXPECT_EQ(0.0f, metal->evaluate(&values, false, false, geometry,
            basis.get_normal(), basis.get_normal(), ScatteringMode::All, value));
        EXPECT_EQ(0.0f, metal->evaluate_pdf(&values, false, geometry,
            basis.get_normal(), basis.get_normal(), ScatteringMode::All));
    }

    TEST_CASE_F(DeltaMixture_PreservesTotalReflectionForEitherClosure, Fixture)
    {
        const Vector3f tilted(0.6f, 0.8f, 0.0f);
        add_mirror(0.2f, basis.get_normal());
        add_mirror(0.3f, tilted);
        size_t straight = 0, angled = 0;
        const size_t SampleCount = 64;
        for (size_t i = 0; i < SampleCount; ++i)
        {
            const BSDFSample result = sample();
            ASSERT_EQ(ScatteringMode::Specular, result.get_mode());
            EXPECT_EQ(BSDF::DiracDelta, result.get_probability());
            const Vector3f& incoming = result.m_incoming.get_value();
            const Vector3f& normal = incoming.x > 0.5f ? tilted : basis.get_normal();
            const float cosine = std::abs(dot(incoming, normal));
            EXPECT_FEQ_EPS(0.5f, result.m_value.m_beauty[0] * cosine, 1.0e-6f);
            if (incoming.x > 0.5f) ++angled;
            else ++straight;
        }
        EXPECT_TRUE(straight > 0);
        EXPECT_TRUE(angled > 0);
    }

    TEST_CASE_F(ModeMask_SelectsEligibleClosureAfterAnExcludedOne, Fixture)
    {
        add_diffuse(0.2f, basis.get_normal());
        add_mirror(0.3f, basis.get_normal());
        const BSDFSample result = sample(ScatteringMode::Specular);
        ASSERT_EQ(ScatteringMode::Specular, result.get_mode());
        EXPECT_FEQ_EPS(0.3f, result.m_value.m_beauty[0], 1.0e-6f);
    }

    TEST_CASE_F(ModeMask_SelectsDiffuseAfterAnExcludedMirror, Fixture)
    {
        add_mirror(0.2f, basis.get_normal());
        add_diffuse(0.3f, basis.get_normal());
        const BSDFSample result = sample(ScatteringMode::Diffuse);
        ASSERT_EQ(ScatteringMode::Diffuse, result.get_mode());
        EXPECT_FEQ_EPS(0.3f * RcpPi<float>(), result.m_value.m_beauty[0], 1.0e-6f);
    }

    TEST_CASE_F(ContinuousMixture_UsesEachClosuresShadingFrame, Fixture)
    {
        const Vector3f tilted(0.6f, 0.8f, 0.0f);
        const float first_weight = 0.2f, second_weight = 0.3f;
        add_diffuse(first_weight, basis.get_normal());
        add_diffuse(second_weight, tilted);
        const size_t SampleCount = 32;
        for (size_t i = 0; i < SampleCount; ++i)
        {
            const BSDFSample result = sample(ScatteringMode::Diffuse);
            ASSERT_EQ(ScatteringMode::Diffuse, result.get_mode());
            const Vector3f& incoming = result.m_incoming.get_value();
            const float first_cos = std::max(dot(incoming, basis.get_normal()), 0.0f);
            const float second_cos = std::max(dot(incoming, tilted), 0.0f);
            const float expected_pdf = (first_weight * first_cos + second_weight * second_cos)
                * RcpPi<float>() / (first_weight + second_weight);
            const float expected_value = ((first_cos > 0.0f ? first_weight : 0.0f) +
                (second_cos > 0.0f ? second_weight : 0.0f)) * RcpPi<float>();
            EXPECT_FEQ_EPS(expected_pdf, result.get_probability(), 1.0e-6f);
            EXPECT_FEQ_EPS(expected_value, result.m_value.m_beauty[0], 1.0e-6f);
            Arena inputs;
            CompositeSurfaceClosure mixture(basis, tree, inputs);
            DirectShadingComponents evaluated;
            const float evaluated_pdf = bsdf->evaluate(&mixture, false, false,
                geometry, basis.get_normal(), incoming, ScatteringMode::Diffuse, evaluated);
            EXPECT_FEQ_EPS(expected_pdf, evaluated_pdf, 1.0e-6f);
            EXPECT_FEQ_EPS(expected_value, evaluated.m_beauty[0], 1.0e-6f);
            EXPECT_FEQ_EPS(expected_pdf, bsdf->evaluate_pdf(&mixture, false,
                geometry, basis.get_normal(), incoming, ScatteringMode::Diffuse), 1.0e-6f);
        }
    }

    TEST_CASE_F(DiffuseAndMirrorMixture_PreservesEnergyForEitherSamplingMode, Fixture)
    {
        add_diffuse(0.2f, basis.get_normal());
        add_mirror(0.3f, basis.get_normal());
        size_t diffuse = 0, specular = 0;
        const size_t SampleCount = 64;
        for (size_t i = 0; i < SampleCount; ++i)
        {
            const BSDFSample result = sample();
            ASSERT_TRUE(result.get_mode() != ScatteringMode::None);
            const float cosine = dot(result.m_incoming.get_value(), basis.get_normal());
            const float probability = result.get_probability() == BSDF::DiracDelta ?
                1.0f : result.get_probability();
            EXPECT_FEQ_EPS(0.5f, result.m_value.m_beauty[0] * cosine / probability, 1.0e-6f);
            if (result.get_mode() == ScatteringMode::Diffuse) ++diffuse;
            else ++specular;
        }
        EXPECT_TRUE(diffuse > 0);
        EXPECT_TRUE(specular > 0);
    }

    TEST_CASE_F(MaximumClosureCount_CanBeSampledAndRejectsAnExtraClosure, Fixture)
    {
        const float weight = 1.0f / CompositeSurfaceClosure::MaxClosureEntries;
        for (size_t i = 0; i < CompositeSurfaceClosure::MaxClosureEntries; ++i)
            add_mirror(weight, basis.get_normal());
        const BSDFSample result = sample();
        ASSERT_EQ(ScatteringMode::Specular, result.get_mode());
        EXPECT_FEQ_EPS(1.0f, result.m_value.m_beauty[0], 1.0e-6f);
        add_mirror(weight, basis.get_normal());
        EXPECT_EXCEPTION(ExceptionOSLRuntimeError, sample());
    }

    TEST_CASE_F(FilteredMixture_RoundedUpperEndpointStaysInAnEligibleClosure, Fixture)
    {
        // Normalizing these weights rounds the sum below the largest float
        // less than one.  The excluded trailing mirror must remain excluded.
        add_diffuse(1.0f, basis.get_normal());
        add_diffuse(17.0f, basis.get_normal());
        add_diffuse(13.0f, basis.get_normal());
        add_mirror(1.0f, basis.get_normal());
        Arena inputs;
        CompositeSurfaceClosure mixture(basis, tree, inputs);
        float pdfs[CompositeSurfaceClosure::MaxClosureEntries];
        EXPECT_EQ(3, mixture.compute_pdfs(ScatteringMode::Diffuse, pdfs));
        EXPECT_EQ(2, mixture.choose_closure(std::nextafter(1.0f, 0.0f),
            mixture.get_closure_count(), pdfs));
    }

    TEST_CASE_F(ZeroProposalWeightHasNoSamplingDistribution, Fixture)
    {
        add_mirror(0.5f, basis.get_normal());
        Arena inputs;
        CompositeSurfaceClosure mixture(basis, tree, inputs);
        mixture.override_closure_scalar_weight(0.0f);
        float pdfs[CompositeSurfaceClosure::MaxClosureEntries];
        EXPECT_EQ(0, mixture.compute_pdfs(ScatteringMode::All, pdfs));
        BSDFSample result;
        bsdf->sample(sampling, &mixture, false, false, geometry,
            Dual3f(basis.get_normal()), ScatteringMode::All, result);
        EXPECT_EQ(ScatteringMode::None, result.get_mode());
    }

    TEST_CASE_F(EmptyOrExcludedMixturesHaveNoScattering, Fixture)
    {
        EXPECT_EQ(ScatteringMode::None, sample().get_mode());
        add_diffuse(0.5f, basis.get_normal());
        EXPECT_EQ(ScatteringMode::None, sample(ScatteringMode::Specular).get_mode());
        add_mirror(0.5f, basis.get_normal());
        EXPECT_EQ(ScatteringMode::None, sample(ScatteringMode::Glossy).get_mode());
    }
}
