
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

// Interface header.
#include "shadingcontext.h"

// appleseed.renderer headers.
#include "renderer/kernel/intersection/intersector.h"
#include "renderer/kernel/shading/closures.h"
#include "renderer/kernel/shading/shadingpoint.h"
#include "renderer/modeling/bsdf/bsdf.h"
#include "renderer/modeling/material/material.h"
#include "renderer/kernel/shading/oslshadingsystem.h"
#include "renderer/kernel/texturing/oiiotexturesystem.h"
#include "renderer/modeling/color/colorspace.h"
#include "renderer/modeling/shadergroup/shadergroup.h"

// Standard headers.
#include <cmath>
#include <stdexcept>
#include <vector>

using namespace foundation;

namespace renderer
{

//
// ShadingContext class implementation.
//

ShadingContext::ShadingContext(
    const Intersector&      intersector,
    Tracer&                 tracer,
    TextureCache&           texture_cache,
    OIIOTextureSystem&      oiio_texture_system,
    OSLShaderGroupExec&     osl_shadergroup_exec,
    Arena&                  arena,
    const size_t            thread_index,
    ILightingEngine*        lighting_engine,
    const float             transparency_threshold,
    const size_t            max_iterations)
  : m_intersector(intersector)
  , m_tracer(tracer)
  , m_texture_cache(texture_cache)
  , m_oiio_texture_system(oiio_texture_system)
  , m_shadergroup_exec(osl_shadergroup_exec)
  , m_arena(arena)
  , m_thread_index(thread_index)
  , m_lighting_engine(lighting_engine)
  , m_transparency_threshold(transparency_threshold)
  , m_max_iterations(max_iterations)
{
}

OIIOTextureSystem&   ShadingContext::get_oiio_texture_system() const
{
    return m_oiio_texture_system;
}

OSLShadingSystem& ShadingContext::get_osl_shading_system() const
{
    return m_shadergroup_exec.m_osl_shading_system;
}

OSL::ShadingContext* ShadingContext::get_osl_shading_context() const
{
    return m_shadergroup_exec.m_osl_shading_context;
}

namespace
{
    struct InterfaceBoundary
    {
        const Material* m_material = nullptr;
        const BSDF* m_bsdf = nullptr;
        const void* m_inputs = nullptr;
        float m_alpha = 1.0f;
        bool m_optical = false;
        bool m_transmissive = false;
        bool m_volume = false;
        bool m_medium = false;
    };

    InterfaceBoundary classify_boundary(
        const ShadingContext&    context,
        const ShadingPoint&      point,
        const Material*          material)
    {
        InterfaceBoundary boundary;
        boundary.m_alpha = point.get_alpha()[0];
        boundary.m_material = material;
        if (boundary.m_material == nullptr)
            return boundary;
        const Material::RenderData& data = boundary.m_material->get_render_data();
        boundary.m_volume = data.m_volume != nullptr;
        if (data.m_shader_group && data.m_shader_group->has_transparency())
        {
            Alpha alpha;
            context.execute_osl_transparency(*data.m_shader_group, point, alpha);
            boundary.m_alpha *= alpha[0];
        }
        boundary.m_bsdf = data.m_bsdf;
        if (boundary.m_bsdf)
        {
            if (data.m_shader_group)
                context.execute_osl_shading(*data.m_shader_group, point);
            boundary.m_inputs = boundary.m_bsdf->evaluate_inputs(context, point);
            boundary.m_optical = boundary.m_bsdf->has_interior(boundary.m_inputs);
            boundary.m_transmissive = boundary.m_bsdf->has_transmission(boundary.m_inputs);
        }
        boundary.m_medium = (boundary.m_optical && !boundary.m_volume) ||
            (boundary.m_volume && data.m_bsdf == nullptr && data.m_bssrdf == nullptr);
        return boundary;
    }

