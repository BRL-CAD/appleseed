
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
#include "renderer/kernel/aov/aovaccumulator.h"
#include "renderer/kernel/aov/aovcomponents.h"
#include "renderer/kernel/rendering/pixelcontext.h"
#include "renderer/kernel/shading/shadingcomponents.h"
#include "renderer/kernel/shading/shadingresult.h"
#include "renderer/modeling/aov/aov.h"
#include "renderer/modeling/aov/aovcontainer.h"
#include "renderer/modeling/aov/depthaov.h"
#include "renderer/modeling/frame/frame.h"
#include "renderer/modeling/camera/camera.h"
#include "renderer/modeling/camera/orthographiccamera.h"
#include "renderer/modeling/project/project.h"
#include "renderer/global/globaltypes.h"
#include "renderer/kernel/intersection/intersector.h"
#include "renderer/kernel/lighting/forwardlightsampler.h"
#include "renderer/kernel/lighting/backwardlightsampler.h"
#include "renderer/kernel/lighting/lighttypes.h"
#include "renderer/kernel/lighting/lightsample.h"
#include "renderer/kernel/lighting/pathvertex.h"
#include "renderer/kernel/intersection/tracecontext.h"
#include "renderer/kernel/shading/shadingpoint.h"
#include "renderer/kernel/shading/shadingray.h"
#include "renderer/kernel/texturing/texturecache.h"
#include "renderer/kernel/texturing/texturestore.h"
#include "renderer/modeling/object/object.h"
#include "renderer/modeling/edf/diffuseedf.h"
#include "renderer/modeling/light/pointlight.h"
#include "renderer/modeling/material/genericmaterial.h"
#include "renderer/modeling/object/diskobject.h"
#include "renderer/modeling/object/rectangleobject.h"
#include "renderer/modeling/object/sphereobject.h"
#include "renderer/modeling/object/proceduralobject.h"
#include "renderer/modeling/scene/assembly.h"
#include "renderer/modeling/scene/assemblyinstance.h"
#include "renderer/modeling/scene/containers.h"
#include "renderer/modeling/scene/objectinstance.h"
#include "renderer/modeling/scene/scene.h"
#include "renderer/modeling/scene/visibilityflags.h"
#include "renderer/utility/paramarray.h"
#include "renderer/utility/testutils.h"

// appleseed.foundation headers.
#include "foundation/image/color.h"
#include "foundation/image/image.h"
#include "foundation/containers/dictionary.h"
#include "foundation/math/matrix.h"
#include "foundation/math/intersection/raydisk.h"
#include "foundation/math/scalar.h"
#include "foundation/math/sampling/mappings.h"
#include "foundation/math/transform.h"
#include "foundation/math/vector.h"
#include "foundation/memory/autoreleaseptr.h"
#include "foundation/utility/test.h"
#include "foundation/utility/iostreamop.h"

// Standard headers.
#include <algorithm>
#include <string>
#include <type_traits>
#include <vector>

using namespace foundation;
using namespace renderer;

