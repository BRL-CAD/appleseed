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
#include "renderer/global/globaltypes.h"
#include "renderer/kernel/intersection/intersector.h"
#include "renderer/kernel/intersection/tracecontext.h"
#include "renderer/kernel/lighting/tracer.h"
#include "renderer/kernel/rendering/rendererservices.h"
#include "renderer/kernel/shading/closures.h"
#include "renderer/kernel/shading/oslshadergroupexec.h"
#include "renderer/kernel/shading/oslshadingsystem.h"
#include "renderer/kernel/shading/shadingcontext.h"
#include "renderer/kernel/shading/shadingpoint.h"
#include "renderer/kernel/texturing/oiiotexturesystem.h"
#include "renderer/kernel/texturing/texturecache.h"
#include "renderer/kernel/texturing/texturestore.h"
#include "renderer/modeling/bsdf/glassbsdf.h"
#include "renderer/modeling/camera/pinholecamera.h"
#include "renderer/modeling/color/colorspace.h"
#include "renderer/modeling/edf/diffuseedf.h"
#include "renderer/modeling/edf/edf.h"
#include "renderer/modeling/edf/osledf.h"
#include "renderer/modeling/frame/frame.h"
#include "renderer/modeling/material/genericmaterial.h"
#include "renderer/modeling/material/oslmaterial.h"
#include "renderer/modeling/object/sphereobject.h"
#include "renderer/modeling/scene/assembly.h"
#include "renderer/modeling/scene/assemblyinstance.h"
#include "renderer/modeling/scene/objectinstance.h"
#include "renderer/modeling/scene/scene.h"
#include "renderer/modeling/shadergroup/shadercompiler.h"
#include "renderer/modeling/shadergroup/shadergroup.h"
#include "renderer/utility/iostreamop.h"
#include "renderer/utility/paramarray.h"
#include "renderer/utility/testutils.h"

// appleseed.foundation headers.
#include "foundation/containers/dictionary.h"
#include "foundation/math/matrix.h"
#include "foundation/math/rng/xoroshiro128plus.h"
#include "foundation/math/scalar.h"
#include "foundation/memory/arena.h"
#include "foundation/memory/autoreleaseptr.h"
#include "foundation/utility/test.h"

// OSL headers.
#include "foundation/platform/_beginoslheaders.h"
#include "OSL/oslclosure.h"
#include "foundation/platform/_endoslheaders.h"

// Standard headers.
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

using namespace foundation;
using namespace renderer;

