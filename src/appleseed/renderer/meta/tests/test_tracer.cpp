
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

// appleseed.renderer headers.
#include "renderer/global/globaltypes.h"
#include "renderer/kernel/intersection/intersector.h"
#include "renderer/kernel/intersection/tracecontext.h"
#include "renderer/kernel/lighting/tracer.h"
#include "renderer/kernel/lighting/pathtracer.h"
#include "renderer/modeling/bsdf/glassbsdf.h"
#include "renderer/kernel/rendering/rendererservices.h"
#include "renderer/kernel/shading/closures.h"
#include "renderer/kernel/shading/oslshadergroupexec.h"
#include "renderer/kernel/shading/oslshadingsystem.h"
#include "renderer/kernel/shading/shadingcontext.h"
#include "renderer/kernel/shading/shadingpoint.h"
#include "renderer/kernel/shading/shadingray.h"
#include "renderer/kernel/texturing/oiiotexturesystem.h"
#include "renderer/kernel/texturing/texturecache.h"
#include "renderer/kernel/texturing/texturestore.h"
#include "renderer/modeling/camera/pinholecamera.h"
#include "renderer/modeling/color/colorentity.h"
#include "renderer/modeling/entity/onframebeginrecorder.h"
#include "renderer/modeling/frame/frame.h"
#include "renderer/modeling/material/genericmaterial.h"
#include "renderer/modeling/object/meshobject.h"
#include "renderer/modeling/object/sphereobject.h"
#include "renderer/modeling/object/triangle.h"
#include "renderer/modeling/project/project.h"
#include "renderer/modeling/scene/assembly.h"
#include "renderer/modeling/scene/assemblyinstance.h"
#include "renderer/modeling/scene/objectinstance.h"
#include "renderer/modeling/scene/scene.h"
#include "renderer/modeling/scene/visibilityflags.h"
#include "renderer/modeling/shadergroup/shadercompiler.h"
#include "renderer/modeling/shadergroup/shadergroup.h"
#include "renderer/modeling/surfaceshader/constantsurfaceshader.h"
#include "renderer/modeling/surfaceshader/surfaceshader.h"
#include "renderer/modeling/volume/genericvolume.h"
#include "renderer/modeling/volume/volume.h"
#include "renderer/utility/iostreamop.h"
#include "renderer/utility/paramarray.h"
#include "renderer/utility/testutils.h"

// appleseed.foundation headers.
#include "foundation/containers/dictionary.h"
#include "foundation/image/color.h"
#include "foundation/math/matrix.h"
#include "foundation/math/rng/xoroshiro128plus.h"
#include "foundation/math/transform.h"
#include "foundation/math/vector.h"
#include "foundation/memory/arena.h"
#include "foundation/memory/autoreleaseptr.h"
#include "foundation/utility/iostreamop.h"
#include "foundation/utility/test.h"

// OpenImageIO headers.
#include "foundation/platform/_beginoiioheaders.h"
#include "OpenImageIO/texture.h"
#include "foundation/platform/_endoiioheaders.h"

// Standard headers.
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

using namespace foundation;
using namespace renderer;

