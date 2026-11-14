
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
#include "renderer/kernel/intersection/intersector.h"
#include "renderer/kernel/intersection/tracecontext.h"
#include "renderer/kernel/shading/shadingpoint.h"
#include "renderer/kernel/texturing/texturecache.h"
#include "renderer/kernel/texturing/texturestore.h"
#include "renderer/modeling/object/rectangleobject.h"
#include "renderer/modeling/object/sphereobject.h"
#include "renderer/modeling/scene/assembly.h"
#include "renderer/modeling/scene/assemblyinstance.h"
#include "renderer/modeling/scene/containers.h"
#include "renderer/modeling/scene/objectinstance.h"
#include "renderer/modeling/scene/scene.h"
#include "renderer/modeling/scene/visibilityflags.h"
#include "renderer/utility/testutils.h"
#include "renderer/kernel/lighting/scatteringmode.h"
#include "renderer/modeling/bsdf/bsdf.h"
#include "renderer/modeling/bsdf/bsdfsample.h"
#include "renderer/modeling/bsdf/glassbsdf.h"
#include "renderer/utility/paramarray.h"

// appleseed.foundation headers.
#include "foundation/containers/dictionary.h"
#include "foundation/math/transform.h"
#include "foundation/math/basis.h"
#include "foundation/math/dual.h"
#include "foundation/math/rng/xoroshiro128plus.h"
#include "foundation/math/vector.h"
#include "foundation/memory/autoreleaseptr.h"
#include "foundation/utility/iostreamop.h"
#include "foundation/utility/test.h"