TEST_SUITE(Renderer_Modeling_EDF_OSL)
{
    enum class Profile { Angular, Constant, Subnormal, Empty, Position, RayType, PathContext, Footprint, OpticalContext };

    template <Profile EmissionProfile, bool Enclosed = false, bool MixedHost = false>
    struct Fixture : TestSceneBase
    {
        std::shared_ptr<OIIOTextureSystem> m_textures;
        RendererServices m_services;
        auto_release_ptr<OSLShadingSystem> m_system;
        TraceContext m_trace;
        TextureStore m_store;
        TextureCache m_cache;
        Intersector m_intersector;
        Arena m_arena;
        OSLShaderGroupExec m_exec;
        Tracer m_tracer;
        ShadingContext m_context;
        std::unique_ptr<TestSceneContext> m_scene_context;
        ShaderGroup* m_group = nullptr;
        ShaderGroup* m_host_group = nullptr;
        EDF* m_edf = nullptr;
        ShadingPoint m_point;
        Xoroshiro128plus m_rng;
        SamplingContext m_sampling;

        Fixture()
          : m_textures(OIIOTextureSystemFactory::create())
          , m_services(m_project, *m_textures)
          , m_system(OSLShadingSystemFactory::create(&m_services, m_textures.get()))
          , m_trace(m_scene)
          , m_store(m_scene)
          , m_cache(m_store)
          , m_intersector(m_trace, m_cache)
          , m_exec(*m_system, m_arena)
          , m_tracer(m_scene, m_intersector, m_exec, 0.001f, 1000, false)
          , m_context(m_intersector, m_tracer, m_cache, *m_textures, m_exec, m_arena, 0)
          , m_sampling(m_rng, SamplingContext::RNGMode)
        {
            m_scene.cameras().insert(PinholeCameraFactory().create("camera", ParamArray()
                .insert("film_width", "0.025").insert("film_height", "0.025")
                .insert("focal_length", "0.035")));
            m_project.set_frame(FrameFactory::create("frame", ParamArray()
                .insert("resolution", "4 4").insert("camera", "camera")));
            m_scene.assemblies().insert(AssemblyFactory().create("assembly", ParamArray()));
            Assembly* assembly = m_scene.assemblies().get_by_name("assembly");
            m_scene.assembly_instances().insert(AssemblyInstanceFactory::create(
                "instance", ParamArray(), "assembly"));
            m_system->attribute("raytypes", OSL::TypeDesc(OSL::TypeDesc::STRING,
                static_cast<int>(VisibilityFlags::Count)), VisibilityFlags::Names);
            register_closures(*m_system);
            auto group = ShaderGroupFactory::create("emission");
            std::string expression;
            std::string setup;
            switch (EmissionProfile)
            {
              case Profile::Angular:
                // The normal is deliberately dark while oblique directions emit.
                expression = "(abs(dot(normalize(Ng),-normalize(I)))<0.95 ? color(0.25,0.5,0.75) : color(0))";
                break;
              case Profile::Constant:
                expression = "color(0.25,0.5,0.75)";
                break;
              case Profile::Subnormal:
                // Normal operands produce a valid subnormal texture-like weight.
                expression = "color(0.25,0.5,1) * (1e-20 * 1e-20)";
                break;
              case Profile::Empty:
                expression = "color(0)";
                break;
              case Profile::Position:
                expression = "color(P[1],0.5,abs(dot(normalize(N),-normalize(I))))";
                break;
              case Profile::RayType:
                expression = "(raytype(\"light\") ? color(0.25,0.5,0.75) : color(1))";
                break;
              case Profile::PathContext:
                setup = "float ray_length=0; int depth=0; getattribute(\"path:ray_length\",ray_length); "
                    "getattribute(\"path:ray_depth\",depth); ";
                expression = "color(ray_length,depth,raytype(\"light\") ? 0.5 : 1)";
                break;
              case Profile::Footprint:
                expression = "color(length(Dx(P)),length(Dy(P)),0.5)";
                break;
              case Profile::OpticalContext:
                setup = "float ior=0; getattribute(\"path:ray_ior\",ior); ";
                expression = "color(ior,0,raytype(\"light\") ? 0.5 : 1)";
                break;
            }
            const std::string source = "surface emission_test() { " + setup + "Ci = " + expression + " * emission(); }";
            group->add_source_shader("surface", "emission_test", "surface", source.c_str(), ParamArray());
            auto compiler = ShaderCompilerFactory::create("../shaders/stdosl.h");
            if (!group->create_optimized_osl_shader_group(*m_system, compiler.get()))
                throw std::runtime_error("Could not compile the emission fixture shader");
            m_group = group.get();
            assembly->shader_groups().insert(group);
            assembly->materials().insert(OSLMaterialFactory().create("material",
                ParamArray().insert("osl_surface", "emission")));
            assembly->edfs().insert(OSLEDFFactory().create());
            assembly->edfs().insert(DiffuseEDFFactory().create("native_edf",
                ParamArray().insert("radiance", "2.0")));
            m_edf = assembly->edfs().get_by_name("osl_edf");
            assembly->objects().insert(SphereObjectFactory().create("sphere", ParamArray()));
            const StringDictionary mapping = StringDictionary().insert("default", "material");
            assembly->object_instances().insert(ObjectInstanceFactory::create("sphere_instance",
                ParamArray(), "sphere", Transformd::identity(), mapping, mapping));
            if (Enclosed)
            {
                const double HostRadius = 5.0;
                if (MixedHost)
                {
                    auto host_group = ShaderGroupFactory::create("host_shader");
                    const char* host_source =
                        "closure color as_glass(normal n, vector t, color s, color r, color f, "
                        "float a, float b, float i, color v, float d) [[ int builtin=1 ]]; "
                        "surface host_test() { "
                        "Ci=0.5*as_glass(N,vector(0),color(1),color(1),color(1),0,0,1.2,color(1),0) "
                        "+0.5*as_glass(N,vector(0),color(1),color(1),color(1),0,0,1.8,color(1),0); }";
                    host_group->add_source_shader("surface", "host_test", "surface", host_source, ParamArray());
                    if (!host_group->create_optimized_osl_shader_group(*m_system, compiler.get()))
                        throw std::runtime_error("Could not compile the mixed-IOR host shader");
                    m_host_group = host_group.get();
                    assembly->shader_groups().insert(host_group);
                    assembly->materials().insert(OSLMaterialFactory().create("host_material",
                        ParamArray().insert("osl_surface", "host_shader")));
                }
                else
                {
                    assembly->bsdfs().insert(GlassBSDFFactory().create("glass",
                        ParamArray().insert("surface_transmittance", "1.0").insert("ior", "1.5")
                            .insert("volume_parameterization", "transmittance")));
                    assembly->materials().insert(GenericMaterialFactory().create("host_material",
                        ParamArray().insert("bsdf", "glass")));
                }
                const StringDictionary host_mapping = StringDictionary().insert("default", "host_material");
                assembly->object_instances().insert(ObjectInstanceFactory::create("host_instance",
                    ParamArray(), "sphere", Transformd(Matrix4d::make_scaling(Vector3d(HostRadius))),
                    host_mapping, host_mapping));
            }
            m_scene_context.reset(new TestSceneContext(*this));
            m_services.initialize(m_store);
            m_trace.update();
            // A nonzero original hit distance also checks that resampling keeps P.
            ShadingRay ray(Vector3d(0.0, 3.0, 0.0), Vector3d(0.0, -1.0, 0.0),
                ShadingRay::Time::create_with_normalized_time(0.0f, 0.0f, 1.0f),
                VisibilityFlags::CameraRay, 0);
            m_intersector.trace(ray, m_point);
            if (!m_point.hit_surface())
                throw std::runtime_error("Emission fixture ray missed its sphere");
        }

        ~Fixture()
        {
            // Finish scene hooks and release compiled groups while OSL is alive.
            m_scene_context.reset();
            m_group->release_optimized_osl_shader_group();
            if (m_host_group)
                m_host_group->release_optimized_osl_shader_group();
        }

        const void* inputs(const bool camera = false)
        {
            if (camera)
                m_context.execute_osl_shading(*m_group, m_point);
            else m_context.execute_osl_emission(*m_group, m_point);
            return m_edf->evaluate_inputs(m_context, m_point);
        }

        void sample(const void* data, Vector3f& outgoing, Spectrum& value, float& pdf)
        {
            // This cosine proposal has outgoing cosine one half.
            m_edf->sample(m_sampling, data, Vector3f(m_point.get_geometric_normal()),
                Basis3f(m_point.get_shading_basis()), Vector2f(0.4f, 0.75f), outgoing, value, pdf);
        }

        void sample_light_ray(ShadingRay& ray, Spectrum& value, float& pdf)
        {
            m_edf->sample_light_ray(m_sampling, m_context, m_point,
                Vector3f(m_point.get_geometric_normal()), Basis3f(m_point.get_shading_basis()),
                Vector2f(0.4f, 0.75f), ray, value, pdf);
        }

        Spectrum radiance(const Color3f& color)
        {
            Spectrum result;
            result.set(color, g_std_lighting_conditions, Spectrum::Illuminance);
            return result;
        }
    };

    using AngularFixture = Fixture<Profile::Angular>;
    using ConstantFixture = Fixture<Profile::Constant>;
    using SubnormalFixture = Fixture<Profile::Subnormal>;
    using EmptyFixture = Fixture<Profile::Empty>;
    using PositionFixture = Fixture<Profile::Position>;
    using RayTypeFixture = Fixture<Profile::RayType>;
    using PathContextFixture = Fixture<Profile::PathContext>;
    using FootprintFixture = Fixture<Profile::Footprint>;
    using VacuumFixture = Fixture<Profile::OpticalContext>;
    using EnclosedFixture = Fixture<Profile::OpticalContext, true>;
    using MixedHostFixture = Fixture<Profile::OpticalContext, true, true>;

    TEST_CASE_F(DarkNormalDoesNotRemoveOtherEmissionDirections, AngularFixture)
    {
        const void* data = inputs();
        Vector3f outgoing;
        Spectrum value;
        float pdf;
        sample(data, outgoing, value, pdf);
        EXPECT_FEQ(radiance(Color3f(0.25f, 0.5f, 0.75f)), value);
        EXPECT_TRUE(is_normalized(outgoing));
        EXPECT_FEQ(0.5f * RcpPi<float>(), pdf);
        Spectrum evaluated;
        float evaluated_pdf;
        m_edf->evaluate(data, Vector3f(m_point.get_geometric_normal()),
            Basis3f(m_point.get_shading_basis()), outgoing, evaluated, evaluated_pdf);
        EXPECT_FEQ(value, evaluated);
        EXPECT_FEQ(pdf, evaluated_pdf);
        EXPECT_FEQ(pdf, m_edf->evaluate_pdf(data, Vector3f(m_point.get_geometric_normal()),
            Basis3f(m_point.get_shading_basis()), outgoing));
    }

    TEST_CASE_F(SubnormalEmissionRetainsFiniteRadianceAndNormalizedSelection, SubnormalFixture)
    {
        constexpr float Tiny = 1.0e-20f * 1.0e-20f;
        constexpr double AbsoluteTolerance = 8.0 * std::numeric_limits<float>::denorm_min();
        constexpr double RelativeTolerance = 1.0e-5;
        const Spectrum expected = radiance(Color3f(0.25f * Tiny, 0.5f * Tiny, Tiny));
        const auto check = [&](const Spectrum& value)
        {
            for (std::size_t i = 0; i < 3; ++i)
            {
                EXPECT_TRUE(std::isfinite(value[i]));
                EXPECT_TRUE(value[i] > 0.0f);
                EXPECT_TRUE(std::abs(double(value[i]) - expected[i]) <=
                    AbsoluteTolerance + RelativeTolerance * std::abs(double(expected[i])));
            }
        };
        const void* data = inputs();
        Arena arena;
        CompositeEmissionClosure closure(Basis3f(m_point.get_shading_basis()),
            m_point.get_osl_shader_globals().Ci, arena);
        ASSERT_EQ(std::size_t(1), closure.get_closure_count());
        EXPECT_EQ(1.0f, closure.get_closure_pdf(0));
        Vector3f outgoing;
        Spectrum sampled;
        float sampled_pdf;
        sample(data, outgoing, sampled, sampled_pdf);
        check(sampled);
        EXPECT_FEQ(0.5f * RcpPi<float>(), sampled_pdf);
        Spectrum evaluated;
        float evaluated_pdf;
        m_edf->evaluate(data, Vector3f(m_point.get_geometric_normal()),
            Basis3f(m_point.get_shading_basis()), outgoing, evaluated, evaluated_pdf);
        check(evaluated);
        EXPECT_FEQ(sampled_pdf, evaluated_pdf);
        ShadingRay ray;
        Spectrum launched;
        float launch_pdf;
        sample_light_ray(ray, launched, launch_pdf);
        check(launched);
        EXPECT_TRUE(std::isfinite(launch_pdf) && launch_pdf > 0.0f);
    }

    TEST_CASE_F(EmissionSelectionAndChromaticityAreInvariantUnderFiniteWeightScaling, ConstantFixture)
    {
        for (const float scale : {std::numeric_limits<float>::denorm_min(),
            1.0e-40f, 1.0f, std::numeric_limits<float>::max()})
        {
            OSL::ClosureComponent red = {}, green = {};
            red.id = green.id = EmissionID;
            red.w = OSL::Vec3(scale, 0.0f, 0.0f);
            green.w = OSL::Vec3(0.0f, scale, 0.0f);
            OSL::ClosureAdd sum = {};
            sum.id = OSL::ClosureColor::ADD;
            sum.closureA = &red;
            sum.closureB = &green;
            Arena arena;
            CompositeEmissionClosure closure(Basis3f(m_point.get_shading_basis()), &sum, arena);
            ASSERT_EQ(std::size_t(2), closure.get_closure_count());
            for (std::size_t i = 0; i < 2; ++i)
            {
                EXPECT_EQ(0.5f, closure.get_closure_pdf(i));
                const auto* values = static_cast<const DiffuseEDFInputValues*>(closure.get_closure_input_values(i));
                EXPECT_EQ(scale, values->m_radiance_multiplier);
                const Spectrum expected = radiance(i == 0 ? Color3f(1.0f, 0.0f, 0.0f) : Color3f(0.0f, 1.0f, 0.0f));
                for (std::size_t channel = 0; channel < 3; ++channel)
                {
                    EXPECT_TRUE(std::isfinite(values->m_radiance[channel]));
                    EXPECT_FEQ(expected[channel], values->m_radiance[channel]);
                }
            }
        }
    }

    TEST_CASE_F(SamplingSumsWeightedEmissionClosures, ConstantFixture)
    {
        ShadingRay ray = m_point.get_ray();
        ray.m_org = m_point.get_point();
        ray.m_tmin = ray.m_tmax = 0.0;
        ray.m_flags = VisibilityFlags::LightRay;
        m_point.set_ray(ray);
        m_context.execute_osl_emission(*m_group, m_point);
        // Preserve the constant shader's value while forcing an unmerged tree.
        // Selection among its leaves must not square their radiance weights.
        OSL::ClosureComponent red = {};
        red.id = EmissionID;
        red.w = OSL::Vec3(0.25f, 0.0f, 0.0f);
        OSL::ClosureComponent cyan = {};
        cyan.id = EmissionID;
        cyan.w = OSL::Vec3(0.0f, 0.5f, 0.75f);
        OSL::ClosureAdd sum = {};
        sum.id = OSL::ClosureColor::ADD;
        sum.closureA = &red;
        sum.closureB = &cyan;
        m_point.get_osl_shader_globals().Ci = &sum;
        const void* data = m_edf->evaluate_inputs(m_context, m_point);
        Vector3f outgoing;
        Spectrum value;
        float pdf;
        m_edf->sample(m_sampling, data, Vector3f(m_point.get_geometric_normal()),
            Basis3f(m_point.get_shading_basis()), Vector2f(0.0f), outgoing, value, pdf);
        EXPECT_FEQ(radiance(Color3f(0.25f, 0.5f, 0.75f)), value);
        EXPECT_FEQ(RcpPi<float>(), pdf);
    }

    TEST_CASE_F(OutsideTheEmissionHemisphereHasZeroRadianceAndDensity, ConstantFixture)
    {
        const void* data = inputs();
        const Vector3f normal(m_point.get_geometric_normal());
        const Basis3f basis(m_point.get_shading_basis());
        Spectrum value(1.0f);
        float pdf = -1.0f;
        m_edf->evaluate(data, normal, basis, -normal, value, pdf);
        EXPECT_FEQ(Spectrum(0.0f), value);
        EXPECT_FEQ(0.0f, pdf);
        EXPECT_FEQ(0.0f, m_edf->evaluate_pdf(data, normal, basis, -normal));
    }

    TEST_CASE_F(AngularHemisphereFluxMatchesItsIndependentIntegral, AngularFixture)
    {
        const Basis3f basis(m_point.get_shading_basis());
        const Vector3f normal(m_point.get_geometric_normal());
        const size_t SampleCount = 16384;
        Spectrum flux(0.0f);
        for (size_t i = 0; i < SampleCount; ++i)
        {
            // Each emitted photon owns one scratch arena lifetime.
            m_arena.clear();
            const void* data = inputs();
            m_sampling.split_in_place(2, 1);
            Vector3f outgoing;
            Spectrum value;
            float pdf;
            m_edf->sample(m_sampling, data, normal, basis,
                m_sampling.next2<Vector2f>(), outgoing, value, pdf);
            flux += value * (dot(outgoing, basis.get_normal()) / pdf);
        }
        flux /= static_cast<float>(SampleCount);
        // Integral of L*cos(theta) on cos(theta)<c is pi*c^2*L.
        const float NormalConeCosine = 0.95f;
        const Spectrum expected = radiance(Color3f(0.25f, 0.5f, 0.75f)) *
            (Pi<float>() * square(NormalConeCosine));
        const float RelativeFluxBound = 0.01f;
        const Spectrum error = flux - expected;
        EXPECT_TRUE(std::abs(error[max_abs_index(error)]) <= RelativeFluxBound * max_value(expected));
    }

    TEST_CASE_F(EmptyEmissionHasFiniteBlackSamplesAndAConsistentProposal, EmptyFixture)
    {
        const void* data = inputs();
        Vector3f outgoing;
        Spectrum value(1.0f);
        float pdf = -1.0f;
        sample(data, outgoing, value, pdf);
        EXPECT_FEQ(Spectrum(0.0f), value);
        EXPECT_TRUE(is_normalized(outgoing));
        EXPECT_TRUE(std::isfinite(pdf) && pdf > 0.0f);
        EXPECT_FEQ(pdf, m_edf->evaluate_pdf(data, Vector3f(m_point.get_geometric_normal()),
            Basis3f(m_point.get_shading_basis()), outgoing));
    }

    TEST_CASE_F(ResamplingPreservesTheEmitterPosition, PositionFixture)
    {
        const void* data = inputs();
        Vector3f outgoing;
        Spectrum value;
        float pdf;
        sample(data, outgoing, value, pdf);
        EXPECT_FEQ(radiance(Color3f(1.0f, 0.5f, 0.5f)), value);
        EXPECT_FEQ(0.5f * RcpPi<float>(), pdf);
    }

    TEST_CASE_F(ResamplingPreservesTheShadingFrame, PositionFixture)
    {
        m_point.set_shading_basis(Basis3d(Vector3d(0.6, 0.8, 0.0)));
        const void* data = inputs();
        Vector3f outgoing;
        Spectrum value;
        float pdf;
        sample(data, outgoing, value, pdf);
        EXPECT_FEQ(radiance(Color3f(1.0f, 0.5f, 0.5f)), value);
        EXPECT_FEQ(0.5f * RcpPi<float>(), pdf);
    }

    TEST_CASE_F(EvaluationUsesTheRequestedShadingFrame, PositionFixture)
    {
        const void* data = inputs();
        const Basis3f tilted(Vector3f(0.6f, 0.8f, 0.0f));
        Spectrum value;
        float pdf;
        m_edf->evaluate(data, Vector3f(m_point.get_geometric_normal()),
            tilted, Vector3f(0.0f, 1.0f, 0.0f), value, pdf);
        EXPECT_FEQ(radiance(Color3f(1.0f, 0.5f, 0.8f)), value);
        EXPECT_FEQ(0.8f * RcpPi<float>(), pdf);
    }

    TEST_CASE_F(EmissionLaunchResetsPathMetadataWithoutChangingVisibleEvaluation, PathContextFixture)
    {
        ShadingRay ray = m_point.get_ray();
        ray.m_depth = 3;
        m_point.set_ray(ray);
        const void* data = inputs(true);
        Vector3f outgoing;
        Spectrum value;
        float pdf;
        sample(data, outgoing, value, pdf);
        EXPECT_FEQ(radiance(Color3f(0.0f, 0.0f, 0.5f)), value);
        m_edf->evaluate(data, Vector3f(m_point.get_geometric_normal()),
            Basis3f(m_point.get_shading_basis()), outgoing, value);
        EXPECT_FEQ(radiance(Color3f(2.0f, 3.0f, 1.0f)), value);
    }

    TEST_CASE_F(VisibleEvaluationRetainsItsFootprintWhileLaunchesHaveNone, FootprintFixture)
    {
        ShadingRay ray = m_point.get_ray();
        ray.m_has_differentials = true;
        ray.m_rx_org = ray.m_org + Vector3d(0.1, 0.0, 0.0);
        ray.m_ry_org = ray.m_org + Vector3d(0.0, 0.0, 0.2);
        ray.m_rx_dir = ray.m_ry_dir = ray.m_dir;
        m_point.set_ray(ray);
        const void* data = inputs(true);
        Vector3f outgoing;
        Spectrum value;
        float pdf;
        sample(data, outgoing, value, pdf);
        EXPECT_FEQ(radiance(Color3f(0.0f, 0.0f, 0.5f)), value);
        m_edf->evaluate(data, Vector3f(m_point.get_geometric_normal()),
            Basis3f(m_point.get_shading_basis()), outgoing, value);
        EXPECT_FEQ(radiance(Color3f(0.1f, 0.2f, 0.5f)), value);
    }

    TEST_CASE_F(EmissionSamplingPreservesTheVisibleEvaluationContext, RayTypeFixture)
    {
        const void* data = inputs(true);
        Vector3f outgoing;
        Spectrum value;
        float pdf;
        sample(data, outgoing, value, pdf);
        EXPECT_FEQ(radiance(Color3f(0.25f, 0.5f, 0.75f)), value);
        Spectrum visible;
        m_edf->evaluate(data, Vector3f(m_point.get_geometric_normal()),
            Basis3f(m_point.get_shading_basis()), outgoing, visible);
        EXPECT_FEQ(Spectrum(1.0f), visible);
    }

    TEST_CASE_F(PhysicalLaunchDiscoversTheShaderAndRayMediumTogether, EnclosedFixture)
    {
        ShadingRay ray;
        Spectrum value;
        float pdf;
        sample_light_ray(ray, value, pdf);
        EXPECT_FEQ(radiance(Color3f(1.5f, 0.0f, 0.5f)), value);
        ASSERT_EQ(1, ray.m_medium_count);
        EXPECT_FEQ(1.5f, ray.get_current_ior());
        EXPECT_EQ(m_scene.assemblies().get_by_name("assembly")->object_instances().get_by_name("host_instance"),
            ray.get_current_medium()->m_object_instance);
        EXPECT_EQ(0, m_point.get_ray().m_medium_count);
        EXPECT_FEQ(0.5f * RcpPi<float>(), pdf);
    }

    TEST_CASE_F(PhysicalLaunchRetainsVacuumWhenNoHostContainsIt, VacuumFixture)
    {
        ShadingRay ray;
        Spectrum value;
        float pdf;
        sample_light_ray(ray, value, pdf);
        EXPECT_FEQ(radiance(Color3f(1.0f, 0.0f, 0.5f)), value);
        EXPECT_EQ(0, ray.m_medium_count);
        EXPECT_FEQ(1.0f, ray.get_current_ior());
    }

    TEST_CASE_F(PhysicalLaunchNeedsNoProvisionalEmissionAndRetainsItsGeometryTime, AngularFixture)
    {
        ShadingRay source_ray = m_point.get_ray();
        source_ray.m_time = ShadingRay::Time::create_with_normalized_time(0.375f, 2.0f, 6.0f);
        source_ray.m_depth = 3;
        m_point.set_ray(source_ray);
        ShadingRay ray;
        Spectrum value;
        float pdf;
        sample_light_ray(ray, value, pdf);
        EXPECT_FEQ(radiance(Color3f(0.25f, 0.5f, 0.75f)), value);
        EXPECT_FEQ(m_point.get_point(), ray.m_org);
        EXPECT_TRUE(is_normalized(ray.m_dir));
        EXPECT_FEQ(0.375f, ray.m_time.m_normalized);
        EXPECT_FEQ(3.5f, ray.m_time.m_absolute);
        EXPECT_EQ(VisibilityFlags::LightRay, ray.m_flags);
        EXPECT_EQ(0, ray.m_depth);
        EXPECT_FALSE(ray.m_has_differentials);
        EXPECT_FEQ(0.0, ray.m_tmin);
        EXPECT_FEQ(0.5f * RcpPi<float>(), pdf);
        EXPECT_EQ(3, m_point.get_ray().m_depth);
    }

    TEST_CASE_F(PhysicalLaunchKeepsSurfacePositionAndDirectionDependentShading, PositionFixture)
    {
        ShadingRay ray;
        Spectrum value;
        float pdf;
        sample_light_ray(ray, value, pdf);
        EXPECT_FEQ(radiance(Color3f(1.0f, 0.5f, 0.5f)), value);
    }

    TEST_CASE_F(PhysicalLaunchClearsTheCameraFootprint, FootprintFixture)
    {
        ShadingRay source_ray = m_point.get_ray();
        source_ray.m_has_differentials = true;
        source_ray.m_rx_org = source_ray.m_org + Vector3d(0.1, 0.0, 0.0);
        source_ray.m_ry_org = source_ray.m_org + Vector3d(0.0, 0.0, 0.2);
        source_ray.m_rx_dir = source_ray.m_ry_dir = source_ray.m_dir;
        m_point.set_ray(source_ray);
        ShadingRay ray;
        Spectrum value;
        float pdf;
        sample_light_ray(ray, value, pdf);
        EXPECT_FEQ(radiance(Color3f(0.0f, 0.0f, 0.5f)), value);
        EXPECT_FALSE(ray.m_has_differentials);
    }


    TEST_CASE_F(PhysicalLaunchUsesOneStochasticIorForShaderAndTransport, MixedHostFixture)
    {
        const size_t IorSamples = 64;
        bool lower_seen = false, higher_seen = false;
        for (size_t i = 0; i < IorSamples; ++i)
        {
            ShadingRay ray;
            Spectrum value;
            float pdf;
            sample_light_ray(ray, value, pdf);
            ASSERT_EQ(1, ray.m_medium_count);
            const float ior = ray.get_current_ior();
            EXPECT_FEQ(radiance(Color3f(ior, 0.0f, 0.5f)), value);
            if (ior < 1.5f)
            {
                EXPECT_FEQ(1.2f, ior);
                lower_seen = true;
            }
            else
            {
                EXPECT_FEQ(1.8f, ior);
                higher_seen = true;
            }
        }
        EXPECT_TRUE(lower_seen && higher_seen);
    }

    TEST_CASE_F(PhysicalLaunchShaderStartsAtZeroLengthAndDepth, PathContextFixture)
    {
        ShadingRay source_ray = m_point.get_ray();
        source_ray.m_depth = 3;
        m_point.set_ray(source_ray);
        ShadingRay ray;
        Spectrum value;
        float pdf;
        sample_light_ray(ray, value, pdf);
        EXPECT_FEQ(radiance(Color3f(0.0f, 0.0f, 0.5f)), value);
    }

    TEST_CASE_F(NativeEdfsUseTheSamePhysicalLaunchContract, ConstantFixture)
    {
        const EDF* native = m_scene.assemblies().get_by_name("assembly")->edfs().get_by_name("native_edf");
        ShadingRay ray;
        Spectrum value;
        float pdf;
        native->sample_light_ray(m_sampling, m_context, m_point,
            Vector3f(m_point.get_geometric_normal()), Basis3f(m_point.get_shading_basis()),
            Vector2f(0.4f, 0.75f), ray, value, pdf);
        EXPECT_FEQ(Spectrum(2.0f), value);
        EXPECT_EQ(0, ray.m_medium_count);
        EXPECT_EQ(VisibilityFlags::LightRay, ray.m_flags);
        EXPECT_EQ(0, ray.m_depth);
        EXPECT_FEQ(m_point.get_point(), ray.m_org);
        EXPECT_FEQ(0.5f * RcpPi<float>(), pdf);
    }

}