TEST_SUITE(Renderer_Kernel_Lighting_Tracer)
{
    struct SceneBase
      : public TestSceneBase
    {
        Assembly*           m_assembly;
        AssemblyInstance*   m_assembly_instance;

        SceneBase()
        {
            m_scene.cameras().insert(
                PinholeCameraFactory().create(
                    "camera",
                    ParamArray()
                        .insert("film_width", "0.025")
                        .insert("film_height", "0.025")
                        .insert("focal_length", "0.035")));

            m_project.set_frame(
                FrameFactory::create(
                    "frame",
                    ParamArray()
                        .insert("resolution", "512 512")
                        .insert("camera", "camera")));

            m_scene.assemblies().insert(
                AssemblyFactory().create("assembly", ParamArray()));
            m_assembly = m_scene.assemblies().get_by_name("assembly");

            m_scene.assembly_instances().insert(
                AssemblyInstanceFactory::create(
                    "assembly_inst",
                    ParamArray(),
                    "assembly"));
            m_assembly_instance = m_scene.assembly_instances().get_by_name("assembly_inst");

            create_color("white", Color4f(1.0f));
            create_constant_surface_shader("constant_white_surface_shader", "white");
            create_material("opaque_material", "constant_white_surface_shader", 1.0f);
            create_material("transparent_material", "constant_white_surface_shader", 0.5f);

            create_plane_object();
        }

        void create_color(const char* name, const Color4f& color)
        {
            ParamArray params;
            params.insert("color_space", "linear_rgb");

            const ColorValueArray color_values(3, &color[0]);
            const ColorValueArray alpha_values(1, &color[3]);

            m_assembly->colors().insert(
                ColorEntityFactory::create(name, params, color_values, alpha_values));
        }

        void create_constant_surface_shader(const char* surface_shader_name, const char* color_name)
        {
            ParamArray params;
            params.insert("color", color_name);

            ConstantSurfaceShaderFactory factory;
            auto_release_ptr<SurfaceShader> surface_shader(factory.create(surface_shader_name, params));
            m_assembly->surface_shaders().insert(surface_shader);
        }

        void create_material(const char* material_name, const char* surface_shader_name, const float alpha)
        {
            ParamArray params;
            params.insert("surface_shader", surface_shader_name);
            params.insert("alpha_map", alpha);

            m_assembly->materials().insert(
                GenericMaterialFactory().create(material_name, params));
        }

        void create_plane_object()
        {
            auto_release_ptr<MeshObject> mesh_object(
                MeshObjectFactory().create("plane", ParamArray()));

            mesh_object->push_vertex(GVector3(0.0f, -0.5f, -0.5f));
            mesh_object->push_vertex(GVector3(0.0f, +0.5f, -0.5f));
            mesh_object->push_vertex(GVector3(0.0f, +0.5f, +0.5f));
            mesh_object->push_vertex(GVector3(0.0f, -0.5f, +0.5f));

            mesh_object->push_vertex_normal(GVector3(-1.0f, 0.0f, 0.0f));

            mesh_object->push_triangle(Triangle(0, 1, 2, 0, 0, 0, 0));
            mesh_object->push_triangle(Triangle(2, 3, 0, 0, 0, 0, 0));

            mesh_object->push_material_slot("material");

            auto_release_ptr<Object> object(mesh_object.release());
            m_assembly->objects().insert(object);
        }

        void create_plane_object_instance(
            const char*             name,
            const Vector3d&         position,
            const char*             material_name,
            const ParamArray&       params = ParamArray())
        {
            StringDictionary material_mappings;
            material_mappings.insert("material", material_name);

            m_assembly->object_instances().insert(
                ObjectInstanceFactory::create(
                    name,
                    params,
                    "plane",
                    Transformd::from_local_to_parent(
                        Matrix4d::make_translation(position)),
                    material_mappings,
                    material_mappings));
        }
    };

    template <typename ParamFixtureBaseClass, bool ParamUseEmbree>
    struct FixtureParams
    {
        typedef ParamFixtureBaseClass FixtureBaseClass;

        static bool UseEmbree()
        {
            return ParamUseEmbree;
        }
    };

    template <typename FixtureParams>
    struct Fixture
      : public StaticTestSceneContext<typename FixtureParams::FixtureBaseClass>
    {
        TraceContext                            m_trace_context;
        TextureStore                            m_texture_store;
        TextureCache                            m_texture_cache;
        Intersector                             m_intersector;
        std::shared_ptr<OIIOTextureSystem>      m_texture_system;
        RendererServices                        m_renderer_services;
        std::shared_ptr<OSLShadingSystem>       m_shading_system;
        Arena                                   m_arena;
        OSLShaderGroupExec                      m_shading_group_exec;
        ShadingContext                          m_shading_context;
        Tracer                                  m_tracer;

        Fixture()
          : m_trace_context(FixtureParams::FixtureBaseClass::m_scene)
          , m_texture_store(FixtureParams::FixtureBaseClass::m_scene)
          , m_texture_cache(m_texture_store)
          , m_intersector(m_trace_context, m_texture_cache)
          , m_texture_system(
                OIIOTextureSystemFactory::create())
          , m_renderer_services(
                FixtureParams::FixtureBaseClass::m_project,
                *m_texture_system)
          , m_shading_system(
                OSLShadingSystemFactory::create(&m_renderer_services, m_texture_system.get()),
                [](OSLShadingSystem* object) { object->release(); })
          , m_shading_group_exec(*m_shading_system, m_arena)
          , m_tracer(FixtureParams::FixtureBaseClass::m_scene, m_intersector, m_shading_group_exec)
          , m_shading_context(
                m_intersector,
                m_tracer,
                m_texture_cache,
                *m_texture_system,
                m_shading_group_exec,
                m_arena,
                0)  // thread index
        {
#ifdef APPLESEED_WITH_EMBREE
            m_trace_context.set_use_embree(FixtureParams::UseEmbree());
#endif
            m_trace_context.update();
        }
    };

    template <bool HiddenBoundary>
    struct AbsorptionScene
      : public SceneBase
    {
        AbsorptionScene()
        {
            m_assembly->bsdfs().insert(
                GlassBSDFFactory().create(
                    "absorbing_glass",
                    ParamArray()
                        .insert("surface_transmittance", "1.0")
                        .insert("ior", "1.0")
                        .insert("volume_parameterization", "transmittance")
                        .insert("volume_transmittance", "0.25")
                        .insert("volume_transmittance_distance", "20.0")));
            m_assembly->materials().insert(
                GenericMaterialFactory().create(
                    "absorbing_medium",
                    ParamArray().insert("bsdf", "absorbing_glass")));
            create_plane_object_instance(
                "medium", Vector3d(0.0), "absorbing_medium",
                ParamArray().insert("medium_priority", 2));
            create_plane_object_instance(
                "terminal", Vector3d(12.0, 0.0, 0.0), "opaque_material",
                ParamArray().insert("medium_priority", 3));
            if (HiddenBoundary)
            {
                create_plane_object_instance(
                    "hidden", Vector3d(8.0, 0.0, 0.0), "absorbing_medium",
                    ParamArray().insert("medium_priority", 1));
            }
        }
    };

    struct AbsorptionPathVisitor
    {
        size_t m_hits = 0;
        Spectrum m_throughput = Spectrum(0.0f);

        void on_first_diffuse_bounce(const PathVertex&, const Spectrum&) {}
        bool accept_scattering(ScatteringMode::Mode, ScatteringMode::Mode) const { return true; }
        void on_miss(const PathVertex&) {}
        void on_hit(const PathVertex& vertex)
        {
            ++m_hits;
            m_throughput = vertex.m_throughput;
        }
        void on_scatter(PathVertex&) {}
    };

    struct AbsorptionVolumeVisitor
    {
        bool accept_scattering(ScatteringMode::Mode) const { return true; }
        void on_scatter(PathVertex&) {}
        void visit_ray(PathVertex&, const ShadingRay&) {}
    };

    template <bool HiddenBoundary>
    struct AbsorptionFixture
      : public Fixture<FixtureParams<AbsorptionScene<HiddenBoundary>, false>>
    {
        template <bool Adjoint>
        AbsorptionPathVisitor trace_path(const size_t max_bounces)
        {
            ShadingRay ray(
                Vector3d(4.0, 0.0, 0.0), Vector3d(1.0, 0.0, 0.0),
                ShadingRay::Time::create_with_normalized_time(0.0f, 0.0f, 1.0f),
                VisibilityFlags::CameraRay, 0);
            const ShadingRay source(ray);
            ray.add_medium(
                source,
                this->m_assembly->object_instances().get_by_name("medium"),
                this->m_assembly->materials().get_by_name("absorbing_medium"),
                1.0f);

            Xoroshiro128plus rng;
            SamplingContext sampling_context(rng, SamplingContext::RNGMode);
            AbsorptionPathVisitor visitor;
            AbsorptionVolumeVisitor volume_visitor;
            PathTracer<AbsorptionPathVisitor, AbsorptionVolumeVisitor, Adjoint> tracer(
                visitor, volume_visitor,
                100, max_bounces, max_bounces, max_bounces, max_bounces, max_bounces, false);
            tracer.trace(sampling_context, this->m_shading_context, ray);
            return visitor;
        }
    };

    TEST_CASE_F(PathTrace_AttenuatesBeforeTerminalHit, AbsorptionFixture<false>)
    {
        const AbsorptionPathVisitor visitor = trace_path<false>(4);
        EXPECT_EQ(1, visitor.m_hits);
        EXPECT_FEQ(Spectrum(std::pow(0.25f, 8.0f / 20.0f)), visitor.m_throughput);
    }

    TEST_CASE_F(PathTrace_AttenuatesBeforeBounceLimit, AbsorptionFixture<false>)
    {
        const AbsorptionPathVisitor visitor = trace_path<false>(0);
        EXPECT_EQ(1, visitor.m_hits);
        EXPECT_FEQ(Spectrum(std::pow(0.25f, 8.0f / 20.0f)), visitor.m_throughput);
    }

    TEST_CASE_F(PathTrace_AdjointAttenuatesBeforeTerminalHit, AbsorptionFixture<false>)
    {
        const AbsorptionPathVisitor visitor = trace_path<true>(4);
        EXPECT_EQ(1, visitor.m_hits);
        EXPECT_FEQ(Spectrum(std::pow(0.25f, 8.0f / 20.0f)), visitor.m_throughput);
    }

    TEST_CASE_F(PathTrace_HiddenBoundaryPreservesAbsorptionDistance, AbsorptionFixture<true>)
    {
        const AbsorptionPathVisitor visitor = trace_path<false>(4);
        EXPECT_EQ(1, visitor.m_hits);
        EXPECT_FEQ(Spectrum(std::pow(0.25f, 8.0f / 20.0f)), visitor.m_throughput);
    }

    struct InitialMediaScene
      : public SceneBase
    {
        static constexpr double Radius = 2.0;

        InitialMediaScene()
        {
            m_assembly->bsdfs().insert(GlassBSDFFactory().create(
                "glass", ParamArray()
                    .insert("surface_transmittance", "1.0")
                    .insert("ior", "1.0")
                    .insert("volume_parameterization", "transmittance")
                    .insert("volume_transmittance", "0.25")
                    .insert("volume_transmittance_distance", Radius)));
            m_assembly->materials().insert(GenericMaterialFactory().create(
                "medium", ParamArray().insert("bsdf", "glass")));
            m_assembly->objects().insert(SphereObjectFactory().create(
                "sphere", ParamArray()));
            const StringDictionary mapping = StringDictionary().insert("default", "medium");
            m_assembly->object_instances().insert(ObjectInstanceFactory::create(
                "sphere_instance", ParamArray(), "sphere",
                Transformd(Matrix4d::make_scaling(Vector3d(Radius))), mapping, mapping));
            create_plane_object_instance("terminal", Vector3d(1.0, 0.0, 0.0), "opaque_material");
        }

        static ShadingRay origin_ray()
        {
            return ShadingRay(Vector3d(0.0), Vector3d(1.0, 0.0, 0.0),
                ShadingRay::Time::create_with_normalized_time(0.0f, 0.0f, 1.0f),
                VisibilityFlags::LightRay, 0);
        }
    };

    struct InitialMediaFixture
      : public Fixture<FixtureParams<InitialMediaScene, false>>
    {
        template <bool Adjoint>
        AbsorptionPathVisitor trace_from_origin()
        {
            Xoroshiro128plus rng;
            SamplingContext sampling_context(rng, SamplingContext::RNGMode);
            AbsorptionPathVisitor visitor;
            AbsorptionVolumeVisitor volume_visitor;
            PathTracer<AbsorptionPathVisitor, AbsorptionVolumeVisitor, Adjoint> tracer(
                visitor, volume_visitor, 100, 0, 0, 0, 0, 0, false);
            tracer.trace(sampling_context, m_shading_context, origin_ray());
            return visitor;
        }
    };

    TEST_CASE_F(InitialMedia_PathTraceAttenuatesUnseededOrigin, InitialMediaFixture)
    {
        const AbsorptionPathVisitor visitor = trace_from_origin<false>();
        EXPECT_EQ(1, visitor.m_hits);
        EXPECT_FEQ(Spectrum(0.5f), visitor.m_throughput);
    }

    TEST_CASE_F(InitialMedia_AdjointPathAttenuatesUnseededOrigin, InitialMediaFixture)
    {
        const AbsorptionPathVisitor visitor = trace_from_origin<true>();
        EXPECT_EQ(1, visitor.m_hits);
        EXPECT_FEQ(Spectrum(0.5f), visitor.m_throughput);
    }

    TEST_CASE_F(InitialMedia_PreservesCallerArenaAndIsIdempotent, InitialMediaFixture)
    {
        using RetainedData = std::array<unsigned char, 64>;
        const unsigned char Sentinel = 0xa5;
        RetainedData* retained = m_arena.allocate<RetainedData>();
        retained->fill(Sentinel);
        Xoroshiro128plus rng;
        SamplingContext sampling_context(rng, SamplingContext::RNGMode);
        ShadingRay ray = origin_ray();
        m_intersector.initialize_media(sampling_context, m_shading_context, ray);
        m_intersector.initialize_media(sampling_context, m_shading_context, ray);
        ASSERT_EQ(1, ray.m_medium_count);
        EXPECT_EQ(m_assembly->object_instances().get_by_name("sphere_instance"), ray.get_current_medium()->m_object_instance);
        for (const unsigned char value : *retained)
            EXPECT_EQ(Sentinel, value);
    }

    TEST_CASE_F(InitialMedia_PreservesSeededMedia, InitialMediaFixture)
    {
        Xoroshiro128plus rng;
        SamplingContext sampling_context(rng, SamplingContext::RNGMode);
        ShadingRay ray = origin_ray();
        const ShadingRay source(ray);
        const float SeededIor = 1.25f;
        ray.add_medium(source, m_assembly->object_instances().get_by_name("sphere_instance"),
            m_assembly->materials().get_by_name("medium"), SeededIor);
        m_intersector.initialize_media(sampling_context, m_shading_context, ray);
        ASSERT_EQ(1, ray.m_medium_count);
        EXPECT_FEQ(SeededIor, ray.get_current_medium()->m_ior);
    }

    TEST_CASE_F(InitialMedia_OutsideVolumeInsideBoundsStaysInAir, InitialMediaFixture)
    {
        Xoroshiro128plus rng;
        SamplingContext sampling_context(rng, SamplingContext::RNGMode);
        ShadingRay ray = origin_ray();
        ray.m_org = Vector3d(1.5, 1.5, 0.0);
        ray.m_dir = Vector3d(-1.0, 0.0, 0.0);
        ASSERT_TRUE(m_scene.get_render_data().m_bbox.contains(GVector3(ray.m_org)));
        m_intersector.initialize_media(sampling_context, m_shading_context, ray);
        EXPECT_EQ(0, ray.m_medium_count);
    }

    TEST_CASE_F(InitialMedia_EmitterParentSelectsOutgoingSide, InitialMediaFixture)
    {
        Xoroshiro128plus rng;
        SamplingContext sampling_context(rng, SamplingContext::RNGMode);
        ShadingRay incoming = origin_ray();
        incoming.m_org = Vector3d(3.0, 0.0, 0.0);
        incoming.m_dir = Vector3d(-1.0, 0.0, 0.0);
        ShadingPoint emitter;
        m_intersector.trace(incoming, emitter);
        ASSERT_TRUE(emitter.hit_surface());
        ASSERT_EQ(m_assembly->object_instances().get_by_name("sphere_instance"), &emitter.get_object_instance());
        for (const bool inward : {false, true})
        {
            ShadingRay ray = origin_ray();
            ray.m_org = emitter.get_point();
            ray.m_dir = Vector3d(inward ? -1.0 : 1.0, 0.0, 0.0);
            m_intersector.initialize_media(sampling_context, m_shading_context, ray, &emitter);
            EXPECT_EQ(inward ? 1 : 0, ray.m_medium_count);
            EXPECT_EQ(emitter.get_point(), ray.m_org);
        }
    }

    struct InitialVolumeScene
      : public SceneBase
    {
        static constexpr double Radius = 2.0;
        static constexpr float Absorption = 0.4f;
        static constexpr double TerminalDistance = 1.0;

        InitialVolumeScene()
        {
            m_assembly->volumes().insert(GenericVolumeFactory().create(
                "fog", ParamArray().insert("absorption", Absorption).insert("scattering", 0.0f)
                    .insert("phase_function_model", "isotropic")));
            m_assembly->materials().insert(GenericMaterialFactory().create(
                "medium", ParamArray().insert("volume", "fog")));
            m_assembly->objects().insert(SphereObjectFactory().create("sphere", ParamArray()));
            const StringDictionary mapping = StringDictionary().insert("default", "medium");
            m_assembly->object_instances().insert(ObjectInstanceFactory::create(
                "sphere_instance", ParamArray(), "sphere",
                Transformd(Matrix4d::make_scaling(Vector3d(Radius))), mapping, mapping));
            create_plane_object_instance(
                "terminal", Vector3d(TerminalDistance, 0.0, 0.0), "opaque_material");
        }
    };

    template <typename Scene>
    struct InitialVolumeFixtureBase
      : public Fixture<FixtureParams<Scene, false>>
    {
        template <bool Adjoint>
        AbsorptionPathVisitor trace_from_origin(
            const bool pretraced,
            const double search_min = 0.0,
            const bool clear_arena = true)
        {
            ShadingRay ray = InitialMediaScene::origin_ray();
            ray.m_flags = Adjoint ? VisibilityFlags::LightRay : VisibilityFlags::CameraRay;
            ray.m_tmin = search_min;
            Xoroshiro128plus rng;
            SamplingContext sampling_context(rng, SamplingContext::RNGMode);
            AbsorptionPathVisitor visitor;
            AbsorptionVolumeVisitor volume_visitor;
            PathTracer<AbsorptionPathVisitor, AbsorptionVolumeVisitor, Adjoint> tracer(
                visitor, volume_visitor, 100, 0, 0, 0, 0, 0, false);
            if (pretraced)
            {
                this->m_intersector.initialize_media(sampling_context, this->m_shading_context, ray);
                ShadingPoint point;
                this->m_intersector.trace(ray, point);
                tracer.trace(sampling_context, this->m_shading_context, point, clear_arena);
            }
            else tracer.trace(sampling_context, this->m_shading_context, ray, nullptr, clear_arena);
            return visitor;
        }
    };

    using InitialVolumeFixture = InitialVolumeFixtureBase<InitialVolumeScene>;

    TEST_CASE_F(InitialVolume_RegistersStandaloneMediumWithoutBSDF, InitialVolumeFixture)
    {
        Xoroshiro128plus rng;
        SamplingContext sampling_context(rng, SamplingContext::RNGMode);
        ShadingRay ray = InitialMediaScene::origin_ray();
        m_intersector.initialize_media(sampling_context, m_shading_context, ray);
        m_intersector.initialize_media(sampling_context, m_shading_context, ray);
        ASSERT_EQ(1, ray.m_medium_count);
        EXPECT_FEQ(1.0f, ray.get_current_ior());
        EXPECT_EQ(m_assembly->volumes().get_by_name("fog"), ray.get_current_medium()->get_volume());
    }

    TEST_CASE_F(InitialVolume_AttenuatesBeforeFirstHitInBothTraceOverloads, InitialVolumeFixture)
    {
        for (const bool pretraced : {false, true})
        {
            // Clipping the search halfway to the surface must not discard
            // attenuation from the physical origin to that clipping plane.
            for (const double search_min : {0.0, 0.5 * TerminalDistance})
            {
                const AbsorptionPathVisitor visitor = trace_from_origin<false>(pretraced, search_min);
                EXPECT_EQ(1, visitor.m_hits);
                EXPECT_FEQ(Spectrum(std::exp(-Absorption * TerminalDistance)), visitor.m_throughput);
            }
        }
    }

    TEST_CASE_F(InitialVolume_AdjointAttenuatesBeforeFirstHit, InitialVolumeFixture)
    {
        const AbsorptionPathVisitor visitor = trace_from_origin<true>(false);
        EXPECT_EQ(1, visitor.m_hits);
        EXPECT_FEQ(Spectrum(std::exp(-Absorption * TerminalDistance)), visitor.m_throughput);
    }

    TEST_CASE_F(InitialVolume_PreservesRetainedPathDataWhenArenaClearingIsDisabled, InitialVolumeFixture)
    {
        using RetainedData = std::array<unsigned char, 64>;
        const unsigned char Sentinel = 0xa5;
        for (const bool pretraced : {false, true})
        {
            RetainedData* retained = m_arena.allocate<RetainedData>();
            retained->fill(Sentinel);
            trace_from_origin<false>(pretraced, 0.0, false);
            for (const unsigned char value : *retained)
                EXPECT_EQ(Sentinel, value);
        }
    }

    struct DisplacedVolumeScene : InitialVolumeScene
    {
        DisplacedVolumeScene()
        {
            m_assembly->bsdfs().insert(GlassBSDFFactory().create(
                "subordinate_glass", ParamArray().insert("surface_transmittance", "1.0").insert("ior", "1.0")
                    .insert("volume_parameterization", "transmittance")
                    .insert("volume_transmittance", "0.25")
                    .insert("volume_transmittance_distance", "20.0")));
            m_assembly->materials().insert(GenericMaterialFactory().create(
                "subordinate_material", ParamArray().insert("bsdf", "subordinate_glass")));
            create_plane_object_instance("hidden", Vector3d(0.5, 0.0, 0.0),
                "subordinate_material", ParamArray().insert("medium_priority", -1));
        }
    };

    using DisplacedVolumeFixture = InitialVolumeFixtureBase<DisplacedVolumeScene>;

    TEST_CASE_F(InitialVolume_AttenuatesBeyondDisplacedOpticalBoundary, DisplacedVolumeFixture)
    {
        Xoroshiro128plus rng;
        SamplingContext sampling_context(rng, SamplingContext::RNGMode);
        ShadingRay ray = InitialMediaScene::origin_ray();
        m_intersector.initialize_media(sampling_context, m_shading_context, ray);
        ShadingPoint hidden;
        m_intersector.trace(ray, hidden);
        ASSERT_TRUE(hidden.hit_surface());
        EXPECT_FEQ(0.5, hidden.get_distance());
        m_shading_context.resolve_optical_interface(sampling_context, hidden);
        ASSERT_TRUE(hidden.get_optical_interface() == ShadingPoint::OpticalInterface::Displaced);

        for (const bool pretraced : {false, true})
        {
            const AbsorptionPathVisitor visitor = trace_from_origin<false>(pretraced);
            EXPECT_EQ(1, visitor.m_hits);
            EXPECT_FEQ(Spectrum(std::exp(-Absorption * TerminalDistance)), visitor.m_throughput);
            const AbsorptionPathVisitor adjoint = trace_from_origin<true>(pretraced);
            EXPECT_EQ(1, adjoint.m_hits);
            EXPECT_FEQ(Spectrum(std::exp(-Absorption * TerminalDistance)), adjoint.m_throughput);
        }
    }

    struct MixedVolumeScene : InitialVolumeScene
    {
        static constexpr float GlassIor = 1.5f;

        MixedVolumeScene()
        {
            m_assembly->bsdfs().insert(GlassBSDFFactory().create(
                "adjacent_glass", ParamArray().insert("surface_transmittance", "1.0")
                    .insert("volume_parameterization", "transmittance")
                    .insert("ior", GlassIor)));
            m_assembly->materials().insert(GenericMaterialFactory().create(
                "glass_material", ParamArray().insert("bsdf", "adjacent_glass")));
            create_plane_object_instance("glass", Vector3d(2.0 * Radius, 0.0, 0.0),
                "glass_material", ParamArray().insert("medium_priority", -1));
        }
    };

    using MixedVolumeFixture = Fixture<FixtureParams<MixedVolumeScene, false>>;

    TEST_CASE_F(OpticalInterface_VolumeUsesAdjacentGlassWithoutChangingGeometricOwner, MixedVolumeFixture)
    {
        const ObjectInstance* glass = m_assembly->object_instances().get_by_name("glass");
        const Material* glass_material = m_assembly->materials().get_by_name("glass_material");
        const ObjectInstance* fog = m_assembly->object_instances().get_by_name("sphere_instance");
        const Material* fog_material = m_assembly->materials().get_by_name("medium");
        ASSERT_TRUE(fog->get_medium_priority() > glass->get_medium_priority());
        Xoroshiro128plus rng;
        SamplingContext sampling_context(rng, SamplingContext::RNGMode);
        for (const bool entering : {false, true})
        {
            ShadingRay ray = InitialMediaScene::origin_ray();
            ray.m_org = Vector3d((entering ? 1.5 : -0.75) * Radius, 0.0, 0.0);
            ray.m_dir = Vector3d(-1.0, 0.0, 0.0);
            ShadingRay source(ray);
            ray.add_medium(source, glass, glass_material, GlassIor);
            if (!entering)
            {
                source = ray;
                ray.m_medium_count = 0;
                ray.add_medium(source, fog, fog_material, 1.0f);
            }
            ShadingPoint point;
            m_intersector.trace(ray, point);
            ASSERT_TRUE(point.hit_surface());
            ASSERT_EQ(fog, &point.get_object_instance());
            ASSERT_EQ(entering, point.is_entering());
            ASSERT_EQ(fog_material, point.get_material());
            m_shading_context.resolve_optical_interface(sampling_context, point);
            ASSERT_TRUE(point.get_optical_interface() == ShadingPoint::OpticalInterface::Surface);
            EXPECT_EQ(fog, &point.get_object_instance());
            EXPECT_EQ(glass_material, point.get_material());
            EXPECT_FEQ(GlassIor, point.get_outside_ior());
            EXPECT_FEQ(1.0f, point.get_inside_ior(GlassIor));
            ShadingRay transmitted;
            point.copy_transmitted_media(transmitted);
            ASSERT_TRUE(transmitted.get_current_medium() != nullptr);
            EXPECT_EQ(entering ? fog : glass, transmitted.get_current_medium()->m_object_instance);

            // Path vertices copy resolved points. Preserve the borrowed
            // surface through both copy operations and invalidate it on retrace.
            ShadingPoint copied(point);
            ShadingPoint assigned;
            assigned = point;
            for (ShadingPoint* duplicate : {&copied, &assigned})
            {
                EXPECT_EQ(glass_material, duplicate->get_material());
                EXPECT_FEQ(1.0f, duplicate->get_inside_ior(GlassIor));
                duplicate->set_ray(ray);
                EXPECT_EQ(fog_material, duplicate->get_material());
                EXPECT_FEQ(GlassIor, duplicate->get_inside_ior(GlassIor));
            }
        }
    }

    struct InitialScatteringScene
      : public InitialVolumeScene
    {
        InitialScatteringScene()
        {
            // Near-certain events before any boundary expose the path limit
            // independently of the larger per-volume scattering allowance.
            m_assembly->volumes().get_by_name("fog")->get_parameters()
                .insert("absorption", 0.0f).insert("scattering", 1000.0f);
        }
    };

    using InitialScatteringFixture = Fixture<FixtureParams<InitialScatteringScene, false>>;

    struct CountingVolumeVisitor : AbsorptionVolumeVisitor
    {
        size_t m_scatters = 0;
        void on_scatter(PathVertex&) { ++m_scatters; }
    };

    TEST_CASE_F(InitialVolume_ScatteringHonorsOverallPathLimit, InitialScatteringFixture)
    {
        const size_t VolumeLimit = 8;
        for (const size_t path_limit : {size_t(0), size_t(2)})
        {
            Xoroshiro128plus rng;
            SamplingContext sampling_context(rng, SamplingContext::RNGMode);
            AbsorptionPathVisitor visitor;
            CountingVolumeVisitor volume_visitor;
            PathTracer<AbsorptionPathVisitor, CountingVolumeVisitor, false> tracer(
                visitor, volume_visitor, 100, path_limit, path_limit, path_limit,
                path_limit, VolumeLimit, false);
            tracer.trace(sampling_context, m_shading_context, InitialMediaScene::origin_ray());
            EXPECT_EQ(path_limit, volume_visitor.m_scatters);
        }
    }

    struct OSLQueryScene
      : public SceneBase
    {
        OSLQueryScene()
        {
            create_plane_object_instance("receiver", Vector3d(0.0), "opaque_material");
            create_plane_object_instance("near_target", Vector3d(4.0, 0.0, 0.0), "opaque_material");
            create_plane_object_instance("far_target", Vector3d(8.0, 0.0, 0.0), "opaque_material");
        }
    };

    struct OSLQueryFixture
      : public Fixture<FixtureParams<OSLQueryScene, false>>
    {
        static constexpr float DistanceTolerance = 1.0e-4f;

        ShadingPoint                m_parent;
        ShadingPoint::OSLTraceData   m_trace_data = {};
        OSL::ShaderGlobals          m_globals = {};

        OSLQueryFixture()
        {
            m_project.get_trace_context();
            m_project.update_trace_context();
            m_renderer_services.initialize(m_texture_store);
            m_intersector.trace(
                ShadingRay(
                    Vector3d(-1.0, 0.0, 0.0),
                    Vector3d(1.0, 0.0, 0.0),
                    ShadingRay::Time(),
                    VisibilityFlags::CameraRay,
                    0),
                m_parent);
            m_globals.P = Vector3f(m_parent.get_point());
            m_globals.N = Vector3f(m_parent.get_shading_normal());
            m_globals.renderstate = &m_parent;
            m_globals.tracedata = &m_trace_data;
        }

        bool query(const float mindist, const float maxdist, const float origin = 0.0f)
        {
            OSL::RendererServices::TraceOpt options;
            options.mindist = mindist;
            options.maxdist = maxdist;
            const OSL::Vec3 zero(0.0f);
            return m_renderer_services.trace(
                options, &m_globals, OSL::Vec3(origin, 0.0f, 0.0f),
                zero, zero, OSL::Vec3(1.0f, 0.0f, 0.0f), zero, zero);
        }

        bool message(const char* name, const OIIO::TypeDesc type, void* value, const bool derivatives = false)
        {
            return m_renderer_services.getmessage(
                &m_globals, OIIO::ustringhash("trace"), OIIO::ustringhash(name),
                type, value, derivatives);
        }
    };

    TEST_CASE_F(OSLTrace_MessagesUnavailableBeforeQuery, OSLQueryFixture)
    {
        int hit = -1;
        EXPECT_FALSE(message("hit", OIIO::TypeInt, &hit));
        EXPECT_EQ(-1, hit);
        float distance = -1.0f;
        EXPECT_FALSE(message("hitdist", OIIO::TypeFloat, &distance));
        EXPECT_FEQ(-1.0f, distance);
    }

    TEST_CASE_F(OSLTrace_FirstMissHasNoSurfaceMessages, OSLQueryFixture)
    {
        ASSERT_FALSE(query(0.0f, 2.0f));
        int hit = -1;
        ASSERT_TRUE(message("hit", OIIO::TypeInt, &hit));
        EXPECT_EQ(0, hit);
        for (const char* name : {"hitdist", "u", "v"})
        {
            float values[3] = {-1.0f, -1.0f, -1.0f};
            EXPECT_FALSE(message(name, OIIO::TypeFloat, values, true));
            for (const float value : values)
                EXPECT_FEQ(-1.0f, value);
        }
        for (const char* name : {"P", "N", "Ng"})
        {
            OSL::Vec3 values[3] = {OSL::Vec3(-1.0f), OSL::Vec3(-1.0f), OSL::Vec3(-1.0f)};
            const OIIO::TypeDesc type = name[0] == 'P' ? OIIO::TypePoint : OIIO::TypeNormal;
            EXPECT_FALSE(message(name, type, values, true));
            for (const OSL::Vec3& value : values)
                for (size_t component = 0; component < 3; ++component)
                    EXPECT_FEQ(-1.0f, value[component]);
        }
    }

    TEST_CASE_F(OSLTrace_HitThenMissThenHitReplacesMessages, OSLQueryFixture)
    {
        ASSERT_TRUE(query(0.0f, 6.0f));
        float distance = -1.0f;
        ASSERT_TRUE(message("hitdist", OIIO::TypeFloat, &distance));
        EXPECT_TRUE(std::abs(distance - 4.0f) < DistanceTolerance);
        ASSERT_FALSE(query(0.0f, 2.0f));
        int hit = -1;
        ASSERT_TRUE(message("hit", OIIO::TypeInt, &hit));
        EXPECT_EQ(0, hit);
        EXPECT_FALSE(message("hitdist", OIIO::TypeFloat, &distance));
        ASSERT_TRUE(query(5.0f, 10.0f));
        ASSERT_TRUE(message("hit", OIIO::TypeInt, &hit));
        EXPECT_EQ(1, hit);
        ASSERT_TRUE(message("hitdist", OIIO::TypeFloat, &distance));
        EXPECT_TRUE(std::abs(distance - 8.0f) < DistanceTolerance);
    }

    TEST_CASE_F(OSLTrace_HitMessagesHaveExpectedTypesAndZeroDerivatives, OSLQueryFixture)
    {
        ASSERT_TRUE(query(0.0f, 6.0f));
        float distance[3] = {-1.0f, -1.0f, -1.0f};
        ASSERT_TRUE(message("hitdist", OIIO::TypeFloat, distance, true));
        EXPECT_TRUE(std::abs(distance[0] - 4.0f) < DistanceTolerance);
        EXPECT_FEQ(0.0f, distance[1]);
        EXPECT_FEQ(0.0f, distance[2]);
        for (const char* name : {"P", "N", "Ng"})
        {
            OSL::Vec3 values[3] = {OSL::Vec3(-1.0f), OSL::Vec3(-1.0f), OSL::Vec3(-1.0f)};
            const OIIO::TypeDesc type = name[0] == 'P' ? OIIO::TypePoint : OIIO::TypeNormal;
            ASSERT_TRUE(message(name, type, values, true));
            EXPECT_FEQ(name[0] == 'P' ? 4.0f : -1.0f, values[0][0]);
            EXPECT_FEQ(0.0f, values[0][1]);
            EXPECT_FEQ(0.0f, values[0][2]);
            for (size_t derivative = 1; derivative < 3; ++derivative)
                for (size_t component = 0; component < 3; ++component)
                    EXPECT_FEQ(0.0f, values[derivative][component]);
        }
        float sentinel = -1.0f;
        EXPECT_FALSE(message("hit", OIIO::TypeFloat, &sentinel));
        EXPECT_FALSE(message("unsupported", OIIO::TypeFloat, &sentinel));
        EXPECT_FALSE(m_renderer_services.getmessage(
            &m_globals, OIIO::ustringhash("unsupported"), OIIO::ustringhash("hitdist"),
            OIIO::TypeFloat, &sentinel, false));
        EXPECT_FEQ(-1.0f, sentinel);
    }

    TEST_CASE_F(OSLTrace_NewShaderInvocationClearsPreviousQuery, OSLQueryFixture)
    {
        register_closures(*m_shading_system);
        auto compiler = ShaderCompilerFactory::create("../shaders/stdosl.h");
        auto_release_ptr<ShaderGroup> group(ShaderGroupFactory::create("query_test"));
        group->add_source_shader(
            "surface", "query_test", "surface",
            "surface query_test() { Ci = diffuse(N); }", ParamArray());
        ASSERT_TRUE(group->create_optimized_osl_shader_group(*m_shading_system, compiler.get()));
        for (size_t invocation = 0; invocation < 2; ++invocation)
        {
            m_shading_context.execute_osl_shading(*group, m_parent);
            m_globals = m_parent.get_osl_shader_globals();
            int hit = -1;
            EXPECT_FALSE(message("hit", OIIO::TypeInt, &hit));
            EXPECT_EQ(-1, hit);
            ASSERT_TRUE(query(0.0f, 6.0f));
            ASSERT_TRUE(message("hit", OIIO::TypeInt, &hit));
            EXPECT_EQ(1, hit);
        }
    }

    TEST_CASE_F(OSLTrace_DisplacedOriginUsesQueryDistance, OSLQueryFixture)
    {
        ASSERT_FALSE(query(0.0f, 2.0f, 1.0f));
        ASSERT_TRUE(query(0.0f, 6.0f, 1.0f));
        float distance = -1.0f;
        ASSERT_TRUE(message("hitdist", OIIO::TypeFloat, &distance));
        EXPECT_FEQ(3.0f, distance);
    }

    struct OSLAttributeFixture
      : public OSLQueryFixture
    {
        OSLAttributeFixture()
        {
            auto& attributes = m_assembly->objects().get_by_name("plane")->get_attributes();
            attributes.attribute("test:id", 17);
            attributes.attribute("test:roughness", 0.25f);
            attributes.attribute("test:label", "hull");
            const float color[3] = {0.125f, 0.25f, 0.5f};
            attributes.attribute("test:color", OIIO::TypeColor, color);
            const int indices[2] = {3, 7};
            attributes.attribute("test:indices", OIIO::TypeDesc(OIIO::TypeDesc::INT, 2), indices);
            const char* labels[2] = {"left", "right"};
            attributes.attribute("test:labels", OIIO::TypeDesc(OIIO::TypeDesc::STRING, 2), labels);
            const float varying[2] = {1.0f, 2.0f};
            attributes.attribute("test:varying", OIIO::TypeFloat, 2, varying);
        }

        bool attribute(const char* name, const OIIO::TypeDesc type, void* value, const bool derivatives = false)
        {
            return m_renderer_services.get_attribute(
                &m_globals, derivatives, OIIO::ustringhash(), type, OIIO::ustringhash(name), value);
        }
    };

    TEST_CASE_F(OSLAttributes_ObjectAndCameraNamesUseRuntimeHashes, OSLAttributeFixture)
    {
        const char* names[] = {"object:object_name", "object:object_instance_name",
            "object:assembly_name", "object:assembly_instance_name", "camera:projection"};
        const char* expected[] = {"plane", "receiver", "assembly", "assembly_inst", "perspective"};
        for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); ++index)
        {
            const OIIO::ustringhash sentinel("unchanged");
            OIIO::ustringhash values[3] = {sentinel, sentinel, sentinel};
            ASSERT_TRUE(attribute(names[index], OIIO::TypeString, values, true));
            EXPECT_TRUE(values[0] == OIIO::ustringhash(expected[index]));
            EXPECT_TRUE(values[1] == sentinel);
            EXPECT_TRUE(values[2] == sentinel);
        }
    }

    TEST_CASE_F(OSLAttributes_SceneValuesDoNotRequireASurface, OSLAttributeFixture)
    {
        OSL::ShaderGlobals background = {};
        OIIO::ustringhash projection;
        ASSERT_TRUE(m_renderer_services.get_attribute(
            &background, false, OIIO::ustringhash(), OIIO::TypeString,
            OIIO::ustringhash("camera:projection"), &projection));
        EXPECT_TRUE(projection == OIIO::ustringhash("perspective"));
        OIIO::ustring optimized_projection;
        ASSERT_TRUE(m_renderer_services.get_attribute(
            nullptr, false, OIIO::ustringhash(), OIIO::TypeString,
            OIIO::ustringhash("camera:projection"), &optimized_projection));
        EXPECT_TRUE(optimized_projection == OIIO::ustring("perspective"));
        EXPECT_FALSE(m_renderer_services.get_attribute(
            nullptr, false, OIIO::ustringhash(), OIIO::TypeString,
            OIIO::ustringhash("object:object_name"), &projection));
    }

    TEST_CASE_F(OSLAttributes_UniformValuesPreserveTypesAndClearDerivatives, OSLAttributeFixture)
    {
        int id[3] = {-1, -1, -1};
        ASSERT_TRUE(attribute("test:id", OIIO::TypeInt, id, true));
        EXPECT_EQ(17, id[0]);
        EXPECT_EQ(0, id[1]);
        EXPECT_EQ(0, id[2]);
        float roughness[3] = {-1.0f, -1.0f, -1.0f};
        ASSERT_TRUE(attribute("test:roughness", OIIO::TypeFloat, roughness, true));
        EXPECT_FEQ(0.25f, roughness[0]);
        EXPECT_FEQ(0.0f, roughness[1]);
        EXPECT_FEQ(0.0f, roughness[2]);
        float color[9];
        std::fill_n(color, 9, -1.0f);
        ASSERT_TRUE(attribute("test:color", OIIO::TypeColor, color, true));
        EXPECT_FEQ(0.125f, color[0]);
        EXPECT_FEQ(0.25f, color[1]);
        EXPECT_FEQ(0.5f, color[2]);
        for (size_t index = 3; index < 9; ++index)
            EXPECT_FEQ(0.0f, color[index]);
        int indices[6] = {-1, -1, -1, -1, -1, -1};
        ASSERT_TRUE(attribute("test:indices", OIIO::TypeDesc(OIIO::TypeDesc::INT, 2), indices, true));
        EXPECT_EQ(3, indices[0]);
        EXPECT_EQ(7, indices[1]);
        for (size_t index = 2; index < 6; ++index)
            EXPECT_EQ(0, indices[index]);
    }

    TEST_CASE_F(OSLAttributes_StringValuesAndArraysUseRuntimeHashes, OSLAttributeFixture)
    {
        const OIIO::ustringhash sentinel("unchanged");
        OIIO::ustringhash label = sentinel;
        ASSERT_TRUE(attribute("test:label", OIIO::TypeString, &label));
        EXPECT_TRUE(label == OIIO::ustringhash("hull"));
        OIIO::ustringhash labels[3] = {sentinel, sentinel, sentinel};
        ASSERT_TRUE(attribute("test:labels", OIIO::TypeDesc(OIIO::TypeDesc::STRING, 2), labels, true));
        EXPECT_TRUE(labels[0] == OIIO::ustringhash("left"));
        EXPECT_TRUE(labels[1] == OIIO::ustringhash("right"));
        EXPECT_TRUE(labels[2] == sentinel);
    }

    TEST_CASE_F(OSLAttributes_UnavailableValuesLeaveDestinationUnchanged, OSLAttributeFixture)
    {
        float sentinel = -1.0f;
        EXPECT_FALSE(attribute("test:id", OIIO::TypeFloat, &sentinel));
        EXPECT_FALSE(attribute("test:missing", OIIO::TypeFloat, &sentinel));
        EXPECT_FALSE(attribute("test:varying", OIIO::TypeFloat, &sentinel));
        EXPECT_FALSE(attribute("test:color", OIIO::TypeVector, &sentinel));
        EXPECT_FALSE(m_renderer_services.get_attribute(
            nullptr, false, OIIO::ustringhash(), OIIO::TypeFloat,
            OIIO::ustringhash("test:roughness"), &sentinel));
        EXPECT_FALSE(m_renderer_services.get_userdata(
            false, OIIO::ustringhash("test:roughness"), OIIO::TypeFloat, nullptr, &sentinel));
        EXPECT_FALSE(m_renderer_services.get_attribute(
            &m_globals, false, OIIO::ustringhash("named_object"), OIIO::TypeFloat,
            OIIO::ustringhash("test:roughness"), &sentinel));
        EXPECT_FEQ(-1.0f, sentinel);
    }

    struct EmptyScene
      : public SceneBase
    {
    };

    typedef FixtureParams<EmptyScene, false> EmptySceneParams;

    TEST_CASE_F(TraceFull_GivenNoOccluder, Fixture<EmptySceneParams>)
    {
        Spectrum transmission;
        ShadingRay ray(
            Vector3d(0.0),
            Vector3d(1.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0);
        const ShadingPoint& shading_point =
            m_tracer.trace_full(
                m_shading_context,
                ray,
                transmission);

        EXPECT_FALSE(shading_point.hit_surface());
        EXPECT_EQ(Spectrum(1.0f), transmission);
    }

    TEST_CASE_F(TraceSimple_GivenNoOccluder, Fixture<EmptySceneParams>)
    {
        ShadingRay ray(
            Vector3d(0.0),
            Vector3d(1.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0);
        Spectrum transmission;
        m_tracer.trace_simple(
            m_shading_context,
            ray,
            transmission);

        EXPECT_EQ(Spectrum(1.0f), transmission);
    }

    TEST_CASE_F(TraceBetweenFull_GivenNoOccluder, Fixture<EmptySceneParams>)
    {
        Spectrum transmission;
        const ShadingPoint& shading_point =
            m_tracer.trace_between_full(
                m_shading_context,
                Vector3d(0.0, 0.0, 0.0),
                Vector3d(5.0, 0.0, 0.0),
                ShadingRay::Time(),
                VisibilityFlags::ShadowRay,
                0,
                transmission);

        EXPECT_FALSE(shading_point.hit_surface());
        EXPECT_EQ(Spectrum(1.0f), transmission);
    }

    TEST_CASE_F(TraceBetweenSimple_GivenNoOccluder, Fixture<EmptySceneParams>)
    {
        Spectrum transmission;
        m_tracer.trace_between_simple(
            m_shading_context,
            Vector3d(0.0, 0.0, 0.0),
            Vector3d(5.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0,
            transmission);

        EXPECT_EQ(Spectrum(1.0f), transmission);
    }

#ifdef APPLESEED_WITH_EMBREE

    typedef FixtureParams<EmptyScene, true> EmptySceneWithEmbreeParams;

    TEST_CASE_F(TraceFull_Embree_GivenNoOccluder, Fixture<EmptySceneWithEmbreeParams>)
    {
        Spectrum transmission;
        ShadingRay ray(
            Vector3d(0.0),
            Vector3d(1.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0);
        const ShadingPoint& shading_point =
            m_tracer.trace_full(
                m_shading_context,
                ray,
                transmission);

        EXPECT_FALSE(shading_point.hit_surface());
        EXPECT_EQ(Spectrum(1.0f), transmission);
    }

    TEST_CASE_F(TraceSimple_Embree_GivenNoOccluder, Fixture<EmptySceneWithEmbreeParams>)
    {
        ShadingRay ray(
            Vector3d(0.0),
            Vector3d(1.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0);
        Spectrum transmission;
        m_tracer.trace_simple(
            m_shading_context,
            ray,
            transmission);

        EXPECT_EQ(Spectrum(1.0f), transmission);
    }

    TEST_CASE_F(TraceBetweenFull_Embree_GivenNoOccluder, Fixture<EmptySceneWithEmbreeParams>)
    {
        Spectrum transmission;
        const ShadingPoint& shading_point =
            m_tracer.trace_between_full(
                m_shading_context,
                Vector3d(0.0, 0.0, 0.0),
                Vector3d(5.0, 0.0, 0.0),
                ShadingRay::Time(),
                VisibilityFlags::ShadowRay,
                0,
                transmission);

        EXPECT_FALSE(shading_point.hit_surface());
        EXPECT_EQ(Spectrum(1.0f), transmission);
    }

    TEST_CASE_F(TraceBetweenSimple_Embree_GivenNoOccluder, Fixture<EmptySceneWithEmbreeParams>)
    {
        Spectrum transmission;
        m_tracer.trace_between_simple(
            m_shading_context,
            Vector3d(0.0, 0.0, 0.0),
            Vector3d(5.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0,
            transmission);

        EXPECT_EQ(Spectrum(1.0f), transmission);
    }
#endif

    struct SceneWithSingleOpaqueOccluder
      : public SceneBase
    {
        SceneWithSingleOpaqueOccluder()
        {
            create_plane_object_instance("plane_inst", Vector3d(2.0, 0.0, 0.0), "opaque_material");
        }
    };

    typedef FixtureParams<SceneWithSingleOpaqueOccluder, false> SceneWithSingleOpaqueOccluderParams;

    TEST_CASE_F(TraceFull_GivenSingleOpaqueOccluder, Fixture<SceneWithSingleOpaqueOccluderParams>)
    {
        Spectrum transmission;
        ShadingRay ray(
            Vector3d(0.0, 0.0, 0.0),
            Vector3d(1.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0);
        const ShadingPoint& shading_point =
            m_tracer.trace_full(
                m_shading_context,
                ray,
                transmission);

        ASSERT_TRUE(shading_point.hit_surface());
        EXPECT_FEQ(Vector3d(2.0, 0.0, 0.0), shading_point.get_point());
        EXPECT_EQ(Spectrum(1.0f), transmission);
    }

    TEST_CASE_F(TraceSimple_GivenSingleOpaqueOccluder, Fixture<SceneWithSingleOpaqueOccluderParams>)
    {
        Spectrum transmission;
        ShadingRay ray(
            Vector3d(0.0, 0.0, 0.0),
            Vector3d(1.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0);
        m_tracer.trace_simple(
            m_shading_context,
            ray,
            transmission);

        EXPECT_EQ(Spectrum(0.0f), transmission);
    }

    TEST_CASE_F(TraceBetweenFull_GivenSingleOpaqueOccluder, Fixture<SceneWithSingleOpaqueOccluderParams>)
    {
        Spectrum transmission;
        const ShadingPoint& shading_point =
            m_tracer.trace_between_full(
                m_shading_context,
                Vector3d(0.0, 0.0, 0.0),
                Vector3d(5.0, 0.0, 0.0),
                ShadingRay::Time(),
                VisibilityFlags::ShadowRay,
                0,
                transmission);

        ASSERT_TRUE(shading_point.hit_surface());
        EXPECT_FEQ(Vector3d(2.0, 0.0, 0.0), shading_point.get_point());
        EXPECT_EQ(Spectrum(1.0f), transmission);
    }

    TEST_CASE_F(TraceBetweenSimple_GivenSingleOpaqueOccluder, Fixture<SceneWithSingleOpaqueOccluderParams>)
    {
        Spectrum transmission;
        m_tracer.trace_between_simple(
            m_shading_context,
            Vector3d(0.0, 0.0, 0.0),
            Vector3d(5.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0,
            transmission);

        EXPECT_EQ(Spectrum(0.0f), transmission);
    }

#ifdef APPLESEED_WITH_EMBREE

    typedef FixtureParams<SceneWithSingleOpaqueOccluder, false> SceneWithSingleOpaqueOccluderWithEmbreeParams;

    TEST_CASE_F(TraceFull_Embree_GivenSingleOpaqueOccluder, Fixture<SceneWithSingleOpaqueOccluderWithEmbreeParams>)
    {
        Spectrum transmission;
        ShadingRay ray(
            Vector3d(0.0, 0.0, 0.0),
            Vector3d(1.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0);
        const ShadingPoint& shading_point =
            m_tracer.trace_full(
                m_shading_context,
                ray,
                transmission);

        ASSERT_TRUE(shading_point.hit_surface());
        EXPECT_FEQ(Vector3d(2.0, 0.0, 0.0), shading_point.get_point());
        EXPECT_EQ(Spectrum(1.0f), transmission);
    }

    TEST_CASE_F(TraceSimple_Embree_GivenSingleOpaqueOccluder, Fixture<SceneWithSingleOpaqueOccluderWithEmbreeParams>)
    {
        Spectrum transmission;
        ShadingRay ray(
            Vector3d(0.0, 0.0, 0.0),
            Vector3d(1.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0);
        m_tracer.trace_simple(
            m_shading_context,
            ray,
            transmission);

        EXPECT_EQ(Spectrum(0.0f), transmission);
    }

    TEST_CASE_F(TraceBetweenFull_Embree_GivenSingleOpaqueOccluder, Fixture<SceneWithSingleOpaqueOccluderWithEmbreeParams>)
    {
        Spectrum transmission;
        const ShadingPoint& shading_point =
            m_tracer.trace_between_full(
                m_shading_context,
                Vector3d(0.0, 0.0, 0.0),
                Vector3d(5.0, 0.0, 0.0),
                ShadingRay::Time(),
                VisibilityFlags::ShadowRay,
                0,
                transmission);

        ASSERT_TRUE(shading_point.hit_surface());
        EXPECT_FEQ(Vector3d(2.0, 0.0, 0.0), shading_point.get_point());
        EXPECT_EQ(Spectrum(1.0f), transmission);
    }

    TEST_CASE_F(TraceBetweenSimple_Embree_GivenSingleOpaqueOccluder, Fixture<SceneWithSingleOpaqueOccluderWithEmbreeParams>)
    {
        Spectrum transmission;
        m_tracer.trace_between_simple(
            m_shading_context,
            Vector3d(0.0, 0.0, 0.0),
            Vector3d(5.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0,
            transmission);

        EXPECT_EQ(Spectrum(0.0f), transmission);
    }

#endif

    struct SceneWithSingleTransparentOccluder
      : public SceneBase
    {
        SceneWithSingleTransparentOccluder()
        {
            create_plane_object_instance("plane_inst", Vector3d(2.0, 0.0, 0.0), "transparent_material");
        }
    };

    typedef FixtureParams<SceneWithSingleTransparentOccluder, false> SceneWithSingleTransparentOccluderParams;

    TEST_CASE_F(TraceFull_GivenSingleTransparentOccluder, Fixture<SceneWithSingleTransparentOccluderParams>)
    {
        Spectrum transmission;
        ShadingRay ray(
            Vector3d(0.0, 0.0, 0.0),
            Vector3d(1.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0);
        const ShadingPoint& shading_point =
            m_tracer.trace_full(
                m_shading_context,
                ray,
                transmission);

        ASSERT_FALSE(shading_point.hit_surface());
        EXPECT_FEQ(Spectrum(0.5f), transmission);
    }

    TEST_CASE_F(TraceSimple_GivenSingleTransparentOccluder, Fixture<SceneWithSingleTransparentOccluderParams>)
    {
        Spectrum transmission;
        ShadingRay ray(
            Vector3d(0.0, 0.0, 0.0),
            Vector3d(1.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0);
        m_tracer.trace_simple(
            m_shading_context,
            ray,
            transmission);

        EXPECT_FEQ(Spectrum(0.5f), transmission);
    }

    TEST_CASE_F(TraceBetweenFull_GivenSingleTransparentOccluder, Fixture<SceneWithSingleTransparentOccluderParams>)
    {
        Spectrum transmission;
        const ShadingPoint& shading_point =
            m_tracer.trace_between_full(
                m_shading_context,
                Vector3d(0.0, 0.0, 0.0),
                Vector3d(5.0, 0.0, 0.0),
                ShadingRay::Time(),
                VisibilityFlags::ShadowRay,
                0,
                transmission);

        ASSERT_FALSE(shading_point.hit_surface());
        EXPECT_FEQ(Spectrum(0.5f), transmission);
    }

    TEST_CASE_F(TraceBetweenSimple_GivenSingleTransparentOccluder, Fixture<SceneWithSingleTransparentOccluderParams>)
    {
        Spectrum transmission;
        m_tracer.trace_between_simple(
            m_shading_context,
            Vector3d(0.0, 0.0, 0.0),
            Vector3d(5.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0,
            transmission);

        EXPECT_FEQ(Spectrum(0.5f), transmission);
    }

#ifdef APPLESEED_WITH_EMBREE

    typedef FixtureParams<SceneWithSingleTransparentOccluder, true> SceneWithSingleTransparentOccluderWithEmbreeParams;

    TEST_CASE_F(TraceFull_Embree_GivenSingleTransparentOccluder, Fixture<SceneWithSingleTransparentOccluderWithEmbreeParams>)
    {
        Spectrum transmission;
        ShadingRay ray(
            Vector3d(0.0, 0.0, 0.0),
            Vector3d(1.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0);
        const ShadingPoint& shading_point =
            m_tracer.trace_full(
                m_shading_context,
                ray,
                transmission);

        ASSERT_FALSE(shading_point.hit_surface());
        EXPECT_FEQ(Spectrum(0.5f), transmission);
    }

    TEST_CASE_F(TraceSimple_Embree_GivenSingleTransparentOccluder, Fixture<SceneWithSingleTransparentOccluderWithEmbreeParams>)
    {
        Spectrum transmission;
        ShadingRay ray(
            Vector3d(0.0, 0.0, 0.0),
            Vector3d(1.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0);
        m_tracer.trace_simple(
            m_shading_context,
            ray,
            transmission);

        EXPECT_FEQ(Spectrum(0.5f), transmission);
    }

    TEST_CASE_F(TraceBetweenFull_Embree_GivenSingleTransparentOccluder, Fixture<SceneWithSingleTransparentOccluderWithEmbreeParams>)
    {
        Spectrum transmission;
        const ShadingPoint& shading_point =
            m_tracer.trace_between_full(
                m_shading_context,
                Vector3d(0.0, 0.0, 0.0),
                Vector3d(5.0, 0.0, 0.0),
                ShadingRay::Time(),
                VisibilityFlags::ShadowRay,
                0,
                transmission);

        ASSERT_FALSE(shading_point.hit_surface());
        EXPECT_FEQ(Spectrum(0.5f), transmission);
    }

    TEST_CASE_F(TraceBetweenSimple_Embree_GivenSingleTransparentOccluder, Fixture<SceneWithSingleTransparentOccluderWithEmbreeParams>)
    {
        Spectrum transmission;
        m_tracer.trace_between_simple(
            m_shading_context,
            Vector3d(0.0, 0.0, 0.0),
            Vector3d(5.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0,
            transmission);

        EXPECT_FEQ(Spectrum(0.5f), transmission);
    }

#endif

    struct SceneWithTransparentThenOpaqueOccluders
      : public SceneBase
    {
        SceneWithTransparentThenOpaqueOccluders()
        {
            create_plane_object_instance("plane_inst1", Vector3d(2.0, 0.0, 0.0), "transparent_material");
            create_plane_object_instance("plane_inst2", Vector3d(4.0, 0.0, 0.0), "opaque_material");
        }
    };

    typedef FixtureParams<SceneWithTransparentThenOpaqueOccluders, false> SceneWithTransparentThenOpaqueOccludersParams;

    TEST_CASE_F(TraceFull_GivenTransparentThenOpaqueOccluders, Fixture<SceneWithTransparentThenOpaqueOccludersParams>)
    {
        Spectrum transmission;
        ShadingRay ray(
            Vector3d(0.0, 0.0, 0.0),
            Vector3d(1.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0);
        const ShadingPoint& shading_point =
            m_tracer.trace_full(
                m_shading_context,
                ray,
                transmission);

        ASSERT_TRUE(shading_point.hit_surface());
        EXPECT_FEQ(Vector3d(4.0, 0.0, 0.0), shading_point.get_point());
        EXPECT_FEQ(Spectrum(0.5f), transmission);
    }

    TEST_CASE_F(TraceSimple_GivenTransparentThenOpaqueOccluders, Fixture<SceneWithTransparentThenOpaqueOccludersParams>)
    {
        Spectrum transmission;
        ShadingRay ray(
            Vector3d(0.0, 0.0, 0.0),
            Vector3d(1.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0);
        m_tracer.trace_simple(
            m_shading_context,
            ray,
            transmission);

        EXPECT_FEQ(Spectrum(0.0f), transmission);
    }

    TEST_CASE_F(TraceBetweenFull_GivenTransparentThenOpaqueOccluders_GivenTargetPastOpaqueOccluder, Fixture<SceneWithTransparentThenOpaqueOccludersParams>)
    {
        Spectrum transmission;
        const ShadingPoint& shading_point =
            m_tracer.trace_between_full(
                m_shading_context,
                Vector3d(0.0, 0.0, 0.0),
                Vector3d(5.0, 0.0, 0.0),
                ShadingRay::Time(),
                VisibilityFlags::ShadowRay,
                0,
                transmission);

        ASSERT_TRUE(shading_point.hit_surface());
        EXPECT_FEQ(Vector3d(4.0, 0.0, 0.0), shading_point.get_point());
        EXPECT_FEQ(Spectrum(0.5f), transmission);
    }

    TEST_CASE_F(TraceBetweenSimple_GivenTransparentThenOpaqueOccluders_GivenTargetPastOpaqueOccluder, Fixture<SceneWithTransparentThenOpaqueOccludersParams>)
    {
        Spectrum transmission;
        m_tracer.trace_between_simple(
            m_shading_context,
            Vector3d(0.0, 0.0, 0.0),
            Vector3d(5.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0,
            transmission);

        EXPECT_FEQ(Spectrum(0.0f), transmission);
    }

    TEST_CASE_F(TraceBetweenFull_GivenTransparentThenOpaqueOccluders_GivenTargetOnOpaqueOccluder, Fixture<SceneWithTransparentThenOpaqueOccludersParams>)
    {
        Spectrum transmission;
        const ShadingPoint& shading_point =
            m_tracer.trace_between_full(
                m_shading_context,
                Vector3d(0.0, 0.0, 0.0),
                Vector3d(4.0, 0.0, 0.0),
                ShadingRay::Time(),
                VisibilityFlags::ShadowRay,
                0,
                transmission);

        ASSERT_FALSE(shading_point.hit_surface());
        EXPECT_FEQ(Spectrum(0.5f), transmission);
    }

    TEST_CASE_F(TraceBetweenSimple_GivenTransparentThenOpaqueOccluders_GivenTargetOnOpaqueOccluder, Fixture<SceneWithTransparentThenOpaqueOccludersParams>)
    {
        Spectrum transmission;
        m_tracer.trace_between_simple(
            m_shading_context,
            Vector3d(0.0, 0.0, 0.0),
            Vector3d(4.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0,
            transmission);

        EXPECT_FEQ(Spectrum(0.5f), transmission);
    }

#ifdef APPLESEED_WITH_EMBREE

    typedef FixtureParams<SceneWithTransparentThenOpaqueOccluders, true> SceneWithTransparentThenOpaqueOccludersWithEmbreeParams;

    TEST_CASE_F(TraceFull_Embree_GivenTransparentThenOpaqueOccluders, Fixture<SceneWithTransparentThenOpaqueOccludersWithEmbreeParams>)
    {
        Spectrum transmission;
        ShadingRay ray(
            Vector3d(0.0, 0.0, 0.0),
            Vector3d(1.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0);
        const ShadingPoint& shading_point =
            m_tracer.trace_full(
                m_shading_context,
                ray,
                transmission);

        ASSERT_TRUE(shading_point.hit_surface());
        EXPECT_FEQ(Vector3f(4.0f, 0.0f, 0.0f), Vector3f(shading_point.get_point()));
        EXPECT_FEQ(Spectrum(0.5f), transmission);
    }

    TEST_CASE_F(TraceSimple_Embree_GivenTransparentThenOpaqueOccluders, Fixture<SceneWithTransparentThenOpaqueOccludersWithEmbreeParams>)
    {
        Spectrum transmission;
        ShadingRay ray(
            Vector3d(0.0, 0.0, 0.0),
            Vector3d(1.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0);
        m_tracer.trace_simple(
            m_shading_context,
            ray,
            transmission);

        EXPECT_FEQ(Spectrum(0.0f), transmission);
    }

    TEST_CASE_F(TraceBetweenFull_Embree_GivenTransparentThenOpaqueOccluders_GivenTargetPastOpaqueOccluder, Fixture<SceneWithTransparentThenOpaqueOccludersWithEmbreeParams>)
    {
        Spectrum transmission;
        const ShadingPoint& shading_point =
            m_tracer.trace_between_full(
                m_shading_context,
                Vector3d(0.0, 0.0, 0.0),
                Vector3d(5.0, 0.0, 0.0),
                ShadingRay::Time(),
                VisibilityFlags::ShadowRay,
                0,
                transmission);

        ASSERT_TRUE(shading_point.hit_surface());
        EXPECT_FEQ(Vector3f(4.0f, 0.0f, 0.0f), Vector3f(shading_point.get_point()));
        EXPECT_FEQ(Spectrum(0.5f), transmission);
    }

    TEST_CASE_F(TraceBetweenSimple_Embree_GivenTransparentThenOpaqueOccluders_GivenTargetPastOpaqueOccluder, Fixture<SceneWithTransparentThenOpaqueOccludersWithEmbreeParams>)
    {
        Spectrum transmission;
        m_tracer.trace_between_simple(
            m_shading_context,
            Vector3d(0.0, 0.0, 0.0),
            Vector3d(5.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0,
            transmission);

        EXPECT_FEQ(Spectrum(0.0f), transmission);
    }

    TEST_CASE_F(TraceBetweenFull_Embree_GivenTransparentThenOpaqueOccluders_GivenTargetOnOpaqueOccluder, Fixture<SceneWithTransparentThenOpaqueOccludersWithEmbreeParams>)
    {
        Spectrum transmission;
        const ShadingPoint& shading_point =
            m_tracer.trace_between_full(
                m_shading_context,
                Vector3d(0.0, 0.0, 0.0),
                Vector3d(4.0, 0.0, 0.0),
                ShadingRay::Time(),
                VisibilityFlags::ShadowRay,
                0,
                transmission);

        ASSERT_FALSE(shading_point.hit_surface());
        EXPECT_FEQ(Spectrum(0.5f), transmission);
    }

    TEST_CASE_F(TraceBetweenSimple_Embree_GivenTransparentThenOpaqueOccluders_GivenTargetOnOpaqueOccluder, Fixture<SceneWithTransparentThenOpaqueOccludersWithEmbreeParams>)
    {
        Spectrum transmission;
        m_tracer.trace_between_simple(
            m_shading_context,
            Vector3d(0.0, 0.0, 0.0),
            Vector3d(4.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0,
            transmission);

        EXPECT_FEQ(Spectrum(0.5f), transmission);
    }

#endif

    struct SceneWithTwoOpaqueOccluders
      : public SceneBase
    {
        SceneWithTwoOpaqueOccluders()
        {
            create_plane_object_instance("plane_inst1", Vector3d(2.0, 0.0, 0.0), "opaque_material");
            create_plane_object_instance("plane_inst2", Vector3d(4.0, 0.0, 0.0), "opaque_material");
        }
    };

    typedef FixtureParams<SceneWithTwoOpaqueOccluders, false> SceneWithTwoOpaqueOccludersParams;

    TEST_CASE_F(TraceBetweenSimple_ComputeVisibilityBetweenTwoOpaqueOccluders_ReturnsOne, Fixture<SceneWithTwoOpaqueOccludersParams>)
    {
        ShadingRay ray(
            Vector3d(0.0, 0.0, 0.0),
            Vector3d(1.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0);
        Spectrum parent_transmission;
        const ShadingPoint& parent_shading_point =
            m_tracer.trace_full(
                m_shading_context,
                ray,
                parent_transmission);

        ASSERT_TRUE(parent_shading_point.hit_surface());
        ASSERT_FEQ(2.0, parent_shading_point.get_distance());

        Spectrum transmission;
        m_tracer.trace_between_simple(
            m_shading_context,
            parent_shading_point,
            Vector3d(4.0, 0.0, 0.0),
            VisibilityFlags::ShadowRay,
            transmission);

        EXPECT_EQ(Spectrum(1.0f), transmission);
    }

#ifdef APPLESEED_WITH_EMBREE

    typedef FixtureParams<SceneWithTwoOpaqueOccluders, true> SceneWithTwoOpaqueOccludersWithEmbreeParams;

    TEST_CASE_F(TraceBetweenSimple_Embree_ComputeVisibilityBetweenTwoOpaqueOccluders_ReturnsOne, Fixture<SceneWithTwoOpaqueOccludersWithEmbreeParams>)
    {
        ShadingRay ray(
            Vector3d(0.0, 0.0, 0.0),
            Vector3d(1.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0);
        Spectrum parent_transmission;
        const ShadingPoint& parent_shading_point =
            m_tracer.trace_full(
                m_shading_context,
                ray,
                parent_transmission);

        ASSERT_TRUE(parent_shading_point.hit_surface());
        ASSERT_FEQ(2.0f, static_cast<float>(parent_shading_point.get_distance()));

        Spectrum transmission;
        m_tracer.trace_between_simple(
            m_shading_context,
            parent_shading_point,
            Vector3d(4.0, 0.0, 0.0),
            VisibilityFlags::ShadowRay,
            transmission);

        EXPECT_EQ(Spectrum(1.0f), transmission);
    }

#endif

    struct SceneWithTwoOpaqueOccludersAndScaledAssemblyInstance
      : public SceneWithTwoOpaqueOccluders
    {
        SceneWithTwoOpaqueOccludersAndScaledAssemblyInstance()
        {
            m_assembly_instance->transform_sequence().set_transform(
                0.0f,
                Transformd::from_local_to_parent(
                    Matrix4d::make_scaling(Vector3d(0.5))));
        }
    };

    typedef FixtureParams<SceneWithTwoOpaqueOccludersAndScaledAssemblyInstance, false> SceneWithTwoOpaqueOccludersAndScaledAssemblyInstanceParams;

    TEST_CASE_F(TraceBetweenSimple_ComputeVisibilityBetweenTwoOpaqueOccludersAndScaledAssemblyInstance_ReturnsOne, Fixture<SceneWithTwoOpaqueOccludersAndScaledAssemblyInstanceParams>)
    {
        ShadingRay ray(
            Vector3d(0.0, 0.0, 0.0),
            Vector3d(1.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0);
        Spectrum parent_transmission;
        const ShadingPoint& parent_shading_point =
            m_tracer.trace_full(
                m_shading_context,
                ray,
                parent_transmission);

        ASSERT_TRUE(parent_shading_point.hit_surface());
        ASSERT_FEQ(1.0, parent_shading_point.get_distance());

        Spectrum transmission;
        m_tracer.trace_between_simple(
            m_shading_context,
            parent_shading_point,
            Vector3d(2.0, 0.0, 0.0),
            VisibilityFlags::ShadowRay,
            transmission);

        EXPECT_EQ(Spectrum(1.0f), transmission);
    }

#ifdef APPLESEED_WITH_EMBREE

    typedef FixtureParams<SceneWithTwoOpaqueOccludersAndScaledAssemblyInstance, true> SceneWithTwoOpaqueOccludersAndScaledAssemblyInstanceWithEmbreeParams;

    TEST_CASE_F(TraceBetweenSimple_Embree_ComputeVisibilityBetweenTwoOpaqueOccludersAndScaledAssemblyInstance_ReturnsOne, Fixture<SceneWithTwoOpaqueOccludersAndScaledAssemblyInstanceWithEmbreeParams>)
    {
        ShadingRay ray(
            Vector3d(0.0, 0.0, 0.0),
            Vector3d(1.0, 0.0, 0.0),
            ShadingRay::Time(),
            VisibilityFlags::ShadowRay,
            0);
        Spectrum parent_transmission;
        const ShadingPoint& parent_shading_point =
            m_tracer.trace_full(
                m_shading_context,
                ray,
                parent_transmission);

        ASSERT_TRUE(parent_shading_point.hit_surface());
        ASSERT_FEQ(1.0f, static_cast<float>(parent_shading_point.get_distance()));

        Spectrum transmission;
        m_tracer.trace_between_simple(
            m_shading_context,
            parent_shading_point,
            Vector3d(2.0, 0.0, 0.0),
            VisibilityFlags::ShadowRay,
            transmission);

        EXPECT_EQ(Spectrum(1.0f), transmission);
    }

#endif

}
