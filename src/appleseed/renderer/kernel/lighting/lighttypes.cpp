
//
// This source file is part of appleseed.
// Visit https://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2019 Esteban Tovagliari, The appleseedhq Organization
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
#include "lighttypes.h"

// appleseed.renderer headers.
#include "renderer/kernel/intersection/intersector.h"
#include "renderer/kernel/lighting/lightsample.h"
#include "renderer/kernel/shading/shadingpoint.h"
#include "renderer/modeling/object/diskobject.h"
#include "renderer/modeling/object/rectangleobject.h"
#include "renderer/modeling/object/sphereobject.h"
#include "renderer/modeling/scene/assembly.h"
#include "renderer/modeling/scene/objectinstance.h"

// appleseed.foundation headers.
#include "foundation/math/basis.h"
#include "foundation/math/distance.h"
#include "foundation/math/fp.h"
#include "foundation/math/intersection/rayparallelogram.h"
#include "foundation/math/intersection/raytrianglemt.h"
#include "foundation/math/sampling/mappings.h"

// Standard headers.
#include <cstdint>
#include <stdexcept>

using namespace foundation;

namespace renderer
{

namespace
{
    // Affine area Jacobian for a unit object-space geometric normal.
    double compute_surface_area_scale(
        const Transformd& transform,
        const Vector3d& normal)
    {
        const Vector3d x = transform.vector_to_parent(Vector3d(1.0, 0.0, 0.0));
        const Vector3d y = transform.vector_to_parent(Vector3d(0.0, 1.0, 0.0));
        const Vector3d z = transform.vector_to_parent(Vector3d(0.0, 0.0, 1.0));
        const double scale = std::abs(dot(x, cross(y, z))) * norm(transform.normal_to_parent(normal));
        if (!(scale > 0.0) || !std::isfinite(scale))
            throw std::runtime_error("Invalid transform for procedural surface sampling");
        return scale;
    }
}

//
// EmittingShape class implementation.
//
// References:
//
//   [1] Monte Carlo Techniques for Direct Lighting Calculations.
//       http://www.cs.virginia.edu/~jdl/bib/globillum/mis/shirley96.pdf
//
//   [2] Stratified Sampling of Spherical Triangles.
//       https://www.graphics.cornell.edu/pubs/1995/Arv95c.pdf
//
//   [3] An Area-Preserving Parametrization for Spherical Rectangles.
//       https://www.arnoldrenderer.com/research/egsr2013_spherical_rectangle.pdf
//

EmittingShape EmittingShape::create_triangle_shape(
    const AssemblyInstance*     assembly_instance,
    const size_t                object_instance_index,
    const size_t                primitive_index,
    const Material*             material,
    const double                area,
    const Vector3d&             v0,
    const Vector3d&             v1,
    const Vector3d&             v2,
    const Vector3d&             n0,
    const Vector3d&             n1,
    const Vector3d&             n2,
    const Vector3d&             geometric_normal)
{
    EmittingShape shape(
        TriangleShape,
        assembly_instance,
        object_instance_index,
        primitive_index,
        material);

    shape.m_geom.m_triangle.m_v0 = v0;
    shape.m_geom.m_triangle.m_v1 = v1;
    shape.m_geom.m_triangle.m_v2 = v2;
    shape.m_geom.m_triangle.m_n0 = n0;
    shape.m_geom.m_triangle.m_n1 = n1;
    shape.m_geom.m_triangle.m_n2 = n2;
    shape.m_geom.m_triangle.m_geometric_normal = geometric_normal;
    shape.m_geom.m_triangle.m_plane_dist = -dot(v0, geometric_normal);

    shape.m_bbox.invalidate();
    shape.m_bbox.insert(v0);
    shape.m_bbox.insert(v1);
    shape.m_bbox.insert(v2);

    shape.m_centroid = (v0 + v1 + v2) * (1.0 / 3.0);

    shape.m_area = static_cast<float>(area);
    shape.m_rcp_area = shape.m_area != 0.0f ? 1.0f / shape.m_area : FP<float>().snan();

    return shape;
}

EmittingShape EmittingShape::create_rectangle_shape(
    const AssemblyInstance*     assembly_instance,
    const size_t                object_instance_index,
    const Material*             material,
    const double                area,
    const Vector3d&             o,
    const Vector3d&             x,
    const Vector3d&             y,
    const Vector3d&             n)
{
    EmittingShape shape(
        RectangleShape,
        assembly_instance,
        object_instance_index,
        0,
        material);

    shape.m_geom.m_rectangle.m_origin = o;
    shape.m_geom.m_rectangle.m_x = x;
    shape.m_geom.m_rectangle.m_y = y;
    shape.m_geom.m_rectangle.m_width = norm(x);
    shape.m_geom.m_rectangle.m_height = norm(y);
    shape.m_geom.m_rectangle.m_geometric_normal = n;
    shape.m_geom.m_rectangle.m_plane_dist = -dot(o, n);

    shape.m_bbox.invalidate();
    shape.m_bbox.insert(o);
    shape.m_bbox.insert(o + x);
    shape.m_bbox.insert(o + y);
    shape.m_bbox.insert(o + x + y);
    shape.m_centroid = o + 0.5 * (x + y);

    shape.m_area = static_cast<float>(area);
    shape.m_rcp_area = shape.m_area != 0.0f ? 1.0f / shape.m_area : FP<float>().snan();

    return shape;
}

EmittingShape EmittingShape::create_sphere_shape(
    const AssemblyInstance*     assembly_instance,
    const size_t                object_instance_index,
    const Material*             material,
    const double                area,
    const Vector3d&             center,
    const double                radius)
{
    EmittingShape shape(
        SphereShape,
        assembly_instance,
        object_instance_index,
        0,
        material);

    shape.m_geom.m_sphere.m_center = center;
    shape.m_geom.m_sphere.m_radius = radius;

    shape.m_bbox = AABB3d(center - Vector3d(radius), center + Vector3d(radius));
    shape.m_centroid = center;

    shape.m_area = static_cast<float>(area);
    shape.m_rcp_area = shape.m_area != 0.0f ? 1.0f / shape.m_area : FP<float>().snan();

    return shape;
}

EmittingShape EmittingShape::create_disk_shape(
    const AssemblyInstance*     assembly_instance,
    const size_t                object_instance_index,
    const Material*             material,
    const double                area,
    const Vector3d&             c,
    const Vector3d&             n,
    const Vector3d&             x,
    const Vector3d&             y)
{
    EmittingShape shape(
        DiskShape,
        assembly_instance,
        object_instance_index,
        0,
        material);

    shape.m_geom.m_disk.m_center = c;
    shape.m_geom.m_disk.m_geometric_normal = n;
    shape.m_geom.m_disk.m_x = x;
    shape.m_geom.m_disk.m_y = y;

    // Each coordinate of an affine disk has this exact sinusoidal extent.
    Vector3d extent;
    for (size_t axis = 0; axis < 3; ++axis)
        extent[axis] = std::hypot(x[axis], y[axis]);
    shape.m_bbox = AABB3d(c - extent, c + extent);
    shape.m_centroid = c;

    shape.m_area = static_cast<float>(area);
    shape.m_rcp_area = shape.m_area != 0.0f ? 1.0f / shape.m_area : FP<float>().snan();

    return shape;
}

EmittingShape::EmittingShape(
    const ShapeType             shape_type,
    const AssemblyInstance*     assembly_instance,
    const size_t                object_instance_index,
    const size_t                primitive_index,
    const Material*             material)
{
    m_assembly_instance_and_type.set(
        assembly_instance,
        static_cast<std::uint16_t>(shape_type));

    m_object_instance_index = object_instance_index;
    m_primitive_index = primitive_index;
    m_back_side = false;
    m_material = material;
    m_assembly_instance_transform = nullptr;
    m_shape_prob = 0.0f;
    m_average_flux = 1.0f;
}

void EmittingShape::sample_uniform(
    const Vector2f&             s,
    const float                 shape_prob,
    LightSample&                light_sample) const
{
    if (get_shape_type() == ProceduralShape)
        throw std::runtime_error("Weighted procedural emitters require sample()");
    // Store a pointer to the emitting shape.
    light_sample.m_shape = this;
    light_sample.m_light = nullptr;
    light_sample.m_valid = true;
    light_sample.m_estimator_weight = 1.0f;

    switch (get_shape_type())
    {
      case TriangleShape:
        {
            // Uniformly sample the surface of the shape.
            const Vector3f bary = sample_triangle_uniform(s);

            // Set the parametric coordinates.
            light_sample.m_param_coords[0] = bary[0];
            light_sample.m_param_coords[1] = bary[1];

            // Compute the world space position of the sample.
            light_sample.m_point =
                  static_cast<double>(bary[0]) * m_geom.m_triangle.m_v0
                + static_cast<double>(bary[1]) * m_geom.m_triangle.m_v1
                + static_cast<double>(bary[2]) * m_geom.m_triangle.m_v2;

            // Compute the world space shading normal at the position of the sample.
            light_sample.m_shading_normal =
                  static_cast<double>(bary[0]) * m_geom.m_triangle.m_n0
                + static_cast<double>(bary[1]) * m_geom.m_triangle.m_n1
                + static_cast<double>(bary[2]) * m_geom.m_triangle.m_n2;
            light_sample.m_shading_normal = normalize(light_sample.m_shading_normal);

            // Set the world space geometric normal.
            light_sample.m_geometric_normal = m_geom.m_triangle.m_geometric_normal;
        }
        break;

      case RectangleShape:
        {
            // Set the parametric coordinates.
            light_sample.m_param_coords = s;

            // Compute the world space position of the sample.
            light_sample.m_point =
                m_geom.m_rectangle.m_origin +
                static_cast<double>(s[0]) * m_geom.m_rectangle.m_x +
                static_cast<double>(s[1]) * m_geom.m_rectangle.m_y;

            // Set the world space shading and geometric normals.
            light_sample.m_shading_normal = m_geom.m_rectangle.m_geometric_normal;
            light_sample.m_geometric_normal = m_geom.m_rectangle.m_geometric_normal;
        }
        break;

      case SphereShape:
        {
            // Set the parametric coordinates.
            light_sample.m_param_coords = s;

            // Uniformly sample the surface of the shape.
            const Vector3d n = sample_sphere_uniform(Vector2d(s));

            // Compute the world space position of the sample.
            light_sample.m_point = m_geom.m_sphere.m_center + n * m_geom.m_sphere.m_radius;

            // Set the world space shading and geometric normals.
            light_sample.m_shading_normal = n;
            light_sample.m_geometric_normal = n;
        }
        break;

      case DiskShape:
        {
            // Uniformly sample the surface of the shape.
            const Vector2f param_coords = sample_disk_uniform(s);

            // Set the parametric coordinates.
            light_sample.m_param_coords = param_coords;

            // Compute the world space position of the sample.
            light_sample.m_point =
                m_geom.m_disk.m_center +
                static_cast<double>(param_coords[0]) * m_geom.m_disk.m_x +
                static_cast<double>(param_coords[1]) * m_geom.m_disk.m_y;

            // Set the world space shading and geometric normals.
            light_sample.m_shading_normal = m_geom.m_disk.m_geometric_normal;
            light_sample.m_geometric_normal = m_geom.m_disk.m_geometric_normal;
        }
        break;

      default:
        assert(!"Unknown emitter shape type");
        break;
    }

    // Compute the probability density of this sample.
    light_sample.m_probability = shape_prob * m_rcp_area;
}

void EmittingShape::make_shading_point(
    ShadingPoint&               shading_point,
    const ShadingRay&           ray,
    const Vector2f&             param_coords,
    const Intersector&          intersector,
    const ProceduralObject::IntersectionResult* supplied_attributes) const
{
    const AssemblyInstance* instance = get_assembly_instance();
    const TransformSequence& sequence = m_assembly_instance_transform
        ? *m_assembly_instance_transform
        : instance->transform_sequence();
    const Transformd& transform = sequence.get_earliest_transform();
    if (get_shape_type() == TriangleShape)
    {
        intersector.make_triangle_shading_point(shading_point, ray, param_coords,
            instance, transform, get_object_instance_index(), get_primitive_index(),
            m_shape_support_plane, &sequence);
        return;
    }

    const ObjectInstance* object_instance = instance->get_assembly().object_instances()
        .get_by_index(get_object_instance_index());
    const Object& object = object_instance->get_object();
    if (get_shape_type() == ProceduralShape)
    {
        assert(supplied_attributes);
        intersector.make_procedural_surface_shading_point(shading_point, ray,
            instance, transform, get_object_instance_index(), get_primitive_index(),
            ray.m_org, *supplied_attributes, &sequence);
        return;
    }
    const Vector3d point = object_instance->get_transform().point_to_local(
        transform.point_to_local(ray.m_org));
    ProceduralObject::IntersectionResult attributes = {};
    switch (get_shape_type())
    {
      case RectangleShape:
        static_cast<const RectangleObject&>(object).get_surface_attributes(point, attributes);
        break;
      case SphereShape:
        static_cast<const SphereObject&>(object).get_surface_attributes(point, attributes);
        break;
      case DiskShape:
        static_cast<const DiskObject&>(object).get_surface_attributes(point, attributes);
        break;
      default:
        assert(!"Unknown emitter shape type");
        return;
    }
    intersector.make_procedural_surface_shading_point(shading_point, ray,
        instance, transform, get_object_instance_index(), get_primitive_index(),
        ray.m_org, attributes, &sequence);
}

EmittingShape EmittingShape::create_procedural_shape(
    const AssemblyInstance* assembly_instance,
    const size_t object_instance_index,
    const Material* material,
    const double area,
    const double reference_area,
    const AABB3d& bbox,
    const bool back_side)
{
    EmittingShape shape(ProceduralShape, assembly_instance, object_instance_index, 0, material);
    shape.m_geom.m_procedural.m_reference_area = reference_area;
    shape.m_area = static_cast<float>(area);
    shape.m_rcp_area = 1.0f / shape.m_area;
    shape.m_bbox = bbox;
    shape.m_centroid = bbox.center();
    shape.m_back_side = back_side;
    return shape;
}

Transformd EmittingShape::get_object_to_world_transform() const
{
    const AssemblyInstance* instance = get_assembly_instance();
    const ObjectInstance* object_instance = instance->get_assembly().object_instances()
        .get_by_index(get_object_instance_index());
    const TransformSequence& sequence = m_assembly_instance_transform
        ? *m_assembly_instance_transform : instance->transform_sequence();
    return object_instance->get_transform() * sequence.get_earliest_transform();
}

void EmittingShape::sample(
    const Vector2f& s,
    const float shape_prob,
    LightSample& light_sample,
    SamplingContext* sampling_context) const
{
    if (get_shape_type() != ProceduralShape)
    {
        sample_uniform(s, shape_prob, light_sample);
        return;
    }
    if (!sampling_context)
        throw std::runtime_error("Procedural emitter sampling requires a sampling context");

    light_sample.m_shape = this;
    light_sample.m_light = nullptr;
    light_sample.m_valid = false;
    light_sample.m_estimator_weight = 0.0f;
    light_sample.m_probability = 0.0f;
    sampling_context->split_in_place(3, 1);
    const Vector3d extra = sampling_context->next2<Vector3d>();
    const double variates[ProceduralObject::SurfaceSampleDimensions] = {s[0], s[1], extra[0], extra[1], extra[2]};
    const ObjectInstance* instance = get_assembly_instance()->get_assembly().object_instances()
        .get_by_index(get_object_instance_index());
    const auto& object = static_cast<const ProceduralObject&>(instance->get_object());
    ProceduralObject::SurfaceSample sample = {};
    if (!object.sample_surface(variates, sample))
        return;

    const auto& attributes = sample.m_attributes;
    const double NormalLengthTolerance = 1.0e-6;
    if (!(sample.m_estimator_weight > 0.0) || !std::isfinite(sample.m_estimator_weight)
        || attributes.m_material_slot != 0
        || std::abs(square_norm(attributes.m_geometric_normal) - 1.0) > NormalLengthTolerance
        || std::abs(square_norm(attributes.m_shading_normal) - 1.0) > NormalLengthTolerance)
        throw std::runtime_error("Invalid procedural emitter surface sample");
    for (size_t i = 0; i < 3; ++i)
    {
        if (!std::isfinite(sample.m_point[i]) || !std::isfinite(attributes.m_geometric_normal[i])
            || !std::isfinite(attributes.m_shading_normal[i]))
            throw std::runtime_error("Nonfinite procedural emitter surface sample");
        if (attributes.m_derivatives.m_valid
            && (!std::isfinite(attributes.m_derivatives.m_dpdu[i]) || !std::isfinite(attributes.m_derivatives.m_dpdv[i])
                || !std::isfinite(attributes.m_derivatives.m_dndu[i]) || !std::isfinite(attributes.m_derivatives.m_dndv[i])))
            throw std::runtime_error("Nonfinite procedural emitter surface derivatives");
    }
    for (size_t i = 0; i < 2; ++i)
        if (!std::isfinite(attributes.m_uv[i]))
            throw std::runtime_error("Nonfinite procedural emitter surface coordinates");
    const Transformd transform = get_object_to_world_transform();
    const double scale = compute_surface_area_scale(transform, attributes.m_geometric_normal);
    light_sample.m_probability = static_cast<float>(shape_prob / (m_geom.m_procedural.m_reference_area * scale));
    light_sample.m_estimator_weight = static_cast<float>(sample.m_estimator_weight);
    if (!(light_sample.m_probability > 0.0f) || !std::isfinite(light_sample.m_probability)
        || !(light_sample.m_estimator_weight > 0.0f) || !std::isfinite(light_sample.m_estimator_weight))
        throw std::runtime_error("Procedural emitter sampling weight is outside renderer range");
    const double orientation = m_back_side != instance->must_flip_normals() ? -1.0 : 1.0;
    light_sample.m_point = transform.point_to_parent(sample.m_point);
    light_sample.m_geometric_normal = orientation * normalize(transform.normal_to_parent(attributes.m_geometric_normal));
    light_sample.m_shading_normal = orientation * normalize(transform.normal_to_parent(attributes.m_shading_normal));
    light_sample.m_param_coords = attributes.m_uv;
    light_sample.m_surface_attributes = attributes;
    for (size_t i = 0; i < 3; ++i)
        if (!std::isfinite(light_sample.m_point[i]) || !std::isfinite(light_sample.m_geometric_normal[i])
            || !std::isfinite(light_sample.m_shading_normal[i]))
            throw std::runtime_error("Nonfinite transformed procedural emitter surface sample");
    light_sample.m_valid = true;
}

float EmittingShape::evaluate_sampling_density(const ShadingPoint& shading_point) const
{
    if (get_shape_type() != ProceduralShape)
        return evaluate_pdf_uniform();
    if (m_shape_prob == 0.0f)
        return 0.0f;
    const Transformd transform = get_object_to_world_transform();
    const Vector3d normal = normalize(transform.normal_to_local(shading_point.get_geometric_normal()));
    const double scale = compute_surface_area_scale(transform, normal);
    const float density = static_cast<float>(m_shape_prob / (m_geom.m_procedural.m_reference_area * scale));
    if (!(density > 0.0f) || !std::isfinite(density))
        throw std::runtime_error("Procedural emitter reference density is outside renderer range");
    return density;
}

void EmittingShape::estimate_flux()
{
    // todo:
    /*
    if (constant EDF)
        return EDF->radiance();

    // Varying EDF or OSL emission case.
    for i = 0..N:
    {
        s = random2d()
        make_shading_point(shading_point, p, d, s, intersector);
        radiance += eval EDF or ShaderGroup
    }

    radiance /= N;
    return radiance;
    */

    m_average_flux = 1.0f;
    m_max_flux = 1.0f;
}

}   // namespace renderer