// Standard headers.
#include <cmath>
#include <stdexcept>

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

    template <typename Factory>
    struct SurfaceScene : public TestSceneBase
    {
        SurfaceScene()
        {
            auto_release_ptr<Assembly> assembly(AssemblyFactory().create("assembly", ParamArray()));
            assembly->objects().insert(Factory().create("plane",
                ParamArray().insert("width", 4.0).insert("height", 4.0)));
            assembly->object_instances().insert(ObjectInstanceFactory::create(
                "plane_instance", ParamArray(), "plane", Transformd::identity(), StringDictionary()));
            m_scene.assembly_instances().insert(AssemblyInstanceFactory::create(
                "assembly_instance", ParamArray(), "assembly"));
            m_scene.assemblies().insert(assembly);
        }
    };

    template <typename Factory>
    struct RefractionTestFixture : public Fixture, public StaticTestSceneContext<SurfaceScene<Factory>>
    {
        TraceContext trace_context;
        TextureStore texture_store;
        TextureCache texture_cache;
        Intersector intersector;
        ShadingPoint point;

        RefractionTestFixture()
          : trace_context(this->m_scene)
          , texture_store(this->m_scene)
          , texture_cache(texture_store)
          , intersector(trace_context, texture_cache)
        {
            trace_context.update();
            values.m_ior = 1.5f;
            values.m_precomputed.m_outside_ior = 1.0f;
            values.m_roughness = 1.0e-4f;
            values.m_precomputed.m_refraction_color.set(1.0f);
            values.m_precomputed.m_refraction_weight = 1.0f;
            set_backfacing(false);
        }

        void set_backfacing(const bool backfacing)
        {
            const double sign = backfacing ? -1.0 : 1.0;
            set_ray(ShadingRay(Vector3d(0.0, 2.0 * sign, 0.0),
                Vector3d(0.0, -sign, 0.0), ShadingRay::Time(), VisibilityFlags::CameraRay, 0));
            values.m_precomputed.m_backfacing = backfacing;
        }

        void set_ray(const ShadingRay& ray)
        {
            point.clear();
            if (!intersector.trace(ray, point))
                throw std::runtime_error("Missing glass differential test surface");
            geometry.m_shading_point = &point;
            geometry.m_geometric_normal = Vector3f(point.get_geometric_normal());
            geometry.m_shading_basis = Basis3f(point.get_shading_basis());
        }

        static float transmission(const float ni, const float nt, const float ci, const float ct)
        {
            const float rs = (ni * ci - nt * ct) / (ni * ci + nt * ct);
            const float rp = (nt * ci - ni * ct) / (nt * ci + ni * ct);
            return 1.0f - 0.5f * (rs * rs + rp * rp);
        }

        static Vector3f snell(const Vector3f& outgoing, const Vector3f& normal, const float ratio)
        {
            const float cosine = dot(outgoing, normal);
            const Vector3f tangent = outgoing - cosine * normal;
            return -ratio * tangent - std::sqrt(1.0f - ratio * ratio * square_norm(tangent)) * normal;
        }
    };

    using RefractionFixture = RefractionTestFixture<RectangleObjectFactory>;
    using CurvedRefractionFixture = RefractionTestFixture<SphereObjectFactory>;

    // Zero requested roughness still has a minimum GGX width of 0.001.
    // Direction and throughput checks cover its near-ideal limit, not a delta.
    const float SmoothDirectionTolerance = 0.005f;

    TEST_CASE_F(EqualIndexGlassDefinesAnInterior, Fixture)
    {
        values.m_ior = 1.0f;
        EXPECT_TRUE(bsdf->has_interior(&values));
    }

    TEST_CASE_F(SmoothGlass_TransmitsAtNormalIncidenceOnBothSides, RefractionFixture)
    {
        for (const bool backfacing : {false, true})
        {
            set_backfacing(backfacing);
            const Vector3f outgoing(0.0f, backfacing ? -1.0f : 1.0f, 0.0f);
            BSDFSample sample;
            bsdf->sample(sampling_context, &values, false, false, geometry,
                Dual3f(outgoing), ScatteringMode::All, sample);
            ASSERT_EQ(ScatteringMode::Glossy, sample.get_mode());
            EXPECT_FEQ_EPS(-outgoing, sample.m_incoming.get_value(), SmoothDirectionTolerance);
        }
    }

    TEST_CASE_F(SmoothGlass_TransmitsAccordingToSnellOnBothSides, RefractionFixture)
    {
        const float Sine = 0.3f;
        const float Cosine = std::sqrt(1.0f - Sine * Sine);
        for (const bool backfacing : {false, true})
        {
            set_backfacing(backfacing);
            const float sign = backfacing ? -1.0f : 1.0f;
            const Vector3f outgoing(Sine, sign * Cosine, 0.0f);
            const float ratio = backfacing ? values.m_ior : 1.0f / values.m_ior;
            BSDFSample sample;
            bsdf->sample(sampling_context, &values, false, false, geometry,
                Dual3f(outgoing), ScatteringMode::All, sample);
            ASSERT_EQ(ScatteringMode::Glossy, sample.get_mode());
            EXPECT_FEQ_EPS(snell(outgoing, geometry.m_shading_basis.get_normal(), ratio),
                sample.m_incoming.get_value(), SmoothDirectionTolerance);
        }
    }

    TEST_CASE_F(SmoothGlass_TransmitsNeighboringRayDirectionsAccordingToSnell, RefractionFixture)
    {
        const float Step = 1.0e-4f;
        const Vector3f outgoing = normalize(Vector3f(0.3f, 1.0f, 0.0f));
        const Vector3f neighbor_x = normalize(outgoing + Vector3f(Step, 0.0f, 0.0f));
        const Vector3f neighbor_y = normalize(outgoing + Vector3f(0.0f, 0.0f, Step));
        BSDFSample sample;
        bsdf->sample(sampling_context, &values, false, false, geometry,
            Dual3f(outgoing, neighbor_x, neighbor_y), ScatteringMode::All, sample);
        ASSERT_EQ(ScatteringMode::Glossy, sample.get_mode());
        ASSERT_TRUE(sample.m_incoming.has_derivatives());
        const Vector3f normal = geometry.m_shading_basis.get_normal();
        const float ratio = 1.0f / values.m_ior;
        const Vector3f central = snell(outgoing, normal, ratio);
        EXPECT_FEQ_EPS(snell(neighbor_x, normal, ratio) - central,
            sample.m_incoming.get_dx() - sample.m_incoming.get_value(), 1.0e-6f);
        EXPECT_FEQ_EPS(snell(neighbor_y, normal, ratio) - central,
            sample.m_incoming.get_dy() - sample.m_incoming.get_value(), 1.0e-6f);
    }

    TEST_CASE_F(SmoothGlass_TransmittedWeightMatchesFresnelAndRadianceScaling, RefractionFixture)
    {
        const size_t SampleCount = 256;
        for (const bool backfacing : {false, true})
        {
            set_backfacing(backfacing);
            for (const float ior : {1.5f, 2.0f})
            {
                values.m_ior = ior;
                const float ni = backfacing ? ior : 1.0f;
                const float nt = backfacing ? 1.0f : ior;
                const float sine = backfacing ? 0.3f : 0.9f;
                const float ci = std::sqrt(1.0f - sine * sine);
                const float ct = std::sqrt(1.0f - ni * ni * sine * sine / (nt * nt));
                const float expected = transmission(ni, nt, ci, ct) * square(ni / nt);
                const Vector3f outgoing(sine, backfacing ? -ci : ci, 0.0f);
                double weight = 0.0;
                for (size_t i = 0; i < SampleCount; ++i)
                {
                    BSDFSample sample;
                    bsdf->sample(sampling_context, &values, false, false, geometry,
                        Dual3f(outgoing), ScatteringMode::All, sample);
                    ASSERT_EQ(ScatteringMode::Glossy, sample.get_mode());
                    const float cosine = std::abs(dot(sample.m_incoming.get_value(), geometry.m_geometric_normal));
                    weight += sample.m_value.m_beauty[0] * cosine / sample.get_probability();
                }
                EXPECT_FEQ_EPS(static_cast<double>(expected), weight / SampleCount, 0.005);
            }
        }
    }

    TEST_CASE_F(RoughGlass_RadianceAndImportanceUseReciprocalOpticalMeasures, RefractionFixture)
    {
        values.m_roughness = 0.3f;
        const float Sine = 0.2f;
        const float Cosine = std::sqrt(1.0f - Sine * Sine);
        for (const bool backfacing : {false, true})
        {
            set_backfacing(backfacing);
            const float ni = backfacing ? values.m_ior : 1.0f;
            const float nt = backfacing ? 1.0f : values.m_ior;
            const Vector3f outgoing(Sine, backfacing ? -Cosine : Cosine, 0.0f);
            const Vector3f incoming = snell(outgoing, geometry.m_shading_basis.get_normal(), ni / nt);
            DirectShadingComponents radiance, importance;
            const float radiance_pdf = bsdf->evaluate(&values, false, true, geometry,
                outgoing, incoming, ScatteringMode::Glossy, radiance);
            const float importance_pdf = bsdf->evaluate(&values, true, true, geometry,
                outgoing, incoming, ScatteringMode::Glossy, importance);
            ASSERT_TRUE(radiance_pdf > 0.0f);
            EXPECT_FEQ_EPS(radiance_pdf, importance_pdf, 1.0e-6f);
            EXPECT_FEQ_EPS(square(ni / nt), radiance.m_glossy[0] / importance.m_glossy[0], 1.0e-6f);
        }
    }

    TEST_CASE_F(RoughGlass_RejectsBackfacingTransmissionMicrofacets, Fixture)
    {
        values.m_ior = 1.5f;
        values.m_precomputed.m_outside_ior = 1.0f;
        values.m_precomputed.m_reflection_color.set(1.0f);
        values.m_precomputed.m_reflection_weight = 1.0f;
        // Both directions lean toward +X. Their generalized half vector
        // faces the wrong side of one ray, so no transmitting facet exists.
        for (const float roughness : {0.25f, 0.5f, 0.8f})
        {
            values.m_roughness = roughness;
            for (const bool backfacing : {false, true})
            {
                values.m_precomputed.m_backfacing = backfacing;
                for (const float side : {-1.0f, 1.0f})
                {
                    const Vector3f outgoing = normalize(Vector3f(0.9f, side * 0.4f, 0.0f));
                    const Vector3f incoming = normalize(Vector3f(0.98f, -side * 0.2f, 0.0f));
                    for (const bool adjoint : {false, true})
                    {
                        DirectShadingComponents value;
                        EXPECT_EQ(0.0f, bsdf->evaluate(&values, adjoint, false, geometry,
                            outgoing, incoming, ScatteringMode::Glossy, value));
                        EXPECT_EQ(0.0f, value.m_beauty[0]);
                        EXPECT_EQ(0.0f, bsdf->evaluate_pdf(&values, adjoint, geometry,
                            outgoing, incoming, ScatteringMode::Glossy));
                    }
                }
            }
        }
    }

    TEST_CASE_F(RoughGlass_EvaluatesNormalTransmissionOnBothSides, Fixture)
    {
        values.m_ior = 1.5f;
        values.m_precomputed.m_outside_ior = 1.0f;
        for (const bool backfacing : {false, true})
        {
            values.m_precomputed.m_backfacing = backfacing;
            for (const float side : {-1.0f, 1.0f})
            {
                const Vector3f outgoing(0.0f, side, 0.0f);
                const Vector3f incoming = -outgoing;
                for (const bool adjoint : {false, true})
                {
                    DirectShadingComponents value;
                    const float pdf = bsdf->evaluate(&values, adjoint, false, geometry,
                        outgoing, incoming, ScatteringMode::Glossy, value);
                    EXPECT_TRUE(value.m_beauty[0] > 0.0f);
                    EXPECT_TRUE(pdf > 0.0f);
                    EXPECT_FEQ_EPS(pdf, bsdf->evaluate_pdf(&values, adjoint, geometry,
                        outgoing, incoming, ScatteringMode::Glossy), 1.0e-6f);
                }
            }
        }
    }

    TEST_CASE_F(RoughGlass_SamplingAndEvaluationAgree, RefractionFixture)
    {
        const size_t SampleCount = 256;
        const float RelativeTolerance = 0.002f;
        values.m_precomputed.m_reflection_color.set(1.0f);
        values.m_precomputed.m_reflection_weight = 1.0f;
        for (const float roughness : {0.3f, 0.8f})
        {
            values.m_roughness = roughness;
            for (const float anisotropy : {-0.6f, 0.0f, 0.6f})
            {
                values.m_anisotropy = anisotropy;
                for (const bool backfacing : {false, true})
                {
                    set_backfacing(backfacing);
                    for (const float cosine : {0.4f, 1.0f})
                    {
                        const Vector3f outgoing(std::sqrt(1.0f - cosine * cosine),
                            backfacing ? -cosine : cosine, 0.0f);
                        for (const bool adjoint : {false, true})
                        {
                            size_t accepted = 0;
                            for (size_t i = 0; i < SampleCount; ++i)
                            {
                                BSDFSample sample;
                                bsdf->sample(sampling_context, &values, adjoint, false, geometry,
                                    Dual3f(outgoing), ScatteringMode::Glossy, sample);
                                if (sample.get_mode() == ScatteringMode::None)
                                    continue;
                                ++accepted;
                                const Vector3f incoming = sample.m_incoming.get_value();
                                DirectShadingComponents value;
                                const float pdf = bsdf->evaluate(&values, adjoint, false, geometry,
                                    outgoing, incoming, ScatteringMode::Glossy, value);
                                ASSERT_TRUE(pdf > 0.0f);
                                EXPECT_FEQ_EPS(1.0f, pdf / sample.get_probability(), RelativeTolerance);
                                EXPECT_FEQ_EPS(1.0f, value.m_beauty[0] / sample.m_value.m_beauty[0], RelativeTolerance);
                                EXPECT_FEQ_EPS(pdf, bsdf->evaluate_pdf(&values, adjoint, geometry,
                                    outgoing, incoming, ScatteringMode::Glossy), 1.0e-6f);
                            }
                            EXPECT_TRUE(accepted > 0);
                        }
                    }
                }
            }
        }
    }

    TEST_CASE_F(SmoothGlass_TotalInternalReflectionStaysInside, RefractionFixture)
    {
        set_backfacing(true);
        values.m_precomputed.m_reflection_weight = 1.0f;
        values.m_precomputed.m_reflection_color.set(1.0f);
        const Vector3f outgoing(0.8f, -0.6f, 0.0f);
        BSDFSample sample;
        bsdf->sample(sampling_context, &values, false, false, geometry,
            Dual3f(outgoing), ScatteringMode::All, sample);
        ASSERT_EQ(ScatteringMode::Glossy, sample.get_mode());
        EXPECT_FEQ_EPS(Vector3f(-0.8f, -0.6f, 0.0f), sample.m_incoming.get_value(), SmoothDirectionTolerance);
        const float cosine = std::abs(dot(sample.m_incoming.get_value(), geometry.m_geometric_normal));
        const float weight = sample.m_value.m_beauty[0] * cosine / sample.get_probability();
        EXPECT_FEQ_EPS(1.0f, weight, SmoothDirectionTolerance);
    }

    TEST_CASE_F(Reflection_TracksNeighboringRaysOnAPlane, RefractionFixture)
    {
        const Vector3f outgoing = normalize(Vector3f(0.3f, 1.0f, 0.0f));
        const float Step = 1.0e-4f;
        const Vector3f neighbor = normalize(outgoing + Vector3f(Step, 0.0f, 0.0f));
        BSDFSample sample;
        sample.m_incoming = Dual3f(Vector3f(-outgoing.x, outgoing.y, -outgoing.z));
        sample.compute_specular_reflected_differentials(geometry, Dual3f(outgoing, neighbor, outgoing));
        EXPECT_FEQ_EPS(Vector3f(-neighbor.x, neighbor.y, -neighbor.z),
            sample.m_incoming.get_dx(), 1.0e-6f);
        EXPECT_FEQ_EPS(sample.m_incoming.get_value(), sample.m_incoming.get_dy(), 1.0e-6f);
    }

    TEST_CASE_F(SpecularDifferentials_TrackChangingNormalsOnASphere, CurvedRefractionFixture)
    {
        const double Step = 1.0e-4;
        const Vector3d n(0.0, 0.8, 0.6);
        const Vector3d tangent_x(1.0, 0.0, 0.0);
        const Vector3d tangent_y(0.0, 0.6, -0.8);
        ShadingRay ray(2.0 * n, -n, ShadingRay::Time(), VisibilityFlags::CameraRay, 0);
        ray.m_has_differentials = true;
        ray.m_rx_org = ray.m_org + Step * tangent_x;
        ray.m_ry_org = ray.m_org + Step * tangent_y;
        ray.m_rx_dir = ray.m_ry_dir = ray.m_dir;
        set_ray(ray);
        const Vector3f outgoing(n);
        const Vector3f normal_x(std::sqrt(1.0 - Step * Step) * n + Step * tangent_x);
        const Vector3f normal_y(std::sqrt(1.0 - Step * Step) * n + Step * tangent_y);
        const Dual3f outgoing_rays(outgoing, outgoing, outgoing);
        const float ratio = 1.0f / values.m_ior;
        BSDFSample transmitted;
        transmitted.m_incoming = Dual3f(snell(outgoing, Vector3f(n), ratio));
        transmitted.compute_specular_transmitted_differentials(geometry, ratio, true, outgoing_rays);
        EXPECT_FEQ_EPS(snell(outgoing, normal_x, ratio), transmitted.m_incoming.get_dx(), 1.0e-6f);
        EXPECT_FEQ_EPS(snell(outgoing, normal_y, ratio), transmitted.m_incoming.get_dy(), 1.0e-6f);
        BSDFSample reflected;
        reflected.m_incoming = Dual3f(outgoing);
        reflected.compute_specular_reflected_differentials(geometry, outgoing_rays);
        EXPECT_FEQ_EPS(2.0f * dot(outgoing, normal_x) * normal_x - outgoing,
            reflected.m_incoming.get_dx(), 1.0e-6f);
        EXPECT_FEQ_EPS(2.0f * dot(outgoing, normal_y) * normal_y - outgoing,
            reflected.m_incoming.get_dy(), 1.0e-6f);
    }

    TEST_CASE_F(ZeroRoughness_TransmitsExactSnellAndFresnelOnBothSides, RefractionFixture)
    {
        values.m_roughness = 0.0f;
        const float Sine = 0.3f;
        const float Cosine = std::sqrt(1.0f - Sine * Sine);
        for (const bool backfacing : {false, true})
        {
            set_backfacing(backfacing);
            const Vector3f outgoing(Sine, backfacing ? -Cosine : Cosine, 0.0f);
            const float ni = backfacing ? values.m_ior : 1.0f;
            const float nt = backfacing ? 1.0f : values.m_ior;
            const float ratio = ni / nt;
            const Vector3f expected = snell(outgoing, geometry.m_shading_basis.get_normal(), ratio);
            const float cosine_in = std::abs(expected.y);
            for (const bool adjoint : {false, true})
            {
                BSDFSample sample;
                bsdf->sample(sampling_context, &values, adjoint, false, geometry,
                    Dual3f(outgoing), ScatteringMode::All, sample);
                ASSERT_EQ(ScatteringMode::Specular, sample.get_mode());
                EXPECT_EQ(BSDF::DiracDelta, sample.get_probability());
                EXPECT_FEQ_EPS(expected, sample.m_incoming.get_value(), 1.0e-6f);
                const float weight = transmission(ni, nt, Cosine, cosine_in) *
                    (adjoint ? 1.0f : ratio * ratio);
                EXPECT_FEQ_EPS(weight, sample.m_value.m_beauty[0] * cosine_in, 1.0e-6f);
            }
        }
    }

    TEST_CASE_F(ZeroRoughness_BalancesTintedFresnelChoices, RefractionFixture)
    {
        values.m_roughness = 0.0f;
        values.m_precomputed.m_reflection_color.set(0.75f);
        values.m_precomputed.m_reflection_weight = 0.75f;
        values.m_precomputed.m_refraction_color.set(0.25f);
        values.m_precomputed.m_refraction_weight = 0.25f;
        const float transmitted = transmission(1.0f, values.m_ior, 1.0f, 1.0f);
        const float expected = 0.75f * (1.0f - transmitted) + 0.25f * transmitted;
        size_t reflection_count = 0, refraction_count = 0;
        const size_t SampleCount = 256;
        for (size_t i = 0; i < SampleCount; ++i)
        {
            BSDFSample sample;
            bsdf->sample(sampling_context, &values, false, false, geometry,
                Dual3f(Vector3f(0.0f, 1.0f, 0.0f)), ScatteringMode::All, sample);
            ASSERT_EQ(ScatteringMode::Specular, sample.get_mode());
            const bool reflected = sample.m_incoming.get_value().y > 0.0f;
            const float correction = reflected ? 1.0f : square(1.0f / values.m_ior);
            EXPECT_FEQ_EPS(expected * correction, sample.m_value.m_beauty[0], 1.0e-6f);
            if (reflected)
                ++reflection_count;
            else ++refraction_count;
        }
        EXPECT_TRUE(reflection_count > 0);
        EXPECT_TRUE(refraction_count > 0);
    }

    TEST_CASE_F(ZeroRoughness_PreservesRefractedNeighbors, RefractionFixture)
    {
        values.m_roughness = 0.0f;
        const float Step = 1.0e-4f;
        for (const bool backfacing : {false, true})
        {
            set_backfacing(backfacing);
            const float sign = backfacing ? -1.0f : 1.0f;
            const Vector3f outgoing = normalize(Vector3f(0.3f, sign, 0.0f));
            const Vector3f neighbor = normalize(outgoing + Vector3f(Step, 0.0f, 0.0f));
            BSDFSample sample;
            bsdf->sample(sampling_context, &values, false, false, geometry,
                Dual3f(outgoing, neighbor, outgoing), ScatteringMode::All, sample);
            ASSERT_EQ(ScatteringMode::Specular, sample.get_mode());
            ASSERT_TRUE(sample.m_incoming.has_derivatives());
            const float ratio = backfacing ? values.m_ior : 1.0f / values.m_ior;
            EXPECT_FEQ_EPS(snell(neighbor, geometry.m_shading_basis.get_normal(), ratio),
                sample.m_incoming.get_dx(), 1.0e-6f);
            EXPECT_FEQ_EPS(sample.m_incoming.get_value(), sample.m_incoming.get_dy(), 1.0e-6f);
        }
    }

    TEST_CASE_F(ZeroRoughness_ReflectsWithFresnelAndTotalInternalReflection, RefractionFixture)
    {
        values.m_roughness = 0.0f;
        values.m_precomputed.m_refraction_weight = 0.0f;
        values.m_precomputed.m_refraction_color.set(0.0f);
        values.m_precomputed.m_reflection_weight = 1.0f;
        values.m_precomputed.m_reflection_color.set(1.0f);
        const float Sine = 0.3f;
        const float Cosine = std::sqrt(1.0f - Sine * Sine);
        BSDFSample sample;
        bsdf->sample(sampling_context, &values, false, false, geometry,
            Dual3f(Vector3f(Sine, Cosine, 0.0f)), ScatteringMode::All, sample);
        ASSERT_EQ(ScatteringMode::Specular, sample.get_mode());
        EXPECT_FEQ_EPS(Vector3f(-Sine, Cosine, 0.0f), sample.m_incoming.get_value(), 1.0e-6f);
        const float ct = std::sqrt(1.0f - Sine * Sine / (values.m_ior * values.m_ior));
        EXPECT_FEQ_EPS(1.0f - transmission(1.0f, values.m_ior, Cosine, ct),
            sample.m_value.m_beauty[0] * Cosine, 1.0e-6f);
        set_backfacing(true);
        BSDFSample tir;
        bsdf->sample(sampling_context, &values, false, false, geometry,
            Dual3f(Vector3f(0.8f, -0.6f, 0.0f)), ScatteringMode::All, tir);
        ASSERT_EQ(ScatteringMode::Specular, tir.get_mode());
        EXPECT_FEQ_EPS(Vector3f(-0.8f, -0.6f, 0.0f), tir.m_incoming.get_value(), 1.0e-6f);
        EXPECT_FEQ_EPS(1.0f, tir.m_value.m_beauty[0] * 0.6f, 1.0e-6f);
    }

    TEST_CASE_F(ZeroRoughness_RespectsModesAndRejectsBlackGlass, RefractionFixture)
    {
        values.m_roughness = 0.0f;
        const Dual3f outgoing(Vector3f(0.0f, 1.0f, 0.0f));
        BSDFSample excluded;
        bsdf->sample(sampling_context, &values, false, false, geometry,
            outgoing, ScatteringMode::Glossy, excluded);
        EXPECT_EQ(ScatteringMode::None, excluded.get_mode());
        BSDFSample included;
        bsdf->sample(sampling_context, &values, false, false, geometry,
            outgoing, ScatteringMode::Specular, included);
        EXPECT_EQ(ScatteringMode::Specular, included.get_mode());
        DirectShadingComponents evaluated;
        EXPECT_EQ(0.0f, bsdf->evaluate(&values, false, false, geometry,
            outgoing.get_value(), -outgoing.get_value(), ScatteringMode::All, evaluated));
        EXPECT_EQ(0.0f, bsdf->evaluate_pdf(&values, false, geometry,
            outgoing.get_value(), -outgoing.get_value(), ScatteringMode::All));
        values.m_precomputed.m_refraction_weight = 0.0f;
        values.m_precomputed.m_refraction_color.set(0.0f);
        BSDFSample black;
        bsdf->sample(sampling_context, &values, false, false, geometry,
            outgoing, ScatteringMode::All, black);
        EXPECT_EQ(ScatteringMode::None, black.get_mode());
    }

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