TEST_SUITE(Renderer_Kernel_Intersection_Intersector)
{
    static_assert(std::is_trivially_destructible<ShadingPoint>::value,
        "Arena-allocated shading points must not own resources");

    struct TestScene
      : public TestSceneBase
    {
        TestScene()
        {
            auto_release_ptr<Assembly> assembly(
                AssemblyFactory().create("assembly", ParamArray()));

            assembly->objects().insert(
                auto_release_ptr<Object>(
                    new BoundingBoxObject(
                        "object",
                        GAABB3(GVector3(-1.0), GVector3(1.0)))));

            assembly->object_instances().insert(
                ObjectInstanceFactory::create(
                    "object_instance",
                    ParamArray(),
                    "object",
                    Transformd::identity(),
                    StringDictionary()));

            m_scene.assembly_instances().insert(
                auto_release_ptr<AssemblyInstance>(
                    AssemblyInstanceFactory::create(
                        "assembly_instance",
                        ParamArray(),
                        "assembly")));

            m_scene.assemblies().insert(assembly);
        }
    };

    template <bool UseEmbree, typename SceneType = TestScene>
    struct Fixture
      : public StaticTestSceneContext<SceneType>
    {
        TraceContext    m_trace_context;
        TextureStore    m_texture_store;
        TextureCache    m_texture_cache;
        Intersector     m_intersector;

        Fixture()
          : m_trace_context(this->m_scene)
          , m_texture_store(this->m_scene)
          , m_texture_cache(m_texture_store)
          , m_intersector(m_trace_context, m_texture_cache)
        {
#ifdef APPLESEED_WITH_EMBREE
            m_trace_context.set_use_embree(UseEmbree);
#endif
            m_trace_context.update();
        }
    };

    class NormalProbeObject : public ProceduralObject
    {
      public:
        NormalProbeObject() : ProceduralObject("object", ParamArray()) {}
        void release() override { delete this; }
        const char* get_model() const override { return "normal_probe"; }
        GAABB3 compute_local_bbox() const override { return GAABB3(GVector3(-1.0), GVector3(1.0)); }
        size_t get_material_slot_count() const override { return 3; }
        const char* get_material_slot(size_t) const override { return "default"; }
        static Vector3d normal() { return normalize(Vector3d(1.0, 2.0, 3.0)); }
        static Vector3d dpdu() { return Vector3d(4.0, -2.0, 0.0); }
        static Vector3d dpdv() { return Vector3d(0.0, 3.0, -2.0); }
        static constexpr double NormalSlopeU = 0.1;
        static constexpr double NormalSlopeV = 0.2;
        bool m_supply_derivatives = true;
        Vector3d m_shading_offset = Vector3d(0.0);

        Vector3d normal_field(const double u, const double v) const
        {
            return normalize(normal() + m_shading_offset + NormalSlopeU * u * dpdu() + NormalSlopeV * v * dpdv());
        }

        void intersect(const ShadingRay& ray, IntersectionResult& result) const override
        {
            const double denominator = dot(ray.m_dir, normal());
            const double distance = -dot(ray.m_org, normal()) / denominator;
            result.m_hit = distance >= ray.m_tmin && distance < ray.m_tmax;
            if (result.m_hit)
            {
                result.m_distance = distance;
                result.m_geometric_normal = normal();
                const Vector3d p = ray.point_at(distance);
                const double uu = dot(dpdu(), dpdu());
                const double uv = dot(dpdu(), dpdv());
                const double vv = dot(dpdv(), dpdv());
                const double determinant = uu * vv - uv * uv;
                const double u = (dot(p, dpdu()) * vv - dot(p, dpdv()) * uv) / determinant;
                const double v = (dot(p, dpdv()) * uu - dot(p, dpdu()) * uv) / determinant;
                const Vector3d raw_normal = normal() + m_shading_offset + NormalSlopeU * u * dpdu() + NormalSlopeV * v * dpdv();
                const double normal_length = norm(raw_normal);
                const Vector3d n = raw_normal / normal_length;
                result.m_shading_normal = n;
                result.m_uv = Vector2f(static_cast<float>(0.25 + u), static_cast<float>(0.75 + v));
                result.m_material_slot = 2;
                result.m_derivatives.m_valid = m_supply_derivatives;
                result.m_derivatives.m_dpdu = dpdu();
                result.m_derivatives.m_dpdv = dpdv();
                const Vector3d du = NormalSlopeU * dpdu();
                const Vector3d dv = NormalSlopeV * dpdv();
                result.m_derivatives.m_dndu = (du - n * dot(n, du)) / normal_length;
                result.m_derivatives.m_dndv = (dv - n * dot(n, dv)) / normal_length;
            }
        }
        bool intersect(const ShadingRay& ray) const override
        {
            IntersectionResult result;
            intersect(ray, result);
            return result.m_hit;
        }
        mutable Vector3d m_received_normal = Vector3d(0.0);
        void refine_and_offset(const Ray3d& ray, const Vector3d& geometric_normal,
            Vector3d& front, Vector3d& back, Vector3d& n) const override
        {
            m_received_normal = geometric_normal;
            n = faceforward(geometric_normal, ray.m_dir);
            const double Offset = 1.0e-6;
            front = ray.m_org + Offset * n;
            back = ray.m_org - Offset * n;
        }
    };

    class SlabProbeObject : public NormalProbeObject
    {
      public:
        static constexpr double HalfThickness = 0.5;

        void intersect(const ShadingRay& ray, IntersectionResult& result) const override
        {
            const Vector3d n = normal();
            const double denominator = dot(ray.m_dir, n);
            result.m_hit = false;
            if (denominator == 0.0)
                return;
            const double origin = dot(ray.m_org, n);
            const double first = (-HalfThickness - origin) / denominator;
            const double second = (HalfThickness - origin) / denominator;
            const double near = std::min(first, second);
            const double far = std::max(first, second);
            const double distance = near >= ray.m_tmin ? near : far;
            if (distance < ray.m_tmin || distance >= ray.m_tmax)
                return;
            result.m_hit = true;
            result.m_distance = distance;
            result.m_geometric_normal = dot(ray.point_at(distance), n) > 0.0 ? n : -n;
            result.m_shading_normal = result.m_geometric_normal;
            result.m_material_slot = 2;
        }

        bool intersect(const ShadingRay& ray) const override
        {
            IntersectionResult result;
            intersect(ray, result);
            return result.m_hit;
        }
    };

    template <typename Probe = NormalProbeObject>
    struct ProceduralTestScene : public TestSceneBase
    {
        NormalProbeObject* m_probe;
        Transformd m_object_transform;
        Transformd m_assembly_transform;

        explicit ProceduralTestScene(const double scale = 1.0)
          : m_probe(new Probe())
          // The analytic inverse isolates tiny-scale derivatives from the
          // generic inverse's absolute threshold for singular matrices.
          , m_object_transform(
                Matrix4d::make_scaling(scale * Vector3d(2.0, 3.0, 4.0)),
                Matrix4d::make_scaling(Vector3d(0.5, 1.0 / 3.0, 0.25) / scale))
          , m_assembly_transform(Matrix4d::make_translation(scale * Vector3d(5.0, 6.0, 7.0)))
        {
            auto_release_ptr<Assembly> assembly(AssemblyFactory().create("assembly", ParamArray()));
            assembly->objects().insert(auto_release_ptr<Object>(m_probe));
            assembly->object_instances().insert(ObjectInstanceFactory::create(
                "object_instance", ParamArray(), "object", m_object_transform, StringDictionary()));
            auto_release_ptr<AssemblyInstance> instance(AssemblyInstanceFactory::create(
                "assembly_instance", ParamArray(), "assembly"));
            instance->transform_sequence().set_transform(0.0f, m_assembly_transform);
            m_scene.assembly_instances().insert(instance);
            m_scene.assemblies().insert(assembly);
        }

        ShadingRay ray_to_plane(const bool back) const
        {
            const Vector3d n = back ? -NormalProbeObject::normal() : NormalProbeObject::normal();
            return ShadingRay(
                m_assembly_transform.point_to_parent(m_object_transform.point_to_parent(2.0 * n)),
                normalize(m_assembly_transform.vector_to_parent(m_object_transform.vector_to_parent(-n))),
                ShadingRay::Time(), VisibilityFlags::CameraRay, 0);
        }

        Vector3d world_vector(const Vector3d& vector) const
        {
            return m_assembly_transform.vector_to_parent(m_object_transform.vector_to_parent(vector));
        }

        Vector3d world_shading_normal(const double u, const double v) const
        {
            return normalize(m_assembly_transform.normal_to_parent(
                m_object_transform.normal_to_parent(m_probe->normal_field(u, v))));
        }

        Vector3d world_normal_derivative(const bool u) const
        {
            const double Step = 1.0e-5;
            return u
                ? (world_shading_normal(Step, 0.0) - world_shading_normal(-Step, 0.0)) / (2.0 * Step)
                : (world_shading_normal(0.0, Step) - world_shading_normal(0.0, -Step)) / (2.0 * Step);
        }

        ShadingRay ray_with_differentials() const
        {
            const double Step = 0.01;
            ShadingRay ray = ray_to_plane(false);
            ray.m_has_differentials = true;
            ray.m_rx_org = ray.m_org + Step * world_vector(NormalProbeObject::dpdu());
            ray.m_ry_org = ray.m_org + Step * world_vector(NormalProbeObject::dpdv());
            ray.m_rx_dir = ray.m_ry_dir = ray.m_dir;
            return ray;
        }

        Vector3d world_normal() const
        {
            return normalize(m_assembly_transform.normal_to_parent(
                m_object_transform.normal_to_parent(NormalProbeObject::normal())));
        }
    };

    const double NormalDerivativeTolerance = 1.0e-9;
    const double RayFootprintTolerance = 1.0e-12;

    using ProceduralFixture = Fixture<false, ProceduralTestScene<>>;

    struct SmallProceduralScene : ProceduralTestScene<>
    {
        SmallProceduralScene() : ProceduralTestScene(1.0e-25) {}
    };
    struct LargeProceduralScene : ProceduralTestScene<>
    {
        LargeProceduralScene() : ProceduralTestScene(1.0e25) {}
    };
    using SmallProceduralFixture = Fixture<false, SmallProceduralScene>;
    using LargeProceduralFixture = Fixture<false, LargeProceduralScene>;

    template <typename Factory>
    struct AnalyticScene : TestSceneBase
    {
        ProceduralObject* m_analytic;
        AnalyticScene(const Transformd& object_transform = Transformd::identity())
        {
            auto_release_ptr<Assembly> assembly(AssemblyFactory().create("assembly", ParamArray()));
            auto_release_ptr<Object> object = Factory().create("object",
                ParamArray().insert("width", 4.0).insert("height", 6.0).insert("radius", 2.0));
            m_analytic = static_cast<ProceduralObject*>(object.get());
            assembly->objects().insert(object);
            assembly->object_instances().insert(ObjectInstanceFactory::create(
                "object_instance", ParamArray(), "object", object_transform, StringDictionary()));
            m_scene.assembly_instances().insert(AssemblyInstanceFactory::create(
                "assembly_instance", ParamArray(), "assembly"));
            m_scene.assemblies().insert(assembly);
        }
    };
    using SphereFixture = Fixture<false, AnalyticScene<SphereObjectFactory>>;
    using RectangleFixture = Fixture<false, AnalyticScene<RectangleObjectFactory>>;
    using DiskFixture = Fixture<false, AnalyticScene<DiskObjectFactory>>;

    template <typename Factory>
    struct EmitterScene : AnalyticScene<Factory>
    {
        Transformd m_assembly_transform;

        explicit EmitterScene(const Matrix4d& scale = Matrix4d::make_scaling(Vector3d(2.0)))
          : AnalyticScene<Factory>(Transformd::from_local_to_parent(
                Matrix4d::make_translation(Vector3d(2.0, -1.0, 3.0)) *
                Matrix4d::make_rotation_y(-0.37) * scale))
          , m_assembly_transform(Transformd::from_local_to_parent(
                Matrix4d::make_translation(Vector3d(-4.0, 5.0, 6.0)) *
                Matrix4d::make_rotation_x(0.4) * Matrix4d::make_scaling(Vector3d(1.5))))
        {
            this->m_scene.assembly_instances().get_by_name("assembly_instance")
                ->transform_sequence().set_transform(0.0f, m_assembly_transform);
            auto* assembly = this->m_scene.assemblies().get_by_name("assembly");
            assembly->edfs().insert(DiffuseEDFFactory().create("emission",
                ParamArray().insert("radiance", "1.0")));
            assembly->materials().insert(GenericMaterialFactory().create("emitter",
                ParamArray().insert("edf", "emission")));
            assembly->object_instances().get_by_index(0)->assign_material(
                "default", ObjectInstance::FrontSide, "emitter");
        }

        EmittingShape emitting_shape(const bool back = false) const
        {
            const auto* instance = this->m_scene.assembly_instances().get_by_name("assembly_instance");
            const auto* object_instance = this->m_scene.assemblies().get_by_name("assembly")
                ->object_instances().get_by_index(0);
            const Transformd global = object_instance->get_transform() * m_assembly_transform;
            if constexpr (std::is_same<Factory, SphereObjectFactory>::value)
            {
                const double radius = static_cast<const SphereObject*>(this->m_analytic)->get_radius() * 3.0;
                return EmittingShape::create_sphere_shape(instance, 0, nullptr,
                    FourPi<double>() * radius * radius, global.point_to_parent(Vector3d(0.0)), radius);
            }
            else if constexpr (std::is_same<Factory, RectangleObjectFactory>::value)
            {
                Vector3d origin, x, y, n;
                static_cast<const RectangleObject*>(this->m_analytic)->get_origin_and_axes(origin, x, y, n);
                x = global.vector_to_parent(x);
                y = global.vector_to_parent(y);
                n = normalize(global.normal_to_parent(back ? -n : n));
                return EmittingShape::create_rectangle_shape(instance, 0, nullptr,
                    norm(cross(x, y)), global.point_to_parent(origin), x, y, n);
            }
            else
            {
                Vector3d x, y, n;
                const auto* disk = static_cast<const DiskObject*>(this->m_analytic);
                disk->get_axes(x, y, n);
                x = global.vector_to_parent(x);
                y = global.vector_to_parent(y);
                n = normalize(global.normal_to_parent(back ? -n : n));
                return EmittingShape::create_disk_shape(instance, 0, nullptr,
                    Pi<double>() * norm(cross(x, y)), global.point_to_parent(Vector3d(0.0)),
                    n, x, y);
            }
        }
    };

    using SphereEmitterFixture = Fixture<false, EmitterScene<SphereObjectFactory>>;
    using RectangleEmitterFixture = Fixture<false, EmitterScene<RectangleObjectFactory>>;
    using DiskEmitterFixture = Fixture<false, EmitterScene<DiskObjectFactory>>;

    // The in-plane shear distinguishes cross-product area from axis lengths.
    Matrix4d affine_emitter_scale()
    {
        Matrix4d transform = Matrix4d::make_scaling(Vector3d(2.0, 3.0, 4.0));
        transform[2] = 0.7;
        return transform;
    }

    template <typename Factory>
    struct AffineEmitterScene : EmitterScene<Factory>
    {
        AffineEmitterScene() : EmitterScene<Factory>(affine_emitter_scale()) {}
    };
    using AffineDiskEmitterFixture = Fixture<false, AffineEmitterScene<DiskObjectFactory>>;
    using AffineRectangleEmitterFixture = Fixture<false, AffineEmitterScene<RectangleObjectFactory>>;


    TEST_CASE(SampledEmitters_MatchTracedSurfaceAttributes)
    {
        const double PointTolerance = 1.0e-11;
        const float UVTolerance = 1.0e-6f;
        const double ProbeDistance = 0.5;
        const Vector2f samples[] = {Vector2f(0.17f, 0.33f), Vector2f(0.71f, 0.87f), Vector2f(0.43f, 0.62f)};
        const auto check = [&](auto& fixture)
        {
            for (const bool back : {false, true})
            {
                const EmittingShape shape = fixture.emitting_shape(back);
                for (const Vector2f& s : samples)
                {
                    LightSample sample;
                    shape.sample_uniform(s, 1.0f, sample);
                    sample.m_time = ShadingRay::Time::create_with_normalized_time(0.37f, -0.2f, 0.8f);
                    if (back && shape.get_shape_type() == EmittingShape::SphereShape)
                    {
                        sample.m_geometric_normal = -sample.m_geometric_normal;
                        sample.m_shading_normal = -sample.m_shading_normal;
                    }
                    const ShadingRay ray(sample.m_point + ProbeDistance * sample.m_geometric_normal,
                        -normalize(sample.m_geometric_normal),
                        sample.m_time, VisibilityFlags::CameraRay, 0);
                    ShadingPoint traced, sampled;
                    ASSERT_TRUE(fixture.m_intersector.trace(ray, traced));
                    sample.make_shading_point(sampled, sample.m_geometric_normal, fixture.m_intersector);
                    ShadingPoint copied(sampled);
                    for (const ShadingPoint* actual : {&sampled, &copied})
                    {
                        EXPECT_FEQ_EPS(sample.m_point, actual->get_point(), PointTolerance);
                        EXPECT_FEQ_EPS(traced.get_point(), actual->get_point(), PointTolerance);
                        EXPECT_FEQ_EPS(traced.get_uv(0), actual->get_uv(0), UVTolerance);
                        EXPECT_FEQ_EPS(traced.get_geometric_normal(), actual->get_geometric_normal(), PointTolerance);
                        EXPECT_FEQ_EPS(traced.get_shading_normal(), actual->get_shading_normal(), PointTolerance);
                        EXPECT_EQ(traced.get_side(), actual->get_side());
                        EXPECT_FEQ_EPS(ray.m_dir, actual->get_ray().m_dir, PointTolerance);
                        EXPECT_FEQ(sample.m_time.m_absolute, actual->get_ray().m_time.m_absolute);
                        EXPECT_FEQ(sample.m_time.m_normalized, actual->get_ray().m_time.m_normalized);
                        EXPECT_FEQ_EPS(traced.get_dpdu(0), actual->get_dpdu(0), PointTolerance);
                        EXPECT_FEQ_EPS(traced.get_dpdv(0), actual->get_dpdv(0), PointTolerance);
                        EXPECT_FEQ_EPS(traced.get_dndu(0), actual->get_dndu(0), PointTolerance);
                        EXPECT_FEQ_EPS(traced.get_dndv(0), actual->get_dndv(0), PointTolerance);
                    }
                }
            }
        };
        SphereEmitterFixture sphere;
        check(sphere);
        RectangleEmitterFixture rectangle;
        check(rectangle);
        DiskEmitterFixture disk;
        check(disk);
        AffineDiskEmitterFixture affine_disk;
        check(affine_disk);
        AffineRectangleEmitterFixture affine_rectangle;
        check(affine_rectangle);
    }

    TEST_CASE(SampledEmitters_HaveFiniteWorldBoundsAndCentroids)
    {
        const auto check = [&](auto& fixture)
        {
            const EmittingShape shape = fixture.emitting_shape();
            ASSERT_TRUE(shape.get_bbox().is_valid());
            EXPECT_TRUE(shape.get_bbox().contains(shape.get_centroid()));
            for (const Vector2f s : {Vector2f(0.17f, 0.33f), Vector2f(0.71f, 0.87f)})
            {
                LightSample sample;
                shape.sample_uniform(s, 1.0f, sample);
                EXPECT_TRUE(shape.get_bbox().contains(sample.m_point));
            }
        };
        SphereEmitterFixture sphere;
        check(sphere);
        RectangleEmitterFixture rectangle;
        check(rectangle);
        DiskEmitterFixture disk;
        check(disk);
        AffineDiskEmitterFixture affine_disk;
        check(affine_disk);
        AffineRectangleEmitterFixture affine_rectangle;
        check(affine_rectangle);
    }

    TEST_CASE(SampledEmitters_CollectorMatchesWorldGeometryAndArea)
    {
        const double PointTolerance = 1.0e-11;
        const auto check = [&](auto& fixture, const double area)
        {
            ForwardLightSampler sampler(fixture.m_scene);
            ASSERT_TRUE(sampler.has_lights());
            const ShadingRay::Time time = ShadingRay::Time::create_with_normalized_time(0.37f, -0.2f, 0.8f);
            LightSample sample;
            sampler.sample(time, Vector3f(0.31f, 0.17f, 0.33f), sample);
            ASSERT_TRUE(sample.m_shape != nullptr);
            EXPECT_FEQ_EPS(static_cast<float>(area), sample.m_shape->get_area(), 1.0e-4f);
            EXPECT_FEQ_EPS(static_cast<float>(1.0 / area), sample.m_probability, 1.0e-7f);
            const Vector3d normal = normalize(sample.m_geometric_normal);
            const ShadingRay ray(sample.m_point + 0.5 * normal, -normal,
                time, VisibilityFlags::CameraRay, 0);
            ShadingPoint traced, sampled;
            ASSERT_TRUE(fixture.m_intersector.trace(ray, traced));
            sample.make_shading_point(sampled, normal, fixture.m_intersector);
            EXPECT_FEQ_EPS(sample.m_point, traced.get_point(), PointTolerance);
            EXPECT_FEQ_EPS(sample.m_point, sampled.get_point(), PointTolerance);
            EXPECT_FEQ_EPS(traced.get_uv(0), sampled.get_uv(0), 1.0e-6f);
            EXPECT_FEQ_EPS(traced.get_geometric_normal(), sampled.get_geometric_normal(), PointTolerance);
            EXPECT_EQ(traced.get_side(), sampled.get_side());
            EXPECT_FEQ(time.m_absolute, sampled.get_ray().m_time.m_absolute);
            EXPECT_FEQ(time.m_normalized, sampled.get_ray().m_time.m_normalized);
        };
        SphereEmitterFixture sphere;
        check(sphere, FourPi<double>() * 9.0);
        RectangleEmitterFixture rectangle;
        check(rectangle, 24.0 * 9.0);
        DiskEmitterFixture disk;
        check(disk, Pi<double>() * 4.0 * 9.0);
        // Axis scales 2 and 4, assembly scale 1.5: plane-area scale is 18.
        AffineDiskEmitterFixture affine_disk;
        check(affine_disk, Pi<double>() * 4.0 * 18.0);
        AffineRectangleEmitterFixture affine_rectangle;
        check(affine_rectangle, 24.0 * 18.0);
    }

    template <bool Front, bool Back, bool Flip = false>
    struct SidedEmitterScene : EmitterScene<RectangleObjectFactory>
    {
        SidedEmitterScene()
        {
            auto* assembly = m_scene.assemblies().get_by_name("assembly");
            auto* object_instance = assembly->object_instances().get_by_index(0);
            if (!Front)
                object_instance->unassign_material("default", ObjectInstance::FrontSide);
            if (Back)
            {
                assembly->edfs().insert(DiffuseEDFFactory().create("back_emission",
                    ParamArray().insert("radiance", "1.0").insert("importance_multiplier", 3.0f)));
                assembly->materials().insert(GenericMaterialFactory().create("back_emitter",
                    ParamArray().insert("edf", "back_emission")));
                object_instance->assign_material("default", ObjectInstance::BackSide, "back_emitter");
            }
            object_instance->get_parameters().insert("flip_normals", Flip);
        }
    };

    struct MixedEmitterScene : SidedEmitterScene<true, true>
    {
        MixedEmitterScene()
        {
            m_scene.assemblies().get_by_name("assembly")->lights().insert(
                PointLightFactory().create("fill", ParamArray().insert("intensity", "1.0")));
        }
    };

    TEST_CASE(ForwardEmitterPDFs_IncludeLightClassSelectionProbability)
    {
        Fixture<false, MixedEmitterScene> fixture;
        ForwardLightSampler sampler(fixture.m_scene);
        ASSERT_TRUE(sampler.has_lights());
        EXPECT_EQ(size_t(1), sampler.get_non_physical_light_count());
        const ShadingRay::Time time = ShadingRay::Time::create_with_normalized_time(0.0f, 0.0f, 1.0f);
        LightSample point_light;
        sampler.sample(time, Vector3f(0.1f, 0.37f, 0.61f), point_light);
        ASSERT_TRUE(point_light.m_light != nullptr);
        EXPECT_FEQ(0.5f, point_light.m_probability);
        for (const float selection : {0.55f, 0.95f})
        {
            LightSample sample;
            sampler.sample(time, Vector3f(selection, 0.37f, 0.61f), sample);
            ASSERT_TRUE(sample.m_shape != nullptr);
            ShadingPoint sampled;
            sample.make_shading_point(sampled, sample.m_geometric_normal, fixture.m_intersector);
            const float side_probability = sample.m_shape->is_back_side() ? 0.75f : 0.25f;
            EXPECT_FEQ_EPS(0.5f * side_probability / sample.m_shape->get_area(), sample.m_probability, 1.0e-8f);
            EXPECT_FEQ_EPS(sample.m_probability, sampler.evaluate_pdf(sampled), 1.0e-8f);
        }
    }

    TEST_CASE(EmitterKeys_DistinguishSurfaceSidesAndFullInstanceIdentity)
    {
        const UniqueID Instance = 19;
        const EmittingShapeKey front(Instance, 2, 3, false);
        const EmittingShapeKey back(Instance, 2, 3, true);
        const EmittingShapeKey high(Instance + (UniqueID(1) << 32), 2, 3, false);
        EXPECT_FALSE(front == back);
        EXPECT_FALSE(front == high);
        EmittingShapeKeyHasher hasher;
        HashTable<EmittingShapeKey, EmittingShapeKeyHasher, int> table(hasher);
        table.resize(4);
        table.insert(front, 1);
        table.insert(back, 2);
        table.insert(high, 3);
        ASSERT_TRUE(table.get(front) != nullptr);
        ASSERT_TRUE(table.get(back) != nullptr);
        ASSERT_TRUE(table.get(high) != nullptr);
        EXPECT_EQ(1, *table.get(front));
        EXPECT_EQ(2, *table.get(back));
        EXPECT_EQ(3, *table.get(high));
    }

    TEST_CASE(EmitterPDFs_MatchSelectedSideAndRejectNonemittingSide)
    {
        const float PDFTolerance = 1.0e-8f;
        const auto check = [&](auto& fixture, const bool front_emits, const bool back_emits)
        {
            ForwardLightSampler forward(fixture.m_scene);
            BackwardLightSampler backward(fixture.m_scene);
            ASSERT_TRUE(forward.has_lights());
            ASSERT_TRUE(backward.has_lightset());
            const ShadingRay::Time time = ShadingRay::Time::create_with_normalized_time(0.0f, 0.0f, 1.0f);
            const auto* assembly = fixture.m_scene.assemblies().get_by_name("assembly");
            const auto* object_instance = assembly->object_instances().get_by_index(0);
            const bool flip = object_instance->must_flip_normals();
            const EmittingShape reference = fixture.emitting_shape();
            LightSample location;
            reference.sample_uniform(Vector2f(0.37f, 0.61f), 1.0f, location);
            const double ProbeDistance = 0.5;
            const float total_importance = (front_emits ? 1.0f : 0.0f) + (back_emits ? 3.0f : 0.0f);
            for (const bool back : {false, true})
            {
                const Vector3d normal = (back != flip ? -1.0 : 1.0) * location.m_geometric_normal;
                const ShadingRay ray(location.m_point + ProbeDistance * normal, -normal,
                    time, VisibilityFlags::CameraRay, 0);
                ShadingPoint traced;
                ASSERT_TRUE(fixture.m_intersector.trace(ray, traced));
                EXPECT_EQ(back, traced.is_leaving());
                const bool emits = back ? back_emits : front_emits;
                const float expected = emits ? (back ? 3.0f : 1.0f) / total_importance / reference.get_area() : 0.0f;
                EXPECT_FEQ_EPS(expected, forward.evaluate_pdf(traced), PDFTolerance);
                EXPECT_FEQ_EPS(expected, backward.evaluate_pdf(traced, traced), PDFTolerance);
            }
            // Different importance multipliers force unequal selection weights.
            for (const float selection : {0.13f, 0.87f})
            {
                LightSample sample;
                forward.sample(time, Vector3f(selection, 0.37f, 0.61f), sample);
                ASSERT_TRUE(sample.m_shape != nullptr);
                ShadingPoint sampled;
                sample.make_shading_point(sampled, sample.m_geometric_normal, fixture.m_intersector);
                EXPECT_EQ(sample.m_shape->is_back_side(), sampled.is_leaving());
                EXPECT_EQ(sample.m_shape->get_material(), sampled.get_material());
                EXPECT_FEQ_EPS(sample.m_probability, forward.evaluate_pdf(sampled), PDFTolerance);
                EXPECT_FEQ_EPS(sample.m_probability, backward.evaluate_pdf(sampled, sampled), PDFTolerance);
                LightSample backward_sample;
                backward.sample_lightset(time, Vector3f(selection, 0.37f, 0.61f), sampled, backward_sample);
                ASSERT_TRUE(backward_sample.m_shape != nullptr);
                ShadingPoint backward_point;
                backward_sample.make_shading_point(backward_point, backward_sample.m_geometric_normal, fixture.m_intersector);
                EXPECT_FEQ_EPS(backward_sample.m_probability, backward.evaluate_pdf(backward_point, sampled), PDFTolerance);
                EXPECT_EQ(backward_sample.m_shape->get_material(), backward_point.get_material());
            }
        };
        Fixture<false, SidedEmitterScene<true, true>> both;
        check(both, true, true);
        Fixture<false, SidedEmitterScene<true, false>> front;
        check(front, true, false);
        Fixture<false, SidedEmitterScene<false, true>> back;
        check(back, false, true);
        Fixture<false, SidedEmitterScene<true, true, true>> flipped;
        check(flipped, true, true);
    }

    template <typename Factory>
    struct NestedEmitterScene : EmitterScene<Factory>
    {
        NestedEmitterScene()
        {
            // Each level rotates a different axis, so using only the leaf
            // instance transform cannot reproduce the world-space shader hit.
            const Transformd transforms[] = {
                Transformd::from_local_to_parent(Matrix4d::make_translation(Vector3d(7.0, -3.0, 4.0)) *
                    Matrix4d::make_rotation_z(0.6) * Matrix4d::make_scaling(Vector3d(1.25))),
                Transformd::from_local_to_parent(Matrix4d::make_translation(Vector3d(-2.0, 8.0, -5.0)) *
                    Matrix4d::make_rotation_y(-0.3) * Matrix4d::make_scaling(Vector3d(0.9)))};
            const char* assembly_name = "assembly";
            const char* instance_name = "assembly_instance";
            const char* parents[] = {"parent", "outer"};
            const char* instances[] = {"parent_instance", "outer_instance"};
            for (size_t level = 0; level < 2; ++level)
            {
                auto_release_ptr<Assembly> parent(AssemblyFactory().create(parents[level], ParamArray()));
                parent->assembly_instances().insert(this->m_scene.assembly_instances().remove(
                    this->m_scene.assembly_instances().get_by_name(instance_name)));
                parent->assemblies().insert(this->m_scene.assemblies().remove(
                    this->m_scene.assemblies().get_by_name(assembly_name)));
                auto_release_ptr<AssemblyInstance> instance(AssemblyInstanceFactory::create(
                    instances[level], ParamArray(), parents[level]));
                instance->transform_sequence().set_transform(0.0f, transforms[level]);
                this->m_scene.assembly_instances().insert(instance);
                this->m_scene.assemblies().insert(parent);
                this->m_assembly_transform = this->m_assembly_transform * transforms[level];
                assembly_name = parents[level];
                instance_name = instances[level];
            }
        }
    };

    TEST_CASE(SampledEmitters_NestedAssembliesPreserveShaderHitAttributes)
    {
        const double PointTolerance = 1.0e-10;
        const float UVTolerance = 1.0e-6f;
        const auto check = [&](auto& fixture)
        {
            ForwardLightSampler forward(fixture.m_scene);
            BackwardLightSampler backward(fixture.m_scene);
            ASSERT_TRUE(forward.has_lights());
            const ShadingRay::Time time = ShadingRay::Time::create_with_normalized_time(0.37f, -0.2f, 0.8f);
            for (const Vector3f s : {Vector3f(0.31f, 0.17f, 0.33f), Vector3f(0.87f, 0.71f, 0.62f)})
            {
                LightSample sample;
                forward.sample(time, s, sample);
                ASSERT_TRUE(sample.m_shape != nullptr);
                const Vector3d normal = normalize(sample.m_geometric_normal);
                const ShadingRay ray(sample.m_point + 0.5 * normal, -normal,
                    time, VisibilityFlags::CameraRay, 0);
                ShadingPoint traced, sampled;
                ASSERT_TRUE(fixture.m_intersector.trace(ray, traced));
                sample.make_shading_point(sampled, normal, fixture.m_intersector);
                ShadingPoint copy(sampled);
                for (const ShadingPoint* actual : {&sampled, &copy})
                {
                    EXPECT_FEQ_EPS(traced.get_point(), actual->get_point(), PointTolerance);
                    EXPECT_FEQ_EPS(traced.get_uv(0), actual->get_uv(0), UVTolerance);
                    EXPECT_FEQ_EPS(traced.get_geometric_normal(), actual->get_geometric_normal(), PointTolerance);
                    EXPECT_FEQ_EPS(traced.get_shading_normal(), actual->get_shading_normal(), PointTolerance);
                    EXPECT_FEQ_EPS(traced.get_dpdu(0), actual->get_dpdu(0), PointTolerance);
                    EXPECT_FEQ_EPS(traced.get_dpdv(0), actual->get_dpdv(0), PointTolerance);
                    EXPECT_FEQ_EPS(traced.get_dndu(0), actual->get_dndu(0), PointTolerance);
                    EXPECT_FEQ_EPS(traced.get_dndv(0), actual->get_dndv(0), PointTolerance);
                    EXPECT_EQ(traced.get_side(), actual->get_side());
                    EXPECT_EQ(traced.get_material(), actual->get_material());
                    EXPECT_FEQ_EPS(traced.get_assembly_instance_transform().point_to_parent(Vector3d(0.0)),
                        actual->get_assembly_instance_transform().point_to_parent(Vector3d(0.0)), PointTolerance);
                    EXPECT_FEQ_EPS(sample.m_probability, forward.evaluate_pdf(*actual), 1.0e-8f);
                    EXPECT_FEQ_EPS(sample.m_probability, backward.evaluate_pdf(*actual, traced), 1.0e-8f);
                }
            }
        };
        Fixture<false, NestedEmitterScene<SphereObjectFactory>> sphere;
        check(sphere);
        Fixture<false, NestedEmitterScene<RectangleObjectFactory>> rectangle;
        check(rectangle);
        Fixture<false, NestedEmitterScene<DiskObjectFactory>> disk;
        check(disk);
    }

    TEST_CASE(OSLObjectTransforms_RespectHierarchyAndStaticLookup)
    {
        const double TransformTolerance = 1.0e-4;
        const Transformd object = Transformd::from_local_to_parent(
            Matrix4d::make_translation(Vector3d(2.0, -1.0, 3.0)) *
            Matrix4d::make_rotation_y(-0.37) * Matrix4d::make_scaling(Vector3d(2.0, 3.0, 4.0)));
        const Transformd start = Transformd::from_local_to_parent(
            Matrix4d::make_translation(Vector3d(-4.0, 5.0, 6.0)) * Matrix4d::make_rotation_x(0.4));
        const Transformd end = Transformd::from_local_to_parent(
            Matrix4d::make_translation(Vector3d(7.0, -2.0, 1.0)) * Matrix4d::make_rotation_z(-0.6));
        const Vector3d local_points[] = {Vector3d(0.0), Vector3d(0.3, -0.7, 0.9)};
        const auto apply = [](const OSL::Matrix44& matrix, const Vector3d& p)
        {
            OSL::Vec3 result;
            matrix.multVecMatrix(OSL::Vec3(static_cast<float>(p.x), static_cast<float>(p.y),
                static_cast<float>(p.z)), result);
            return Vector3d(result.x, result.y, result.z);
        };
        for (const size_t keys : {size_t(0), size_t(1), size_t(2)})
        {
            TransformSequence assembly;
            if (keys > 0)
                assembly.set_transform(0.0f, start);
            if (keys > 1)
                assembly.set_transform(1.0f, end);
            ASSERT_TRUE(assembly.prepare());
            const ShadingPoint::OSLObjectTransformInfo info = {&assembly, &object};
            EXPECT_EQ(keys > 1, info.is_animated());
            for (const float time : {0.0f, 0.37f, 1.0f})
            {
                const Transformd assembly_transform = assembly.evaluate(time);
                for (const Vector3d& local : local_points)
                {
                    const Vector3d world = assembly_transform.point_to_parent(object.point_to_parent(local));
                    EXPECT_FEQ_EPS(world, apply(info.get_transform(time), local), TransformTolerance);
                    EXPECT_FEQ_EPS(local, apply(info.get_inverse_transform(time), world), TransformTolerance);
                    EXPECT_FEQ_EPS(local, apply(info.get_inverse_transform(time),
                        apply(info.get_transform(time), local)), TransformTolerance);
                    if (!info.is_animated())
                    {
                        EXPECT_FEQ_EPS(world, apply(info.get_transform(), local), TransformTolerance);
                        EXPECT_FEQ_EPS(local, apply(info.get_inverse_transform(), world), TransformTolerance);
                    }
                }
            }
        }
    }

    class WeightedSphereObject : public ProceduralObject
    {
      public:
        WeightedSphereObject()
          : ProceduralObject("object", ParamArray())
          , m_sphere(SphereObjectFactory().create("native_sphere", ParamArray()))
        {
        }
        void release() override { delete this; }
        const char* get_model() const override { return "weighted_sphere"; }
        GAABB3 compute_local_bbox() const override { return native().compute_local_bbox(); }
        size_t get_material_slot_count() const override { return 1; }
        const char* get_material_slot(size_t) const override { return "default"; }
        void intersect(const ShadingRay& ray, IntersectionResult& hit) const override { native().intersect(ray, hit); }
        bool intersect(const ShadingRay& ray) const override { return native().intersect(ray); }
        void refine_and_offset(const Ray3d& ray, const Vector3d& n, Vector3d& front, Vector3d& back, Vector3d& offset) const override
        { native().refine_and_offset(ray, n, front, back, offset); }
        double get_surface_sampling_reference_area() const override { return 1.5 * FourPi<double>(); }
        bool sample_surface(const double s[SurfaceSampleDimensions], SurfaceSample& result) const override
        {
            // Match a sphere's Crofton estimator: one third of draws hit,
            // two crossings per hit, reference area 1.5 times physical area.
            if (s[0] < 2.0 / 3.0)
                return false;
            SurfaceSample sample = {};
            sample.m_point = sample_sphere_uniform(Vector2d(s[1], s[2]));
            native().get_surface_attributes(sample.m_point, sample.m_attributes);
            sample.m_estimator_weight = 2.0;
            result = sample;
            return true;
        }
      private:
        auto_release_ptr<Object> m_sphere;
        const SphereObject& native() const { return static_cast<const SphereObject&>(*m_sphere); }
    };

    struct WeightedEmitterScene : EmitterScene<SphereObjectFactory>
    {
        explicit WeightedEmitterScene(const Matrix4d& scale = Matrix4d::make_scaling(Vector3d(2.0)))
          : EmitterScene<SphereObjectFactory>(scale)
        {
            auto* assembly = m_scene.assemblies().get_by_name("assembly");
            assembly->objects().remove(assembly->objects().get_by_name("object"));
            auto_release_ptr<Object> object(new WeightedSphereObject());
            m_analytic = static_cast<ProceduralObject*>(object.get());
            assembly->objects().insert(object);
        }
    };
    struct AffineWeightedEmitterScene : WeightedEmitterScene
    {
        AffineWeightedEmitterScene() : WeightedEmitterScene(affine_emitter_scale()) {}
    };

    TEST_CASE_F(ProceduralHitDistance_PreservesFloatDirectionParameter, ProceduralFixture)
    {
        ShadingRay ray = ray_to_plane(false);
        // Reproduce the float-to-double promotion used by sampled EDF rays.
        ray.m_dir = Vector3d(Vector3f(ray.m_dir));
        const Vector3d point = m_assembly_transform.point_to_parent(
            m_object_transform.point_to_parent(Vector3d(0.0)));
        const Vector3d normal = world_normal();
        const double expected = dot(point - ray.m_org, normal) / dot(ray.m_dir, normal);
        ShadingPoint hit;
        ASSERT_TRUE(m_intersector.trace(ray, hit));
        EXPECT_FEQ_EPS(expected, hit.get_distance(), 1.0e-11);
        EXPECT_FEQ_EPS(ray.point_at(expected), hit.get_point(), 1.0e-11);
        m_intersector.advance_past_hit(ray, hit);
        ShadingPoint beyond;
        EXPECT_FALSE(m_intersector.trace(ray, beyond));
    }

    TEST_CASE_F(LightHitMIS_PreservesPhysicalOriginAcrossSkippedSurfaces, ProceduralFixture)
    {
        ShadingRay ray = ray_to_plane(false);
        const Vector3d physical_origin = ray.m_org;
        const double physical_distance = 2.0 * norm(world_vector(NormalProbeObject::normal()));
        // Model a transparent boundary three quarters along the segment.
        ray.m_org += 0.75 * physical_distance * ray.m_dir;
        ShadingPoint hit;
        ASSERT_TRUE(m_intersector.trace(ray, hit));
        EXPECT_FEQ_EPS(0.25 * physical_distance, hit.get_distance(), 1.0e-10);
        SamplingContext::RNGType rng;
        SamplingContext sampling(rng, SamplingContext::QMCMode, 0);
        PathVertex vertex(sampling);
        vertex.m_shading_point = &hit;
        vertex.m_prev_point = physical_origin;
        vertex.m_prev_mode = ScatteringMode::Diffuse;
        vertex.m_prev_prob = 0.25f;
        vertex.m_cos_on = 0.6;
        const float expected = static_cast<float>(0.15 / square(physical_distance));
        EXPECT_FEQ_EPS(expected, vertex.get_bsdf_prob_area(), 1.0e-9f);
    }

    TEST_CASE(WeightedEmitters_PreserveNullAttemptsAndReferenceDensity)
    {
        Fixture<false, WeightedEmitterScene> fixture;
        ForwardLightSampler sampler(fixture.m_scene);
        ASSERT_TRUE(sampler.has_lights());
        const double WorldArea = FourPi<double>() * 9.0;
        const double ReferenceArea = 1.5 * WorldArea;
        const size_t SampleCount = 8192;
        const auto time = ShadingRay::Time::create_with_normalized_time(0.0f, 0.0f, 1.0f);
        for (const auto mode : {SamplingContext::QMCMode, SamplingContext::RNGMode})
        {
            SamplingContext::RNGType rng;
            SamplingContext sampling(rng, mode, 3, SampleCount);
            size_t successes = 0, misses = 0;
            double area_sum = 0.0;
            LightSample sample;
            for (size_t i = 0; i < SampleCount; ++i)
            {
                const Vector3f s = sampling.next2<Vector3f>();
                SamplingContext child = sampling.split(0, 0);
                sampler.sample(time, s, sample, &child);
                ASSERT_TRUE(sample.m_shape != nullptr);
                if (!sample.m_valid)
                {
                    ++misses;
                    EXPECT_FEQ(0.0f, sample.m_probability);
                    EXPECT_FEQ(0.0f, sample.m_estimator_weight);
                    continue;
                }
                ++successes;
                EXPECT_FEQ(2.0f, sample.m_estimator_weight);
                EXPECT_FEQ_EPS(static_cast<float>(1.0 / ReferenceArea), sample.m_probability, 1.0e-9f);
                area_sum += sample.m_estimator_weight / sample.m_probability;
            }
            EXPECT_TRUE(successes > SampleCount / 4 && successes < SampleCount / 2);
            EXPECT_TRUE(misses > successes);
            EXPECT_FEQ_EPS(WorldArea, area_sum / SampleCount, 0.05 * WorldArea);
        }
    }

    TEST_CASE(WeightedEmitters_AffineDensityAndNativeAttributesMatchTracedHits)
    {
        Fixture<false, AffineWeightedEmitterScene> fixture;
        ForwardLightSampler sampler(fixture.m_scene);
        // A requested tree must retain weighted emitters via the CDF fallback.
        BackwardLightSampler backward(fixture.m_scene, ParamArray().insert("algorithm", "lighttree"));
        ASSERT_TRUE(backward.has_lightset());
        const auto time = ShadingRay::Time::create_with_normalized_time(0.0f, 0.0f, 1.0f);
        const auto* object_instance = fixture.m_scene.assemblies().get_by_name("assembly")->object_instances().get_by_index(0);
        const Transformd global = object_instance->get_transform() * fixture.m_assembly_transform;
        SamplingContext::RNGType rng;
        SamplingContext sampling(rng, SamplingContext::QMCMode, 3, 128);
        size_t successes = 0;
        for (size_t i = 0; i < 128; ++i)
        {
            const Vector3f s = sampling.next2<Vector3f>();
            SamplingContext child = sampling.split(0, 0);
            LightSample sample;
            sampler.sample(time, s, sample, &child);
            if (!sample.m_valid)
                continue;
            ++successes;
            const Basis3d basis(sample.m_surface_attributes.m_geometric_normal);
            const double jacobian = norm(cross(global.vector_to_parent(basis.get_tangent_u()),
                global.vector_to_parent(basis.get_tangent_v())));
            const double reference_area = fixture.m_analytic->get_surface_sampling_reference_area();
            EXPECT_FEQ_EPS(static_cast<float>(1.0 / (reference_area * jacobian)), sample.m_probability, 1.0e-9f);
            ShadingPoint traced, sampled;
            const ShadingRay ray(sample.m_point + 0.1 * sample.m_geometric_normal, -sample.m_geometric_normal,
                time, VisibilityFlags::CameraRay, 0);
            ASSERT_TRUE(fixture.m_intersector.trace(ray, traced));
            sample.make_shading_point(sampled, sample.m_geometric_normal, fixture.m_intersector);
            EXPECT_FEQ_EPS(traced.get_point(), sampled.get_point(), 1.0e-9);
            EXPECT_FEQ_EPS(traced.get_uv(0), sampled.get_uv(0), 1.0e-6f);
            EXPECT_FEQ_EPS(traced.get_shading_normal(), sampled.get_shading_normal(), 1.0e-9);
            EXPECT_FEQ_EPS(traced.get_dpdu(0), sampled.get_dpdu(0), 1.0e-9);
            EXPECT_FEQ_EPS(traced.get_dndu(0), sampled.get_dndu(0), 1.0e-9);
            EXPECT_FEQ_EPS(sample.m_probability, sampler.evaluate_pdf(traced), 1.0e-9f);
            EXPECT_FEQ_EPS(sample.m_probability, backward.evaluate_pdf(traced, traced), 1.0e-9f);
        }
        EXPECT_TRUE(successes > 0);
    }

    TEST_CASE(DiskIntersection_ParallelMissPreservesOutputsAndAcceptsNonunitRays)
    {
        double distance = -1.0, u = -1.0, v = -1.0;
        for (const double height : {0.0, 1.0})
        {
            const Ray3d parallel(Vector3d(0.0, height, 0.0), Vector3d(1.0, 0.0, 0.0));
            EXPECT_FALSE(intersect_disk(parallel, 2.0));
            EXPECT_FALSE(intersect_disk(parallel, 2.0, distance, u, v));
            EXPECT_FEQ(-1.0, distance);
            EXPECT_FEQ(-1.0, u);
            EXPECT_FEQ(-1.0, v);
        }
        const Ray3d scaled(Vector3d(1.0, 2.0, 0.0), Vector3d(0.0, -2.0, 0.0));
        ASSERT_TRUE(intersect_disk(scaled, 2.0, distance, u, v));
        EXPECT_FEQ(1.0, distance);
        EXPECT_FEQ(0.25, u);
        EXPECT_FEQ(0.5, v);
    }

    struct CoincidentSphereScene : TestSceneBase
    {
        static constexpr double NearSurfaceSeparation = 0.01;
        static Vector3d center() { return Vector3d(5.0, 6.0, 7.0); }

        CoincidentSphereScene()
        {
            auto_release_ptr<Assembly> assembly(AssemblyFactory().create("assembly", ParamArray()));
            assembly->objects().insert(SphereObjectFactory().create("sphere",
                ParamArray().insert("radius", 2.0)));
            const Matrix4d scale = Matrix4d::make_scaling(Vector3d(2.0, 3.0, 4.0));
            assembly->object_instances().insert(ObjectInstanceFactory::create(
                "first", ParamArray(), "sphere", Transformd(scale), StringDictionary()));
            assembly->object_instances().insert(ObjectInstanceFactory::create(
                "second", ParamArray(), "sphere", Transformd(scale), StringDictionary()));
            assembly->object_instances().insert(ObjectInstanceFactory::create(
                "nearby", ParamArray(), "sphere",
                Transformd(Matrix4d::make_translation(Vector3d(0.0, 0.0, -NearSurfaceSeparation)) * scale),
                StringDictionary()));
            auto_release_ptr<AssemblyInstance> instance(AssemblyInstanceFactory::create(
                "assembly_instance", ParamArray(), "assembly"));
            instance->transform_sequence().set_transform(0.0f,
                Transformd(Matrix4d::make_translation(center())));
            m_scene.assembly_instances().insert(instance);
            m_scene.assemblies().insert(assembly);
        }

        ShadingRay ray_to_boundary(const bool inside) const
        {
            const Vector3d origin = center() + Vector3d(0.0, 0.0, inside ? 0.0 : 18.0);
            return ShadingRay(origin, Vector3d(0.0, 0.0, -1.0),
                ShadingRay::Time::create_with_normalized_time(0.0f, 0.0f, 1.0f),
                VisibilityFlags::CameraRay, 0);
        }
    };
    using CoincidentSphereFixture = Fixture<false, CoincidentSphereScene>;

    using SlabFixture = Fixture<false, ProceduralTestScene<SlabProbeObject>>;

    struct DepthCameraScene : ProceduralTestScene<>
    {
        DepthCameraScene()
        {
            auto_release_ptr<Camera> camera(OrthographicCameraFactory().create("camera",
                ParamArray().insert("film_dimensions", "4 4")));
            camera->transform_sequence().set_transform(0.0f,
                Transformd(Matrix4d::make_translation(Vector3d(0.0, 0.0, 40.0))));
            m_scene.cameras().insert(camera);
            m_project.set_frame(FrameFactory::create("depth_camera_frame",
                ParamArray().insert("resolution", "1 1").insert("camera", "camera")));
        }
    };
    using DepthCameraFixture = Fixture<false, DepthCameraScene>;

    struct DepthSample
    {
        Color<float, 2> depth;
        float unrelated_alpha;
    };

    DepthSample accumulate_depth(const ShadingPoint& point, const bool camera_space = false)
    {
        AOVContainer aovs;
        aovs.insert(DepthAOVFactory().create(ParamArray().insert("camera_space", camera_space)));
        auto_release_ptr<Frame> frame(FrameFactory::create("depth_test",
            ParamArray().insert("resolution", "1 1").insert("tile_size", "1 1"), aovs));
        frame->clear_main_and_aov_images();
        AOVAccumulatorContainer accumulators(*frame);
        accumulators.on_tile_begin(*frame, 0, 0, 1);
        ShadingResult result(1);
        result.m_aovs[0] = Color4f(0.25f);
        accumulators.write(PixelContext(Vector2i(0), Vector2d(0.5)), point,
            ShadingComponents(), AOVComponents(), result);
        accumulators.on_tile_end(*frame, 0, 0);
        DepthSample sample;
        frame->aovs().get_by_name("depth")->get_image().get_pixel(0, 0, sample.depth);
        sample.unrelated_alpha = result.m_aovs[0].a;
        return sample;
    }

    TEST_CASE_F(DepthAOV_HitWritesOwnCoverageWithoutChangingColorResult, ProceduralFixture)
    {
        ShadingPoint point;
        ASSERT_TRUE(m_intersector.trace(ray_to_plane(false), point));
        const DepthSample sample = accumulate_depth(point);
        EXPECT_FEQ(static_cast<float>(point.get_distance()), sample.depth[0]);
        EXPECT_FEQ(1.0f, sample.depth[1]);
        EXPECT_FEQ(0.25f, sample.unrelated_alpha);
    }

    TEST_CASE_F(DepthAOV_CameraSpaceIgnoresPrimaryRayOriginShift, DepthCameraFixture)
    {
        ShadingRay near_ray = ray_to_plane(false);
        ShadingRay far_ray = near_ray;
        far_ray.m_org -= 10.0 * far_ray.m_dir;
        ShadingPoint near_point, far_point;
        ASSERT_TRUE(m_intersector.trace(near_ray, near_point));
        ASSERT_TRUE(m_intersector.trace(far_ray, far_point));
        EXPECT_FEQ(33.0f, accumulate_depth(near_point, true).depth[0]);
        EXPECT_FEQ(33.0f, accumulate_depth(far_point, true).depth[0]);
        EXPECT_FEQ(10.0f, accumulate_depth(far_point).depth[0] -
            accumulate_depth(near_point).depth[0]);
    }

    TEST_CASE(DepthAOV_MissClearsOwnCoverageWithoutChangingColorResult)
    {
        const DepthSample sample = accumulate_depth(ShadingPoint());
        EXPECT_FEQ(0.0f, sample.depth[0]);
        EXPECT_FEQ(0.0f, sample.depth[1]);
        EXPECT_FEQ(0.25f, sample.unrelated_alpha);
    }

    TEST_CASE_F(CoincidentSurfaces_CollectsTransformedEntriesAndExitsWithoutNearbyBoundary, CoincidentSphereFixture)
    {
        for (const bool inside : {false, true})
        {
            ShadingPoint first;
            ASSERT_TRUE(m_intersector.trace(ray_to_boundary(inside), first));
            std::vector<ShadingPoint> points;
            m_intersector.collect_coincident_surfaces(first, points);
            ASSERT_EQ(2, points.size());
            EXPECT_EQ(&first.get_object_instance(), &points.front().get_object_instance());
            EXPECT_TRUE(&points[0].get_object_instance() != &points[1].get_object_instance());
            for (const ShadingPoint& point : points)
            {
                EXPECT_TRUE(std::string(point.get_object_instance().get_name()) != "nearby");
                EXPECT_FEQ_EPS(first.get_point(), point.get_point(), 1.0e-12);
                EXPECT_FEQ_EPS(first.get_geometric_normal(), point.get_geometric_normal(), 1.0e-12);
                EXPECT_EQ(!inside, point.is_entering());
            }
        }
    }

    TEST_CASE_F(OpticalInterface_CopyAndResetPreserveSemanticState, SphereFixture)
    {
        const float InteriorIOR = 1.33f;
        const float ExteriorIOR = 2.4f;
        const ShadingRay ray(Vector3d(0.0, 0.0, 4.0), Vector3d(0.0, 0.0, -1.0),
            ShadingRay::Time::create_with_normalized_time(0.0f, 0.0f, 1.0f),
            VisibilityFlags::CameraRay, 0);
        ShadingPoint point;
        ASSERT_TRUE(m_intersector.trace(ray, point));
        ShadingRay transmitted;
        transmitted.add_medium(ray, &point.get_object_instance(), nullptr, InteriorIOR);
        point.set_optical_interface(ShadingPoint::OpticalInterface::Surface, transmitted, ExteriorIOR);
        ShadingPoint copy(point), assigned;
        assigned = point;
        const ShadingPoint* self = &assigned;
        assigned = *self;
        point.clear();
        EXPECT_TRUE(point.get_optical_interface() == ShadingPoint::OpticalInterface::Unresolved);
        for (const ShadingPoint* saved : {&copy, &assigned})
        {
            EXPECT_TRUE(saved->get_optical_interface() == ShadingPoint::OpticalInterface::Surface);
            EXPECT_FEQ(ExteriorIOR, saved->get_outside_ior());
            ShadingRay next;
            saved->copy_transmitted_media(next);
            EXPECT_EQ(1, next.m_medium_count);
            EXPECT_FEQ(InteriorIOR, next.get_current_ior());
            EXPECT_EQ(&saved->get_object_instance(), next.get_current_medium()->m_object_instance);
        }
        assigned.set_ray(ray);
        EXPECT_TRUE(assigned.get_optical_interface() == ShadingPoint::OpticalInterface::Unresolved);
        EXPECT_FEQ(1.0f, assigned.get_outside_ior());
        assigned.set_no_optical_interface();
        ShadingPoint checked(assigned);
        EXPECT_TRUE(checked.get_optical_interface() == ShadingPoint::OpticalInterface::None);
    }

    TEST_CASE_F(CoincidentSurfaces_MissClearsOutput, CoincidentSphereFixture)
    {
        std::vector<ShadingPoint> points(1);
        m_intersector.collect_coincident_surfaces(ShadingPoint(), points);
        EXPECT_TRUE(points.empty());
    }

    TEST_CASE_F(InitialMediaQuery_SkipsOutsideProceduralBounds, SphereFixture)
    {
        const ShadingRay ray(Vector3d(0.0, 0.0, 4.0), Vector3d(0.0, 0.0, -1.0),
            ShadingRay::Time::create_with_normalized_time(0.0f, 0.0f, 1.0f),
            VisibilityFlags::CameraRay, 0);
        ShadingPoint ordinary, initial;
        EXPECT_TRUE(m_intersector.trace(ray, ordinary));
        EXPECT_FALSE(m_intersector.trace_for_initial_media(ray, initial));
    }

    TEST_CASE_F(InitialMediaQuery_PreservesTransformedInsideHit, SlabFixture)
    {
        const Vector3d origin = m_assembly_transform.point_to_parent(
            m_object_transform.point_to_parent(Vector3d(0.0)));
        const ShadingRay ray(origin, normalize(world_vector(NormalProbeObject::normal())),
            ShadingRay::Time::create_with_normalized_time(0.0f, 0.0f, 1.0f),
            VisibilityFlags::CameraRay, 0);
        ShadingPoint ordinary, initial;
        ASSERT_TRUE(m_intersector.trace(ray, ordinary));
        ASSERT_TRUE(m_intersector.trace_for_initial_media(ray, initial));
        EXPECT_FEQ_EPS(ordinary.get_point(), initial.get_point(), 1.0e-12);
        EXPECT_FEQ_EPS(ordinary.get_geometric_normal(), initial.get_geometric_normal(), 1.0e-12);
        EXPECT_FALSE(initial.is_entering());
    }

    TEST_CASE_F(OffsetSecondaryRay_PreservesActualHitAndLeavesClosedObject, SlabFixture)
    {
        ShadingPoint entry;
        ASSERT_TRUE(m_intersector.trace(ray_to_plane(false), entry));
        const Vector3d direction = normalize(world_vector(
            -NormalProbeObject::normal() + 0.1 * normalize(NormalProbeObject::dpdu())));
        const ShadingRay transmitted(entry.get_point(), direction,
            ShadingRay::Time(), VisibilityFlags::SpecularRay, 1);
        ShadingPoint exit;
        ASSERT_TRUE(m_intersector.trace(transmitted, exit, &entry));
        ShadingPoint copy(exit);
        const Vector3d object_point = m_object_transform.point_to_local(
            m_assembly_transform.point_to_local(exit.get_point()));
        EXPECT_FEQ_EPS(-SlabProbeObject::HalfThickness,
            dot(object_point, NormalProbeObject::normal()), 1.0e-12);
        EXPECT_FEQ_EPS(exit.get_point(), copy.get_point(), 1.0e-12);
        ShadingPoint assigned;
        assigned = exit;
        assigned.flip_side();
        EXPECT_FEQ_EPS(exit.get_point(), assigned.get_point(), 1.0e-12);
        const ShadingRay outgoing(exit.get_point(), direction,
            ShadingRay::Time(), VisibilityFlags::SpecularRay, 2);
        ShadingPoint background;
        EXPECT_FALSE(m_intersector.trace(outgoing, background, &exit));
        EXPECT_FALSE(m_intersector.trace_probe(outgoing, &exit));
    }

    TEST_CASE_F(CopyConstructor_PreservesUncachedProceduralInputs, ProceduralFixture)
    {
        ShadingPoint original;
        ASSERT_TRUE(m_intersector.trace(ray_to_plane(false), original));
        ShadingPoint copy(original);
        EXPECT_FEQ(world_normal(), copy.get_geometric_normal());
        EXPECT_FEQ(world_normal(), copy.get_original_shading_normal());
        EXPECT_FEQ(Vector2f(0.25f, 0.75f), copy.get_uv(0));
        EXPECT_EQ(size_t(2), copy.get_primitive_attribute_index());
        EXPECT_EQ(ObjectInstance::FrontSide, copy.get_side());
    }

    TEST_CASE_F(Assignment_PreservesCachedBackFaceInputs, ProceduralFixture)
    {
        ShadingPoint original;
        ASSERT_TRUE(m_intersector.trace(ray_to_plane(true), original));
        EXPECT_FEQ(-world_normal(), original.get_geometric_normal());
        EXPECT_EQ(ObjectInstance::BackSide, original.get_side());
        ShadingPoint copy;
        copy = original;
        EXPECT_FEQ(-world_normal(), copy.get_geometric_normal());
        EXPECT_FEQ(-world_normal(), copy.get_original_shading_normal());
        EXPECT_EQ(ObjectInstance::BackSide, copy.get_side());
        EXPECT_FEQ(Vector2f(0.25f, 0.75f), copy.get_uv(0));
        EXPECT_EQ(size_t(2), copy.get_primitive_attribute_index());
    }

    TEST_CASE_F(Refine_ReceivesObjectSpaceNormalAfterBackFaceCopy, ProceduralFixture)
    {
        ShadingPoint original;
        ASSERT_TRUE(m_intersector.trace(ray_to_plane(true), original));
        EXPECT_EQ(ObjectInstance::BackSide, original.get_side());
        ShadingPoint copy(original);
        const ShadingRay outgoing(copy.get_point(), world_normal(),
            ShadingRay::Time(), VisibilityFlags::ShadowRay, 1);
        EXPECT_FALSE(m_intersector.trace_probe(outgoing, &copy));
        EXPECT_FEQ(NormalProbeObject::normal(), m_probe->m_received_normal);
    }

    TEST_CASE_F(FlipSide_RecomputesProceduralNormalsFromIntersectionInputs, ProceduralFixture)
    {
        ShadingPoint point;
        ASSERT_TRUE(m_intersector.trace(ray_to_plane(true), point));
        EXPECT_EQ(ObjectInstance::BackSide, point.get_side());
        point.flip_side();
        EXPECT_EQ(ObjectInstance::FrontSide, point.get_side());
        EXPECT_FEQ(world_normal(), point.get_geometric_normal());
    }

    TEST_CASE_F(ProceduralDerivatives_TransformPositionAndUnitNormals, ProceduralFixture)
    {
        ShadingPoint point;
        ASSERT_TRUE(m_intersector.trace(ray_to_plane(false), point));
        EXPECT_FEQ(world_vector(NormalProbeObject::dpdu()), point.get_dpdu(0));
        EXPECT_FEQ(world_vector(NormalProbeObject::dpdv()), point.get_dpdv(0));
        EXPECT_FEQ_EPS(world_normal_derivative(true), point.get_dndu(0), NormalDerivativeTolerance);
        EXPECT_FEQ_EPS(world_normal_derivative(false), point.get_dndv(0), NormalDerivativeTolerance);
        EXPECT_FEQ(0.0, dot(point.get_dndu(0), point.get_original_shading_normal()));
    }

    TEST_CASE_F(ProceduralDerivatives_CopyAssignmentAndSideChangesPreserveInputs, ProceduralFixture)
    {
        ShadingPoint original;
        ASSERT_TRUE(m_intersector.trace(ray_to_plane(true), original));
        ShadingPoint copy(original);
        EXPECT_FEQ(world_vector(NormalProbeObject::dpdu()), copy.get_dpdu(0));
        EXPECT_FEQ_EPS(-world_normal_derivative(true), copy.get_dndu(0), NormalDerivativeTolerance);
        EXPECT_FEQ_EPS(-world_normal_derivative(false), original.get_dndv(0), NormalDerivativeTolerance);
        copy = original;
        EXPECT_FEQ(world_vector(NormalProbeObject::dpdv()), copy.get_dpdv(0));
        EXPECT_FEQ_EPS(-world_normal_derivative(false), copy.get_dndv(0), NormalDerivativeTolerance);
        copy.flip_side();
        EXPECT_FEQ_EPS(world_normal_derivative(true), copy.get_dndu(0), NormalDerivativeTolerance);
        EXPECT_FEQ(world_vector(NormalProbeObject::dpdu()), copy.get_dpdu(0));
    }

    TEST_CASE_F(ProceduralDerivatives_UseTheGeometricSurfaceForRayFootprints, ProceduralFixture)
    {
        m_probe->m_shading_offset = 0.1 * NormalProbeObject::dpdu();
        ShadingPoint point;
        ASSERT_TRUE(m_intersector.trace(ray_with_differentials(), point));
        EXPECT_FEQ_EPS(0.01 * world_vector(NormalProbeObject::dpdu()), point.get_dpdx(), RayFootprintTolerance);
        EXPECT_FEQ_EPS(0.01 * world_vector(NormalProbeObject::dpdv()), point.get_dpdy(), RayFootprintTolerance);
        EXPECT_FEQ(Vector2f(0.01f, 0.0f), point.get_duvdx(0));
        EXPECT_FEQ(Vector2f(0.0f, 0.01f), point.get_duvdy(0));
        point.flip_side();
        EXPECT_FEQ(Vector2f(0.01f, 0.0f), point.get_duvdx(0));
        EXPECT_FEQ(Vector2f(0.0f, 0.01f), point.get_duvdy(0));
    }

    TEST_CASE_F(ProceduralDerivatives_UnavailableMappingHasZeroUVFootprint, ProceduralFixture)
    {
        m_probe->m_supply_derivatives = false;
        ShadingPoint point;
        ASSERT_TRUE(m_intersector.trace(ray_with_differentials(), point));
        EXPECT_FEQ(Vector3d(0.0), point.get_dpdu(0));
        EXPECT_FEQ(Vector3d(0.0), point.get_dpdv(0));
        EXPECT_FEQ(Vector3d(0.0), point.get_dndu(0));
        EXPECT_FEQ(Vector2f(0.0f), point.get_duvdx(0));
        EXPECT_FEQ(Vector2f(0.0f), point.get_duvdy(0));
        EXPECT_TRUE(is_normalized(point.get_shading_basis().get_normal()));
    }

    TEST_CASE_F(ProceduralDerivatives_SmallScalePreservesUVFootprints, SmallProceduralFixture)
    {
        ShadingPoint point;
        ASSERT_TRUE(m_intersector.trace(ray_with_differentials(), point));
        EXPECT_FEQ(Vector2f(0.01f, 0.0f), point.get_duvdx(0));
        EXPECT_FEQ(Vector2f(0.0f, 0.01f), point.get_duvdy(0));
    }

    TEST_CASE_F(ProceduralDerivatives_LargeScalePreservesUVFootprints, LargeProceduralFixture)
    {
        ShadingPoint point;
        ASSERT_TRUE(m_intersector.trace(ray_with_differentials(), point));
        EXPECT_FEQ(Vector2f(0.01f, 0.0f), point.get_duvdx(0));
        EXPECT_FEQ(Vector2f(0.0f, 0.01f), point.get_duvdy(0));
    }

    TEST_CASE_F(SphereDerivatives_MatchNativeMapping, SphereFixture)
    {
        const ShadingRay ray(Vector3d(2.0, 0.0, 0.0), Vector3d(-1.0, 0.0, 0.0),
            ShadingRay::Time(), VisibilityFlags::CameraRay, 0);
        ProceduralObject::IntersectionResult result;
        m_analytic->intersect(ray, result);
        ASSERT_TRUE(result.m_hit);
        ASSERT_TRUE(result.m_derivatives.m_valid);
        EXPECT_FEQ(Vector2f(0.0f, 0.5f), result.m_uv);
        EXPECT_FEQ(Vector3d(0.0, 0.0, -TwoPi<double>()), result.m_derivatives.m_dpdu);
        EXPECT_FEQ(Vector3d(0.0, Pi<double>(), 0.0), result.m_derivatives.m_dpdv);
        EXPECT_FEQ(result.m_derivatives.m_dpdu, result.m_derivatives.m_dndu);
        EXPECT_FEQ(result.m_derivatives.m_dpdv, result.m_derivatives.m_dndv);
    }

    TEST_CASE_F(SphereDerivatives_PoleHasUnavailableMapping, SphereFixture)
    {
        const ShadingRay ray(Vector3d(0.0, 2.0, 0.0), Vector3d(0.0, -1.0, 0.0),
            ShadingRay::Time(), VisibilityFlags::CameraRay, 0);
        ProceduralObject::IntersectionResult result;
        m_analytic->intersect(ray, result);
        ASSERT_TRUE(result.m_hit);
        EXPECT_FALSE(result.m_derivatives.m_valid);
    }

    TEST_CASE_F(RectangleDerivatives_RetainWidthAndHeight, RectangleFixture)
    {
        const ShadingRay ray(Vector3d(0.0, 2.0, 0.0), Vector3d(0.0, -1.0, 0.0),
            ShadingRay::Time(), VisibilityFlags::CameraRay, 0);
        ProceduralObject::IntersectionResult result;
        m_analytic->intersect(ray, result);
        ASSERT_TRUE(result.m_hit);
        ASSERT_TRUE(result.m_derivatives.m_valid);
        EXPECT_FEQ(Vector3d(4.0, 0.0, 0.0), result.m_derivatives.m_dpdu);
        EXPECT_FEQ(Vector3d(0.0, 0.0, -6.0), result.m_derivatives.m_dpdv);
        EXPECT_FEQ(Vector3d(0.0), result.m_derivatives.m_dndu);
        EXPECT_FEQ(Vector3d(0.0), result.m_derivatives.m_dndv);
    }

    TEST_CASE_F(DiskDerivatives_MatchPolarMapping, DiskFixture)
    {
        const ShadingRay ray(Vector3d(1.0, 2.0, 0.0), Vector3d(0.0, -1.0, 0.0),
            ShadingRay::Time(), VisibilityFlags::CameraRay, 0);
        ProceduralObject::IntersectionResult result;
        m_analytic->intersect(ray, result);
        ASSERT_TRUE(result.m_hit);
        ASSERT_TRUE(result.m_derivatives.m_valid);
        EXPECT_FEQ(Vector2f(0.25f, 0.5f), result.m_uv);
        EXPECT_FEQ(Vector3d(0.0, 0.0, -TwoPi<double>()), result.m_derivatives.m_dpdu);
        EXPECT_FEQ(Vector3d(-2.0, 0.0, 0.0), result.m_derivatives.m_dpdv);
        EXPECT_FEQ(Vector3d(0.0), result.m_derivatives.m_dndu);
        EXPECT_FEQ(Vector3d(0.0), result.m_derivatives.m_dndv);
    }

    TEST_CASE_F(DiskDerivatives_CenterHasUnavailableMapping, DiskFixture)
    {
        const ShadingRay ray(Vector3d(0.0, 2.0, 0.0), Vector3d(0.0, -1.0, 0.0),
            ShadingRay::Time(), VisibilityFlags::CameraRay, 0);
        ProceduralObject::IntersectionResult result;
        m_analytic->intersect(ray, result);
        ASSERT_TRUE(result.m_hit);
        EXPECT_FALSE(result.m_derivatives.m_valid);
    }

    TEST_CASE_F(SampledProceduralPoint_NativeAttributesPreserveMaterialAndCurvature, ProceduralFixture)
    {
        const Vector3d n = NormalProbeObject::normal();
        const ShadingRay object_ray(2.0 * n, -n,
            ShadingRay::Time::create_with_normalized_time(0.0f, 0.0f, 1.0f),
            VisibilityFlags::CameraRay, 0);
        ProceduralObject::IntersectionResult attributes = {};
        m_probe->intersect(object_ray, attributes);
        ASSERT_TRUE(attributes.m_hit);
        const ShadingRay ray = ray_to_plane(false);
        ShadingPoint traced, sampled;
        ASSERT_TRUE(m_intersector.trace(ray, traced));
        m_intersector.make_procedural_surface_shading_point(sampled, ray,
            m_scene.assembly_instances().get_by_name("assembly_instance"),
            m_assembly_transform, 0, 0, traced.get_point(), attributes);
        ShadingPoint copied(sampled);
        for (const ShadingPoint* actual : {&sampled, &copied})
        {
            EXPECT_EQ(size_t(2), actual->get_primitive_attribute_index());
            EXPECT_FEQ(traced.get_uv(0), actual->get_uv(0));
            EXPECT_FEQ_EPS(traced.get_shading_normal(), actual->get_shading_normal(), NormalDerivativeTolerance);
            EXPECT_FEQ_EPS(traced.get_dpdu(0), actual->get_dpdu(0), NormalDerivativeTolerance);
            EXPECT_FEQ_EPS(traced.get_dpdv(0), actual->get_dpdv(0), NormalDerivativeTolerance);
            EXPECT_FEQ_EPS(traced.get_dndu(0), actual->get_dndu(0), NormalDerivativeTolerance);
            EXPECT_FEQ_EPS(traced.get_dndv(0), actual->get_dndv(0), NormalDerivativeTolerance);
        }
    }

    TEST_CASE_F(SampledProceduralPoint_PreservesSuppliedPointUVAndTangents, ProceduralFixture)
    {
        const ShadingRay ray = ray_to_plane(false);
        const Vector3d point = m_assembly_transform.point_to_parent(Vector3d(0.0));
        const Vector3d dpdu = m_object_transform.vector_to_parent(Vector3d(4.0, -2.0, 0.0));
        const Vector3d dpdv = m_object_transform.vector_to_parent(Vector3d(0.0, 3.0, -2.0));
        const Vector2f uv(0.25f, 0.75f);
        ShadingPoint hit;
        ASSERT_TRUE(m_intersector.trace(ray, hit));
        ASSERT_TRUE(square_norm(hit.get_dndu(0)) > 0.0);
        m_intersector.make_procedural_surface_shading_point(hit, ray, uv,
            m_scene.assembly_instances().get_by_name("assembly_instance"),
            m_assembly_transform, 0, 0, point, world_normal(), dpdu, dpdv);
        EXPECT_FEQ(point, hit.get_point());
        EXPECT_FEQ(uv, hit.get_uv(0));
        EXPECT_FEQ(dpdu, hit.get_dpdu(0));
        EXPECT_FEQ(dpdv, hit.get_dpdv(0));
        EXPECT_FEQ(Vector3d(0.0), hit.get_dndu(0));
        EXPECT_FEQ(Vector3d(0.0), hit.get_dndv(0));
        EXPECT_FEQ(world_normal(), hit.get_geometric_normal());
        EXPECT_EQ(ObjectInstance::FrontSide, hit.get_side());
        const Basis3d& basis = hit.get_shading_basis();
        EXPECT_FEQ(0.0, dot(basis.get_tangent_u(), basis.get_tangent_v()));
        EXPECT_FEQ(world_normal(), basis.get_normal());
    }

    TEST_CASE_F(SampledProceduralPoint_BackFaceAndCopyPreservePrimaryInputs, ProceduralFixture)
    {
        const ShadingRay ray = ray_to_plane(true);
        const Vector3d point = m_assembly_transform.point_to_parent(Vector3d(0.0));
        const Vector3d dpdu = m_object_transform.vector_to_parent(Vector3d(4.0, -2.0, 0.0));
        const Vector3d dpdv = m_object_transform.vector_to_parent(Vector3d(0.0, 3.0, -2.0));
        ShadingPoint hit;
        m_intersector.make_procedural_surface_shading_point(hit, ray, Vector2f(0.25f, 0.75f),
            m_scene.assembly_instances().get_by_name("assembly_instance"),
            m_assembly_transform, 0, 0, point, world_normal(), dpdu, dpdv);
        EXPECT_EQ(ObjectInstance::BackSide, hit.get_side());
        EXPECT_FEQ(-world_normal(), hit.get_geometric_normal());
        ShadingPoint copy(hit);
        EXPECT_FEQ(point, copy.get_point());
        EXPECT_FEQ(Vector2f(0.25f, 0.75f), copy.get_uv(0));
        EXPECT_FEQ(-world_normal(), copy.get_original_shading_normal());
        EXPECT_EQ(ObjectInstance::BackSide, copy.get_side());
        EXPECT_EQ(size_t(0), copy.get_primitive_attribute_index());
        EXPECT_FEQ(dpdu, copy.get_dpdu(0));
        EXPECT_FEQ(dpdv, copy.get_dpdv(0));
    }

    TEST_CASE_F(Trace_GivenAssemblyContainingEmptyBoundingBoxAndRayWithTMaxInsideAssembly_ReturnsFalse, Fixture<false>)
    {
        const ShadingRay ray(
            Vector3d(0.0, 0.0, 2.0),
            Vector3d(0.0, 0.0, -1.0),
            0.0,                                // tmin
            2.0,                                // tmax
            ShadingRay::Time(),
            VisibilityFlags::CameraRay,
            0);                                 // depth

        ShadingPoint shading_point;
        const bool hit = m_intersector.trace(ray, shading_point);

        EXPECT_FALSE(hit);
    }

    TEST_CASE_F(TraceProbe_GivenAssemblyContainingEmptyBoundingBoxAndRayWithTMaxInsideAssembly_ReturnsFalse, Fixture<false>)
    {
        const ShadingRay ray(
            Vector3d(0.0, 0.0, 2.0),
            Vector3d(0.0, 0.0, -1.0),
            0.0,                                // tmin
            2.0,                                // tmax
            ShadingRay::Time(),
            VisibilityFlags::CameraRay,
            0);                                 // depth

        const bool hit = m_intersector.trace_probe(ray);

        EXPECT_FALSE(hit);
    }

#ifdef APPLESEED_WITH_EMBREE

    TEST_CASE_F(Trace_Embree_GivenAssemblyContainingEmptyBoundingBoxAndRayWithTMaxInsideAssembly_ReturnsFalse, Fixture<true>)
    {
        const ShadingRay ray(
            Vector3d(0.0, 0.0, 2.0),
            Vector3d(0.0, 0.0, -1.0),
            0.0,                                // tmin
            2.0,                                // tmax
            ShadingRay::Time(),
            VisibilityFlags::CameraRay,
            0);                                 // depth

        ShadingPoint shading_point;
        const bool hit = m_intersector.trace(ray, shading_point);

        EXPECT_FALSE(hit);
    }

    TEST_CASE_F(TraceProbe_Embree_GivenAssemblyContainingEmptyBoundingBoxAndRayWithTMaxInsideAssembly_ReturnsFalse, Fixture<true>)
    {
        const ShadingRay ray(
            Vector3d(0.0, 0.0, 2.0),
            Vector3d(0.0, 0.0, -1.0),
            0.0,                                // tmin
            2.0,                                // tmax
            ShadingRay::Time(),
            VisibilityFlags::CameraRay,
            0);                                 // depth

        const bool hit = m_intersector.trace_probe(ray);

        EXPECT_FALSE(hit);
    }

#endif  // APPLESEED_WITH_EMBREE
}
