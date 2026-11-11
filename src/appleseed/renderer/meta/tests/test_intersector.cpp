
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
#include "renderer/kernel/shading/shadingpoint.h"
#include "renderer/kernel/shading/shadingray.h"
#include "renderer/kernel/texturing/texturecache.h"
#include "renderer/kernel/texturing/texturestore.h"
#include "renderer/modeling/object/object.h"
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
#include "foundation/containers/dictionary.h"
#include "foundation/math/matrix.h"
#include "foundation/math/transform.h"
#include "foundation/math/vector.h"
#include "foundation/memory/autoreleaseptr.h"
#include "foundation/utility/test.h"
#include "foundation/utility/iostreamop.h"

using namespace foundation;
using namespace renderer;

TEST_SUITE(Renderer_Kernel_Intersection_Intersector)
{
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

        void intersect(const ShadingRay& ray, IntersectionResult& result) const override
        {
            const double denominator = dot(ray.m_dir, normal());
            const double distance = -dot(ray.m_org, normal()) / denominator;
            result.m_hit = distance >= ray.m_tmin && distance < ray.m_tmax;
            if (result.m_hit)
            {
                result.m_distance = distance;
                result.m_geometric_normal = normal();
                result.m_shading_normal = normal();
                result.m_uv = Vector2f(0.25f, 0.75f);
                result.m_material_slot = 2;
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

    struct ProceduralTestScene : public TestSceneBase
    {
        NormalProbeObject* m_probe;
        Transformd m_object_transform;
        Transformd m_assembly_transform;

        ProceduralTestScene()
          : m_probe(new NormalProbeObject())
          , m_object_transform(Matrix4d::make_scaling(Vector3d(2.0, 3.0, 4.0)))
          , m_assembly_transform(Matrix4d::make_translation(Vector3d(5.0, 6.0, 7.0)))
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

        Vector3d world_normal() const
        {
            return normalize(m_assembly_transform.normal_to_parent(
                m_object_transform.normal_to_parent(NormalProbeObject::normal())));
        }
    };

    using ProceduralFixture = Fixture<false, ProceduralTestScene>;

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

    TEST_CASE_F(SampledProceduralPoint_PreservesSuppliedPointUVAndTangents, ProceduralFixture)
    {
        const ShadingRay ray = ray_to_plane(false);
        const Vector3d point = m_assembly_transform.point_to_parent(Vector3d(0.0));
        const Vector3d dpdu = m_object_transform.vector_to_parent(Vector3d(4.0, -2.0, 0.0));
        const Vector3d dpdv = m_object_transform.vector_to_parent(Vector3d(0.0, 3.0, -2.0));
        const Vector2f uv(0.25f, 0.75f);
        ShadingPoint hit;
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