    bool contains_medium(const ShadingRay& ray, const ObjectInstance* object)
    {
        for (std::uint8_t i = 0; i < ray.m_medium_count; ++i)
        {
            if (ray.m_media[i].m_object_instance == object)
                return true;
        }
        return false;
    }
}

void ShadingContext::resolve_optical_interface(SamplingContext& sampling_context, ShadingPoint& point) const
{
    using Interface = ShadingPoint::OpticalInterface;
    if (point.get_optical_interface() != Interface::Unresolved)
        return;
    point.set_no_optical_interface();
    if (!point.hit_surface())
        return;
    const ShadingRay::Medium* current = point.get_ray().get_current_medium();
    const bool lower_priority = current &&
        current->m_object_instance->get_medium_priority() > point.get_object_instance().get_medium_priority();
    const bool procedural = point.get_primitive_type() == ShadingPoint::PrimitiveProceduralSurface;
    const Material* hit_material = point.get_material();
    bool adjacent_surface = false;
    if (hit_material && hit_material->get_render_data().m_volume)
    {
        const ShadingRay& ray = point.get_ray();
        for (std::uint8_t i = 0; i < ray.m_medium_count; ++i)
        {
            const Material* material = ray.m_media[i].m_material;
            if (material && material->get_render_data().m_bsdf)
            {
                adjacent_surface = true;
                break;
            }
        }
    }
    // Native meshes need no coincident group, but their volume boundaries
    // can still exchange the active glass and fog interiors.
    if (!procedural && !lower_priority && !adjacent_surface)
        return;

    const InterfaceBoundary first = classify_boundary(*this, point, hit_material);
    // Ordinary opaque hits already own their surface. Transmissive alpha
    // layers still need classification when they coincide with glass.
    if (!first.m_medium && !first.m_transmissive)
        return;

    const bool hidden_boundary = first.m_medium && lower_priority;
    std::vector<ShadingPoint> points;
    m_intersector.collect_coincident_surfaces(point, points);
    if (points.size() < 2 && !hidden_boundary && !adjacent_surface)
        return;
    std::vector<InterfaceBoundary> boundaries;
    boundaries.reserve(points.size());
    boundaries.push_back(first);
    for (size_t i = 1; i < points.size(); ++i)
        boundaries.push_back(classify_boundary(*this, points[i], points[i].get_material()));

    std::vector<size_t> media;
    std::vector<size_t> opaque;
    bool unsupported = false;
    for (size_t i = 0; i < points.size(); ++i)
    {
        const InterfaceBoundary& boundary = boundaries[i];
        if (boundary.m_alpha <= 0.0f)
            continue;
        if (boundary.m_medium)
            media.push_back(i);
        if (boundary.m_alpha < 1.0f || (boundary.m_volume && !boundary.m_medium) ||
            (boundary.m_transmissive && !boundary.m_medium))
            unsupported = true;
        else if (!boundary.m_medium)
            opaque.push_back(i);
    }
    if (media.empty() || (media.size() == 1 && opaque.empty() &&
        !hidden_boundary && !adjacent_surface && !unsupported))
        return;
    if (unsupported)
        throw std::runtime_error("shared optical interfaces require opaque coverage and supported glass or volume interiors");
    if (opaque.size() > 1)
        throw std::runtime_error("shared optical interface has multiple opaque surface owners");

    const ShadingRay before = point.get_ray();
    ShadingRay after;
    after.copy_media_from(before);
    size_t owner = opaque.empty() ? media.front() : opaque.front();
    // An opaque contact cannot transmit. Its reflection retains the incoming
    // state and need not establish media hidden beyond that surface.
    if (opaque.empty())
    {
        // Remove every exit before adding entries so a simultaneous exchange
        // does not temporarily exceed the fixed native medium capacity.
        for (size_t i : media)
        {
            if (points[i].is_leaving())
            {
                ShadingRay next;
                next.remove_medium(after, &points[i].get_object_instance());
                after.m_medium_count = 0;
                after.copy_media_from(next);
            }
        }
        for (size_t i : media)
        {
            const ObjectInstance* object = &points[i].get_object_instance();
            if (points[i].is_entering() && !contains_medium(after, object))
            {
                if (after.m_medium_count == ShadingRay::MaxMediumCount)
                    throw std::runtime_error("too many optical media at a shared interface");
                const float ior = boundaries[i].m_optical
                    ? boundaries[i].m_bsdf->sample_ior(sampling_context, boundaries[i].m_inputs) : 1.0f;
                ShadingRay next;
                next.add_medium(after, object, boundaries[i].m_material, ior);
                after.m_medium_count = 0;
                after.copy_media_from(next);
            }
        }

        for (size_t i : media)
        {
            if (points[i].get_object_instance().get_medium_priority() >
                points[owner].get_object_instance().get_medium_priority())
                owner = i;
        }

    }
    const ShadingRay::Medium* incoming = before.get_current_medium();
    const ShadingRay::Medium* outgoing = after.get_current_medium();
    const bool same_medium = incoming && outgoing
        ? incoming->m_object_instance == outgoing->m_object_instance
        : incoming == outgoing;
    const bool displaced = opaque.empty() && same_medium;
    const size_t dominant_boundary = owner;
    const Material* surface_material = nullptr;
    if (!displaced && opaque.empty())
    {
        // Medium priority selects the occupied interior. A glass boundary
        // of either active medium supplies the shared surface scattering.
        bool glass_owner = false;
        for (size_t i : media)
        {
            const ObjectInstance* object = &points[i].get_object_instance();
            const bool active = (incoming && incoming->m_object_instance == object) ||
                (outgoing && outgoing->m_object_instance == object);
            if (boundaries[i].m_optical && active &&
                (!glass_owner || object->get_medium_priority() >
                    points[owner].get_object_instance().get_medium_priority()))
            {
                owner = i;
                glass_owner = true;
            }
        }
        if (!glass_owner)
        {
            // In overlapping geometry the fog boundary has no coincident
            // glass hit. Use the active glass material at this same point.
            const ShadingRay::Medium* selected = nullptr;
            for (const ShadingRay::Medium* medium : {incoming, outgoing})
            {
                if (medium == nullptr || medium->m_material == nullptr)
                    continue;
                const InterfaceBoundary boundary = classify_boundary(*this, point, medium->m_material);
                if (boundary.m_optical &&
                    (selected == nullptr || medium->m_object_instance->get_medium_priority() >
                        selected->m_object_instance->get_medium_priority()))
                    selected = medium;
            }
            if (selected)
                surface_material = selected->m_material;
        }
    }
    if (!displaced)
    {
        // A shared interface has one tangent plane. Intersecting boundary
        // planes describe a multi-medium corner, not a single refraction event.
        const double NormalAlignmentTolerance = 1.0e-8;
        const Vector3d normal = point.get_geometric_normal();
        for (size_t i = 0; i < points.size(); ++i)
        {
            if (boundaries[i].m_alpha <= 0.0f)
                continue;
            if (std::abs(dot(normal, points[i].get_geometric_normal())) < 1.0 - NormalAlignmentTolerance)
                throw std::runtime_error("shared optical boundaries have different tangent planes");
        }

    }
    if (!displaced && opaque.empty())
    {
        for (size_t i : media)
        {
            if (i != dominant_boundary && points[i].get_object_instance().get_medium_priority() ==
                points[dominant_boundary].get_object_instance().get_medium_priority())
                throw std::runtime_error("shared optical interfaces require distinct medium priorities");
        }
    }
    ShadingRay owner_ray = before;
    owner_ray.m_tmax = points[owner].get_distance();
    point = points[owner];
    point.set_ray(owner_ray);
    const float outside_ior = point.is_entering() ? before.get_current_ior() : after.get_current_ior();
    point.set_optical_interface(displaced ? Interface::Displaced : Interface::Surface,
        after, outside_ior, surface_material);
}

void ShadingContext::execute_osl_shading(
    const ShaderGroup&      shader_group,
    const ShadingPoint&     shading_point) const
{
    m_shadergroup_exec.execute_shading(
        shader_group,
        shading_point);
}

void ShadingContext::execute_osl_shading(
    const ShaderGroup&          shader_group,
    const ShadingPoint&         shading_point,
    const foundation::Vector3f& incident,
    const VisibilityFlags::Type ray_flags) const
{
    m_shadergroup_exec.execute_shading(shader_group, shading_point, incident, ray_flags);
}

void ShadingContext::execute_osl_subsurface(
    const ShaderGroup&      shader_group,
    const ShadingPoint&     shading_point) const
{
    m_shadergroup_exec.execute_subsurface(
        shader_group,
        shading_point);
}

void ShadingContext::execute_osl_transparency(
    const ShaderGroup&      shader_group,
    const ShadingPoint&     shading_point,
    Alpha&                  alpha) const
{
    m_shadergroup_exec.execute_transparency(
        shader_group,
        shading_point,
        alpha);
}

void ShadingContext::execute_osl_emission(
    const ShaderGroup&      shader_group,
    const ShadingPoint&     shading_point) const
{
    m_shadergroup_exec.execute_emission(
        shader_group,
        shading_point);
}

void ShadingContext::execute_osl_bump(
    const ShaderGroup&      shader_group,
    const ShadingPoint&     shading_point,
    const Vector2f&         s) const
{
    m_shadergroup_exec.execute_bump(
        shader_group,
        shading_point,
        s);
}

void ShadingContext::execute_osl_background(
    const ShaderGroup&      shader_group,
    const Vector3f&         outgoing,
    Spectrum&               value) const
{
    value.set(
        m_shadergroup_exec.execute_background(
            shader_group,
            outgoing),
        g_std_lighting_conditions,
        Spectrum::Reflectance);
}

void ShadingContext::execute_osl_npr(
    const ShaderGroup&      shader_group,
    const ShadingPoint&     shading_point) const
{
    m_shadergroup_exec.execute_npr(
        shader_group,
        shading_point);
}

void ShadingContext::execute_osl_transparency_and_matte(
    const ShaderGroup&      shader_group,
    const ShadingPoint&     shading_point) const
{
    m_shadergroup_exec.execute_transparency_and_matte(
        shader_group,
        shading_point);
}

void ShadingContext::choose_bsdf_closure_shading_basis(
    const ShadingPoint&     shading_point,
    const Vector2f&         s) const
{
    m_shadergroup_exec.choose_bsdf_closure_shading_basis(shading_point, s);
}

}   // namespace renderer
