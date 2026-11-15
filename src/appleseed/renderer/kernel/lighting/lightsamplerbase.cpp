
//
// This source file is part of appleseed.
// Visit https://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2017-2018 Petra Gospodnetic, The appleseedhq Organization
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

// Interface header.
#include "lightsamplerbase.h"

// appleseed.renderer headers
#include "renderer/global/globaltypes.h"
#include "renderer/kernel/intersection/intersector.h"
#include "renderer/modeling/edf/edf.h"
#include "renderer/modeling/light/light.h"
#include "renderer/modeling/object/diskobject.h"
#include "renderer/modeling/object/meshobject.h"
#include "renderer/modeling/object/rectangleobject.h"
#include "renderer/modeling/object/sphereobject.h"
#include "renderer/modeling/scene/scene.h"
#include "renderer/modeling/shadergroup/shadergroup.h"
#include "renderer/utility/triangle.h"

// appleseed.foundation headers.
#include "foundation/containers/dictionary.h"
#include "foundation/math/sampling/mappings.h"

#include <cmath>
#include <stdexcept>

using namespace foundation;

namespace renderer
{

//
// LightSamplerBase class implementation.
//

LightSamplerBase::LightSamplerBase(const ParamArray& params)
  : m_params(params)
  , m_emitting_shape_hash_table(m_shape_key_hasher)
{
}

void LightSamplerBase::sample_non_physical_light(
    const ShadingRay::Time&             time,
    const size_t                        light_index,
    LightSample&                        light_sample,
    const float                         light_prob) const
{
    light_sample.m_shape = nullptr;
    light_sample.m_valid = true;
    light_sample.m_estimator_weight = 1.0f;
    light_sample.m_time = time;
    // Fetch the light.
    const NonPhysicalLightInfo& light_info = m_non_physical_lights[light_index];
    light_sample.m_light = light_info.m_light;

    // Evaluate and store the transform of the light.
    light_sample.m_light_transform =
          light_info.m_light->get_transform()
        * light_info.m_transform_sequence.evaluate(time.m_absolute);

    // Store the probability density of this light.
    light_sample.m_probability = light_prob;
    assert(!light_sample.m_valid || light_sample.m_probability > 0.0f);
}

Dictionary LightSamplerBase::get_params_metadata()
{
    Dictionary metadata;

    metadata.insert(
        "enable_importance_sampling",
        Dictionary()
            .insert("type", "bool")
            .insert("default", "false")
            .insert("label", "Enable Importance Sampling")
            .insert("help", "Enable Importance Sampling"));

    return metadata;
}

void LightSamplerBase::insert_non_physical_light(const NonPhysicalLightInfo& info)
{
    const size_t index = m_non_physical_lights.size();
    m_non_physical_lights.push_back(info);
    m_non_physical_lights_cdf.insert(index, info.m_light->get_uncached_importance_multiplier());
}

void LightSamplerBase::insert_emitting_shape_cdf(const Material* material, const float area, const size_t index)
{
    const EDF* edf = material->get_uncached_edf();
    const float multiplier = edf ? edf->get_uncached_importance_multiplier() : 1.0f;
    m_emitting_shapes_cdf.insert(index, (m_params.m_importance_sampling ? area : 1.0f) * multiplier);
}

void LightSamplerBase::build_emitting_shape_hash_table()
{
    const size_t emitting_shape_count = m_emitting_shapes.size();

    m_emitting_shape_hash_table.resize(
        emitting_shape_count > 0 ? next_pow2(emitting_shape_count) : 0);

    for (size_t i = 0; i < emitting_shape_count; ++i)
    {
        const EmittingShape& emitting_shape = m_emitting_shapes[i];

        const EmittingShapeKey emitting_shape_key(
            emitting_shape.get_assembly_instance()->get_uid(),
            emitting_shape.get_object_instance_index(),
            emitting_shape.get_primitive_index(),
            emitting_shape.is_back_side());

        m_emitting_shape_hash_table.insert(emitting_shape_key, &emitting_shape);
    }
}

void LightSamplerBase::collect_emitting_shapes(
    const AssemblyInstanceContainer&    assembly_instances,
    const TransformSequence&            parent_transform_seq,
    const ShapeHandlingFunction&        shape_handling)
{
    for (const AssemblyInstance& assembly_instance : assembly_instances)
    {
        // Retrieve the assembly.
        const Assembly& assembly = assembly_instance.get_assembly();

        // Compute the cumulated transform sequence of this assembly instance.
        TransformSequence cumulated_transform_seq =
            assembly_instance.transform_sequence() * parent_transform_seq;
        cumulated_transform_seq.prepare();

        // Recurse into child assembly instances.
        collect_emitting_shapes(
            assembly.assembly_instances(),
            cumulated_transform_seq,
            shape_handling);

        // Collect emitting shapes from this assembly instance.
        collect_emitting_shapes(
            assembly,
            assembly_instance,
            cumulated_transform_seq,
            shape_handling);
    }
}

void LightSamplerBase::collect_emitting_shapes(
    const Assembly&                     assembly,
    const AssemblyInstance&             assembly_instance,
    const TransformSequence&            transform_sequence,
    const ShapeHandlingFunction&        shape_handling)
{
    const size_t first_shape = m_emitting_shapes.size();

    // Loop over the object instances of the assembly.
    const size_t object_instance_count = assembly.object_instances().size();
    for (size_t object_instance_index = 0; object_instance_index < object_instance_count; ++object_instance_index)
    {
        // Retrieve the object instance.
        const ObjectInstance* object_instance = assembly.object_instances().get_by_index(object_instance_index);

        // Retrieve the materials of the object instance.
        const MaterialArray& front_materials = object_instance->get_front_materials();
        const MaterialArray& back_materials = object_instance->get_back_materials();

        // Skip object instances without light-emitting materials.
        if (!has_emitting_materials(front_materials) && !has_emitting_materials(back_materials))
            continue;

        // Compute the object space to world space transformation.
        // todo: add support for moving light-emitters.
        const Transformd& object_instance_transform = object_instance->get_transform();
        const Transformd& assembly_instance_transform = transform_sequence.get_earliest_transform();
        const Transformd global_transform = object_instance_transform * assembly_instance_transform;

        // Retrieve the object.
        Object& object = object_instance->get_object();

        float object_area = 0.0f;

        if (strcmp(object.get_model(), MeshObjectFactory().get_model()) == 0)
        {
            // Retrieve the tessellation of the mesh.
            const MeshObject& mesh = static_cast<const MeshObject&>(object);
            const StaticTriangleTess& tess = mesh.get_static_triangle_tess();

            // Skip object instances without light-emitting materials.
            if (!has_emitting_materials(front_materials) && !has_emitting_materials(back_materials))
                continue;

            // Loop over the triangles of the mesh.
            for (size_t triangle_index = 0, triangle_count = tess.m_primitives.size();
                triangle_index < triangle_count; ++triangle_index)
            {
                // Fetch the triangle.
                const Triangle& triangle = tess.m_primitives[triangle_index];

                // Skip triangles without a material.
                if (triangle.m_pa == Triangle::None)
                    continue;

                // Fetch the materials assigned to this triangle.
                const size_t pa_index = static_cast<size_t>(triangle.m_pa);
                const Material* front_material =
                    pa_index < front_materials.size() ? front_materials[pa_index] : nullptr;
                const Material* back_material =
                    pa_index < back_materials.size() ? back_materials[pa_index] : nullptr;

                // Skip triangles that don't emit light.
                if ((front_material == nullptr || !front_material->has_emission()) &&
                    (back_material == nullptr || !back_material->has_emission()))
                    continue;

                // Retrieve object instance space vertices of the triangle.
                const GVector3& v0_os = tess.m_vertices[triangle.m_v0];
                const GVector3& v1_os = tess.m_vertices[triangle.m_v1];
                const GVector3& v2_os = tess.m_vertices[triangle.m_v2];

                // Transform triangle vertices to assembly space.
                const GVector3 v0_as = object_instance_transform.point_to_parent(v0_os);
                const GVector3 v1_as = object_instance_transform.point_to_parent(v1_os);
                const GVector3 v2_as = object_instance_transform.point_to_parent(v2_os);

                // Compute the support plane of the hit triangle in assembly space.
                const GTriangleType triangle_geometry(v0_as, v1_as, v2_as);
                TriangleSupportPlaneType triangle_support_plane;
                triangle_support_plane.initialize(TriangleType(triangle_geometry));

                // Transform triangle vertices to world space.
                const Vector3d v0(assembly_instance_transform.point_to_parent(v0_as));
                const Vector3d v1(assembly_instance_transform.point_to_parent(v1_as));
                const Vector3d v2(assembly_instance_transform.point_to_parent(v2_as));

                // Compute the geometric normal to the triangle and the area of the triangle.
                Vector3d geometric_normal = compute_triangle_normal(v0, v1, v2);
                const double geometric_normal_norm = norm(geometric_normal);
                if (geometric_normal_norm == 0.0)
                    continue;
                const double rcp_geometric_normal_norm = 1.0 / geometric_normal_norm;
                const double rcp_area = 2.0 * rcp_geometric_normal_norm;
                const double area = 0.5 * geometric_normal_norm;
                geometric_normal *= rcp_geometric_normal_norm;
                assert(is_normalized(geometric_normal));

                // Flip the geometric normal if the object instance requests so.
                if (object_instance->must_flip_normals())
                    geometric_normal = -geometric_normal;

                Vector3d n0, n1, n2;

                if (triangle.m_n0 != Triangle::None &&
                    triangle.m_n1 != Triangle::None &&
                    triangle.m_n2 != Triangle::None)
                {
                    // Retrieve object instance space vertex normals.
                    const Vector3d n0_os = Vector3d(tess.m_vertex_normals[triangle.m_n0]);
                    const Vector3d n1_os = Vector3d(tess.m_vertex_normals[triangle.m_n1]);
                    const Vector3d n2_os = Vector3d(tess.m_vertex_normals[triangle.m_n2]);

                    // Transform vertex normals to world space.
                    n0 = normalize(global_transform.normal_to_parent(n0_os));
                    n1 = normalize(global_transform.normal_to_parent(n1_os));
                    n2 = normalize(global_transform.normal_to_parent(n2_os));

                    // Flip normals if the object instance requests so.
                    if (object_instance->must_flip_normals())
                    {
                        n0 = -n0;
                        n1 = -n1;
                        n2 = -n2;
                    }
                }
                else
                {
                    n0 = n1 = n2 = geometric_normal;
                }

                for (size_t side = 0; side < 2; ++side)
                {
                    // Retrieve the material; skip sides without a material or without emission.
                    const Material* material = side == 0 ? front_material : back_material;
                    if (material == nullptr || !material->has_emission())
                        continue;

                    // Invoke the shape handling function.
                    const bool accept_shape =
                        shape_handling(
                            material,
                            static_cast<float>(area),
                            m_emitting_shapes.size());

                    if (accept_shape)
                    {
                        // Create a light-emitting triangle.
                        auto emitting_shape = EmittingShape::create_triangle_shape(
                            &assembly_instance,
                            object_instance_index,
                            triangle_index,
                            material,
                            area,
                            v0,
                            v1,
                            v2,
                            side == 0 ? n0 : -n0,
                            side == 0 ? n1 : -n1,
                            side == 0 ? n2 : -n2,
                            side == 0 ? geometric_normal : -geometric_normal);
                        emitting_shape.m_back_side = side != 0;
                        emitting_shape.m_shape_support_plane = triangle_support_plane;
                        emitting_shape.m_area = static_cast<float>(area);
                        emitting_shape.m_rcp_area = static_cast<float>(rcp_area);

                        // Estimate radiant flux emitted by this shape.
                        emitting_shape.estimate_flux();

                        // Store the light-emitting shape.
                        m_emitting_shapes.push_back(emitting_shape);

                        // Accumulate the object area for OSL shaders.
                        object_area += emitting_shape.m_area;
                    }
                }
            }
        }
        else if (strcmp(object.get_model(), RectangleObjectFactory().get_model()) == 0)
        {
            // Fetch the materials assigned to this rectangle.
            const Material* front_material =
                front_materials.empty() ? nullptr : front_materials[0];

            const Material* back_material =
                back_materials.empty() ? nullptr : back_materials[0];

            // Skip rectangles that don't emit light.
            if ((front_material == nullptr || !front_material->has_emission()) &&
                (back_material == nullptr || !back_material->has_emission()))
                continue;

            // Retrieve the rectangle.
            const RectangleObject& rectangle = static_cast<const RectangleObject&>(object);

            // Retrieve object instance space geometry of the rectangle.
            Vector3d o, x, y, n;
            rectangle.get_origin_and_axes(o, x, y, n);

            if (object_instance->must_flip_normals())
                n = -n;

            // Transform rectangle to world space.
            o = global_transform.point_to_parent(o);
            x = global_transform.vector_to_parent(x);
            y = global_transform.vector_to_parent(y);
            n = normalize(global_transform.normal_to_parent(n));

            const double area = norm(cross(x, y));

            if (area <= 0.0)
            {
                RENDERER_LOG_WARNING(
                    "rectangle object \"%s\" has zero or negative area; it will be ignored.",
                    rectangle.get_name());
                continue;
            }

            for (size_t side = 0; side < 2; ++side)
            {
                // Retrieve the material; skip sides without a material or without emission.
                const Material* material = side == 0 ? front_material : back_material;
                if (material == nullptr || !material->has_emission())
                    continue;

                // Invoke the shape handling function.
                const bool accept_shape =
                    shape_handling(
                        material,
                        static_cast<float>(area),
                        m_emitting_shapes.size());

                if (accept_shape)
                {
                    // Create a light-emitting rectangle.
                    auto emitting_shape = EmittingShape::create_rectangle_shape(
                        &assembly_instance,
                        object_instance_index,
                        material,
                        area,
                        o,
                        x,
                        y,
                        side == 0 ? n : -n);
                    emitting_shape.m_back_side = side != 0;

                    // Estimate radiant flux emitted by this shape.
                    emitting_shape.estimate_flux();

                    // Store the light-emitting shape.
                    m_emitting_shapes.push_back(emitting_shape);

                    // Accumulate the object area for OSL shaders.
                    object_area += emitting_shape.m_area;
                }
            }
        }
        else if (strcmp(object.get_model(), SphereObjectFactory().get_model()) == 0)
        {
            // Fetch the materials assigned to this sphere.
            const Material* material = front_materials.empty() ? nullptr : front_materials[0];

            // Skip spheres that don't emit light.
            if ((material == nullptr || !material->has_emission()))
                continue;

            // Retrieve the sphere.
            const SphereObject& sphere = static_cast<const SphereObject&>(object);

            // Transform sphere to world space.
            const Matrix4d& xform = global_transform.get_local_to_parent();
            Vector3d center, scale;
            Quaterniond rot;
            xform.decompose(scale, rot, center);
            double radius = sphere.get_radius();

            if (feq(scale.x, scale.y) && feq(scale.x, scale.z))
                radius *= scale.x;
            else
            {
                RENDERER_LOG_WARNING(
                    "transform of sphere object \"%s\" has a non-uniform scale factor; scale will be ignored.",
                    sphere.get_name());
            }

            if (radius <= 0.0)
            {
                RENDERER_LOG_WARNING(
                    "sphere object \"%s\" has zero or negative radius; it will be ignored.",
                    sphere.get_name());
                continue;
            }

            const double area = FourPi<double>() * square(radius);

            // Invoke the shape handling function.
            const bool accept_shape =
                shape_handling(
                    material,
                    static_cast<float>(area),
                    m_emitting_shapes.size());

            if (accept_shape)
            {
                // Create a light-emitting rectangle.
                auto emitting_shape = EmittingShape::create_sphere_shape(
                    &assembly_instance,
                    object_instance_index,
                    material,
                    area,
                    center,
                    radius);

                // Estimate radiant flux emitted by this shape.
                emitting_shape.estimate_flux();

                // Store the light-emitting shape.
                m_emitting_shapes.push_back(emitting_shape);

                // Accumulate the object area for OSL shaders.
                object_area += emitting_shape.m_area;
            }
        }
        else if (strcmp(object.get_model(), DiskObjectFactory().get_model()) == 0)
        {
            // Fetch the materials assigned to this disk.
            const Material* material = front_materials.empty() ? nullptr : front_materials[0];

            // Skip disks that don't emit light.
            if ((material == nullptr || !material->has_emission()))
                continue;

            // Retrieve the disk.
            const DiskObject& disk = static_cast<const DiskObject&>(object);

            // Retrieve object instance space geometry of the disk.
            const double radius = disk.get_uncached_radius();
            Vector3d x, y, n;
            disk.get_axes(x, y, n);
            if (object_instance->must_flip_normals())
                n = -n;
            x = global_transform.vector_to_parent(x);
            y = global_transform.vector_to_parent(y);
            n = normalize(global_transform.normal_to_parent(n));
            const Vector3d center = global_transform.point_to_parent(disk.get_uncached_center());
            const double area = Pi<double>() * norm(cross(x, y));
            if (radius <= 0.0 || area <= 0.0)
            {
                RENDERER_LOG_WARNING(
                    "disk object \"%s\" has zero or negative area; it will be ignored.",
                    disk.get_name());
                continue;
            }

            // Invoke the shape handling function.
            const bool accept_shape =
                shape_handling(
                    material,
                    static_cast<float>(area),
                    m_emitting_shapes.size());

            if (accept_shape)
            {
                // Create a light-emitting shape.
                auto emitting_shape = EmittingShape::create_disk_shape(
                    &assembly_instance,
                    object_instance_index,
                    material,
                    area,
                    center,
                    n,
                    x,
                    y);

                // Estimate radiant flux emitted by this shape.
                emitting_shape.estimate_flux();

                // Store the light-emitting shape.
                m_emitting_shapes.push_back(emitting_shape);

                // Accumulate the object area for OSL shaders.
                object_area += emitting_shape.m_area;
            }
        }
        else if (const auto* procedural = dynamic_cast<const ProceduralObject*>(&object))
        {
            const double reference_area = procedural->get_surface_sampling_reference_area();
            if (reference_area == 0.0)
            {
                RENDERER_LOG_WARNING(
                    "procedural emitter '%s' has no surface sampler; "
                    "explicit light sampling and photon emission are unavailable.",
                    object.get_name());
                continue;
            }
            if (!(reference_area > 0.0) || !std::isfinite(reference_area)
                || procedural->get_material_slot_count() != 1 || transform_sequence.size() > 1)
                throw std::runtime_error("Invalid or moving procedural emitter '" + std::string(object.get_name()) + "'");

            const AABB3d bbox = global_transform.to_parent(AABB3d(object.compute_local_bbox()));
            // Area is estimated for importance and power metadata only. The
            // known reference density, not this estimate, weights transport.
            EmittingShape probe = EmittingShape::create_procedural_shape(
                &assembly_instance, object_instance_index, nullptr, 1.0, reference_area, bbox, false);
            probe.m_assembly_instance_transform = &transform_sequence;
            constexpr size_t SurfaceAreaSampleCount = 65536;
            SamplingContext::RNGType rng;
            SamplingContext sampling(rng, SamplingContext::QMCMode, 2, SurfaceAreaSampleCount);
            double area_sum = 0.0;
            for (size_t i = 0; i < SurfaceAreaSampleCount; ++i)
            {
                const Vector2f s = sampling.next2<Vector2f>();
                SamplingContext child = sampling.split(0, 0);
                LightSample sample;
                probe.sample(s, 1.0f, sample, &child);
                if (sample.m_valid)
                    area_sum += sample.m_estimator_weight / static_cast<double>(sample.m_probability);
            }
            const double area = area_sum / SurfaceAreaSampleCount;
            if (!(area > 0.0) || !std::isfinite(static_cast<float>(area)))
                throw std::runtime_error("Cannot estimate procedural emitter area for '" + std::string(object.get_name()) + "'");
            for (size_t side = 0; side < 2; ++side)
            {
                const MaterialArray& materials = side == 0 ? front_materials : back_materials;
                const Material* material = materials.empty() ? nullptr : materials[0];
                if (!material || !material->has_emission()
                    || !shape_handling(material, static_cast<float>(area), m_emitting_shapes.size()))
                    continue;
                EmittingShape shape = EmittingShape::create_procedural_shape(
                    &assembly_instance, object_instance_index, material, area, reference_area, bbox, side != 0);
                shape.estimate_flux();
                m_emitting_shapes.push_back(shape);
                object_area += static_cast<float>(area);
            }
        }
        else
            continue;

        store_object_area_in_shadergroups(
            &assembly_instance,
            object_instance,
            object_area,
            front_materials);

        store_object_area_in_shadergroups(
            &assembly_instance,
            object_instance,
            object_area,
            back_materials);
    }

    if (m_emitting_shapes.size() > first_shape)
    {
        m_emitting_shape_transforms.push_back(transform_sequence);
        for (size_t i = first_shape; i < m_emitting_shapes.size(); ++i)
            m_emitting_shapes[i].m_assembly_instance_transform = &m_emitting_shape_transforms.back();
    }
}

void LightSamplerBase::collect_non_physical_lights(
    const AssemblyInstanceContainer&    assembly_instances,
    const TransformSequence&            parent_transform_seq,
    const LightHandlingFunction&        light_handling)
{
    for (const AssemblyInstance& assembly_instance : assembly_instances)
    {
        // Retrieve the assembly.
        const Assembly& assembly = assembly_instance.get_assembly();

        // Compute the cumulated transform sequence of this assembly instance.
        TransformSequence cumulated_transform_seq =
            assembly_instance.transform_sequence() * parent_transform_seq;
        cumulated_transform_seq.prepare();

        // Recurse into child assembly instances.
        collect_non_physical_lights(
            assembly.assembly_instances(),
            cumulated_transform_seq,
            light_handling);

        // Collect lights from this assembly.
        collect_non_physical_lights(
            assembly,
            cumulated_transform_seq,
            light_handling);
    }
}

void LightSamplerBase::collect_non_physical_lights(
    const Assembly&                     assembly,
    const TransformSequence&            transform_sequence,
    const LightHandlingFunction&        light_handling)
{
    for (const Light& light : assembly.lights())
    {
        NonPhysicalLightInfo light_info;
        light_info.m_transform_sequence = transform_sequence;
        light_info.m_light = &light;
        light_handling(light_info);
    }
}

void LightSamplerBase::store_object_area_in_shadergroups(
    const AssemblyInstance*             assembly_instance,
    const ObjectInstance*               object_instance,
    const float                         object_area,
    const MaterialArray&                materials)
{
    for (size_t i = 0, e = materials.size(); i < e; ++i)
    {
        if (const Material* m = materials[i])
        {
            if (const ShaderGroup* sg = m->get_uncached_osl_surface())
            {
                if (sg->has_emission())
                    sg->set_surface_area(assembly_instance, object_instance, object_area);
            }
        }
    }
}

void LightSamplerBase::sample_emitting_shape(
    const ShadingRay::Time&             time,
    const Vector2f&                     s,
    const size_t                        shape_index,
    const float                         shape_prob,
    LightSample&                        light_sample,
    SamplingContext*                    sampling_context) const
{
    // Fetch the emitting shape.
    const EmittingShape& emitting_shape = m_emitting_shapes[shape_index];

    // Sample the surface, retaining null draws and estimator weights.
    light_sample.m_light = nullptr;
    light_sample.m_time = time;
    emitting_shape.sample(s, shape_prob, light_sample, sampling_context);

    assert(light_sample.m_shape);
    assert(!light_sample.m_valid || light_sample.m_probability > 0.0f);
}

void LightSamplerBase::sample_emitting_shapes(
    const ShadingRay::Time&             time,
    const Vector3f&                     s,
    LightSample&                        light_sample,
    SamplingContext*                    sampling_context) const
{
    assert(m_emitting_shapes_cdf.valid());

    // Fetch the emitting shape.
    const EmitterCDF::ItemWeightPair result = m_emitting_shapes_cdf.sample(s[0]);
    const size_t emitter_index = result.first;
    const float emitter_prob = result.second;
    const EmittingShape& emitting_shape = m_emitting_shapes[emitter_index];

    // Sample the surface, retaining null draws and estimator weights.
    light_sample.m_light = nullptr;
    light_sample.m_time = time;
    emitting_shape.sample(Vector2f(s[1], s[2]), emitter_prob, light_sample, sampling_context);

    assert(light_sample.m_shape);
    assert(!light_sample.m_valid || light_sample.m_probability > 0.0f);
}


//
// LightSamplerBase::Parameters class implementation.
//

LightSamplerBase::Parameters::Parameters(const ParamArray& params)
  : m_importance_sampling(params.get_optional<bool>("enable_importance_sampling", false))
{
}

}   // namespace renderer
