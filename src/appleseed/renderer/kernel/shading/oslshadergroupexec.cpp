
//
// This source file is part of appleseed.
// Visit https://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2014-2018 Esteban Tovagliari, The appleseedhq Organization
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
#include "oslshadergroupexec.h"

// appleseed.renderer headers.
#include "renderer/kernel/intersection/intersector.h"
#include "renderer/kernel/shading/closures.h"
#include "renderer/kernel/shading/oslshadingsystem.h"
#include "renderer/kernel/shading/shadingpoint.h"
#include "renderer/kernel/shading/shadingray.h"
#include "renderer/modeling/bsdf/bsdf.h"
#include "renderer/modeling/shadergroup/shadergroup.h"

// Standard headers.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <memory>

using namespace foundation;

namespace renderer
{

namespace
{
    template <typename Vector>
    bool finite_bump_vector(const Vector& value)
    {
        return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
    }

    class ScopedBumpContext
      : public NonCopyable
    {
      public:
        ScopedBumpContext(OSL::ShaderGlobals& globals, OSLBumpContext& context)
          : m_globals(globals)
          , m_previous(globals.objdata)
        {
            m_globals.objdata = &context;
        }
        ~ScopedBumpContext()
        {
            m_globals.objdata = m_previous;
        }
      private:
        OSL::ShaderGlobals& m_globals;
        void* m_previous;
    };
}


//
// OSLShaderGroupExec class implementation.
//

OSLShaderGroupExec::OSLShaderGroupExec(OSLShadingSystem& shading_system, Arena& arena)
  : m_osl_shading_system(shading_system)
  , m_arena(arena)
  , m_osl_thread_info(shading_system.create_thread_info())
  , m_osl_shading_context(shading_system.get_context(m_osl_thread_info))
{
}

OSLShaderGroupExec::~OSLShaderGroupExec()
{
    if (m_osl_shading_context)
        m_osl_shading_system.release_context(m_osl_shading_context);

    if (m_osl_thread_info)
        m_osl_shading_system.destroy_thread_info(m_osl_thread_info);
}

void OSLShaderGroupExec::execute_shading(
    const ShaderGroup&              shader_group,
    const ShadingPoint&             shading_point) const
{
    do_execute(
        shader_group,
        shading_point,
        shading_point.get_ray().m_flags);
}

void OSLShaderGroupExec::execute_shading(
    const ShaderGroup&              shader_group,
    const ShadingPoint&             shading_point,
    const Vector3f&                 incident,
    const VisibilityFlags::Type     ray_flags) const
{
    assert(is_normalized(incident));
    do_execute(shader_group, shading_point, ray_flags, &incident);
}

void OSLShaderGroupExec::execute_subsurface(
    const ShaderGroup&              shader_group,
    const ShadingPoint&             shading_point) const
{
    do_execute(
        shader_group,
        shading_point,
        VisibilityFlags::SubsurfaceRay);
}

void OSLShaderGroupExec::execute_transparency(
    const ShaderGroup&              shader_group,
    const ShadingPoint&             shading_point,
    Alpha&                          alpha) const
{
    do_execute(
        shader_group,
        shading_point,
        VisibilityFlags::TransparencyRay);

    process_transparency_tree(shading_point.get_osl_shader_globals().Ci, alpha);
}

void OSLShaderGroupExec::execute_transparency_and_matte(
    const ShaderGroup&              shader_group,
    const ShadingPoint&             shading_point) const
{
    do_execute(
        shader_group,
        shading_point,
        VisibilityFlags::TransparencyRay);
}

void OSLShaderGroupExec::execute_shadow(
    const ShaderGroup&              shader_group,
    const ShadingPoint&             shading_point,
    Alpha&                          alpha) const
{
    do_execute(
        shader_group,
        shading_point,
        VisibilityFlags::ShadowRay);

    process_transparency_tree(shading_point.get_osl_shader_globals().Ci, alpha);
}

void OSLShaderGroupExec::execute_emission(
    const ShaderGroup&              shader_group,
    const ShadingPoint&             shading_point) const
{
    do_execute(
        shader_group,
        shading_point,
        VisibilityFlags::LightRay);
}

void OSLShaderGroupExec::execute_bump(
    const ShaderGroup&              shader_group,
    const ShadingPoint&             shading_point,
    const Vector2f&                 s) const
{
    // Choose between BSSRDF and BSDF.
    if (shader_group.has_subsurface() && s[0] < 0.5f)
    {
        do_execute(
            shader_group,
            shading_point,
            VisibilityFlags::SubsurfaceRay);

        CompositeSubsurfaceClosure c(
            Basis3f(shading_point.get_shading_basis()),
            shading_point.get_osl_shader_globals().Ci,
            m_arena);

        // Pick a shading basis from one of the BSSRDF closures.
        if (c.get_closure_count() > 0)
        {
            const size_t index = c.choose_closure(s[1]);
            shading_point.set_shading_basis(
                Basis3d(c.get_closure_shading_basis(index)));
        }
    }
    else
    {
        do_execute(
            shader_group,
            shading_point,
            VisibilityFlags::CameraRay);

        choose_bsdf_closure_shading_basis(shading_point, s);
    }
}

void OSLShaderGroupExec::execute_npr(
    const ShaderGroup&              shader_group,
    const ShadingPoint&             shading_point) const
{
    do_execute(
        shader_group,
        shading_point,
        VisibilityFlags::NPRRay);
}

Color3f OSLShaderGroupExec::execute_background(
    const ShaderGroup&              shader_group,
    const Vector3f&                 outgoing) const
{
    assert(m_osl_shading_context);
    assert(m_osl_thread_info);

    OSL::ShaderGlobals sg = {};
    sg.I = outgoing;
    sg.renderer = m_osl_shading_system.renderer();
    sg.raytype = VisibilityFlags::CameraRay;

    m_osl_shading_system.execute(
        m_osl_shading_context,
        *reinterpret_cast<OSL::ShaderGroup*>(shader_group.osl_shader_group()),
        sg);

    return process_background_tree(sg.Ci);
}

void OSLShaderGroupExec::do_execute(
    const ShaderGroup&              shader_group,
    const ShadingPoint&             shading_point,
    const VisibilityFlags::Type     ray_flags,
    const Vector3f*                 incident) const
{
    assert(m_osl_shading_context);
    assert(m_osl_thread_info);

    shading_point.initialize_osl_shader_globals(
        shader_group,
        ray_flags,
        m_osl_shading_system.renderer());

    auto& globals = shading_point.get_osl_shader_globals();
    if (incident && Vector3f(globals.I) != *incident)
    {
        globals.I = *incident;
        globals.dIdx = OSL::Vec3(0.0f);
        globals.dIdy = OSL::Vec3(0.0f);
    }
    // A prefix must replay upstream writes to globals exactly once.
    const OSL::ShaderGlobals input = globals;
    OSLBumpContext context{this, &shader_group, &input};
    ScopedBumpContext scope(globals, context);
    m_osl_shading_system.execute(
        m_osl_shading_context,
        *reinterpret_cast<OSL::ShaderGroup*>(shader_group.osl_shader_group()),
        globals);
}

bool OSLShaderGroupExec::recover_bump_normal(
    OSLBumpContext& state,
    const Intersector& intersector,
    const std::uint64_t layer_hash,
    OSL::Vec3* result) const
{
    if (!state.m_input || !state.m_input->renderstate || !state.m_group)
        return false;
    const OSL::ShaderGlobals& globals = *state.m_input;
    auto* prefix = static_cast<OSL::ShaderGroup*>(state.m_group->osl_bump_shader_group(layer_hash));
    const auto& surface = *static_cast<const ShadingPoint*>(globals.renderstate);
    const ShadingRay& incoming = surface.get_ray();
    if (!prefix || !surface.hit_surface() || !incoming.m_has_differentials)
        return false;

    const OIIO::ustring layer = OIIO::ustring::from_hash(layer_hash);
    const auto release_context = [&](OSL::ShadingContext* context)
    {
        m_osl_shading_system.release_context(context);
    };
    std::unique_ptr<OSL::ShadingContext, decltype(release_context)> context(
        m_osl_shading_system.get_context(m_osl_thread_info), release_context);
    if (!context)
        return false;

    const auto normal_at = [&](OSL::ShaderGlobals sample, Vector3d& normal)
    {
        const OSL::ShaderGlobals input = sample;
        OSLBumpContext sample_state{this, state.m_group, &input, &state, layer_hash};
        // Prefix trace messages and closure storage belong to this sample.
        ShadingPoint::OSLTraceData trace_data{};
        sample.tracedata = &trace_data;
        sample.Ci = nullptr;
        sample.objdata = &sample_state;
        if (!m_osl_shading_system.execute(context.get(), *prefix, sample) || sample_state.m_failed)
            return false;
        OSL::TypeDesc type;
        const auto* value = static_cast<const OSL::Vec3*>(m_osl_shading_system.get_symbol(
            *context, layer, OIIO::ustring("out_normal"), type));
        if (!value || type != OIIO::TypeNormal || !finite_bump_vector(*value))
            return false;
        normal = Vector3d(*value);
        return square_norm(normal) > 0.0;
    };

    Vector3d center_normal;
    if (!normal_at(globals, center_normal))
        return false;
    result[0] = Vector3f(center_normal);

    // A world-space field remains continuous across unrelated UV seams.
    // Only constrain its probes to a UV chart when the prefix reads that
    // chart or its tangent basis; keep the conservative guard if unknown.
    const int chart_globals = static_cast<int>(OSL::SGBits::u) |
        static_cast<int>(OSL::SGBits::v) | static_cast<int>(OSL::SGBits::dPdu) |
        static_cast<int>(OSL::SGBits::dPdv);
    int globals_read = 0;
    bool uses_chart = !m_osl_shading_system.getattribute(prefix, "globals_read", globals_read) ||
        (globals_read & chart_globals) != 0;
    int userdata_count = 0;
    OIIO::ustring* userdata_names = nullptr;
    if (!m_osl_shading_system.getattribute(prefix, "num_userdata", userdata_count) ||
        (userdata_count != 0 && !m_osl_shading_system.getattribute(
            prefix, "userdata_names", OSL::TypeDesc::PTR, &userdata_names)))
        uses_chart = true;
    else
    {
        for (int i = 0; i < userdata_count; ++i)
            if (userdata_names[i] == OIIO::ustring("Tn") || userdata_names[i] == OIIO::ustring("Bn"))
                uses_chart = true;
    }

    // Keep probes in the local surface chart. This bounds both geometric
    // linearization error and jumps across UV seams without model units.
    constexpr double MaxLinearizationError = 0.125;
    constexpr double UVPrecisionUlps = 8.0;
    constexpr double PositionPrecisionUlps = 64.0;
    constexpr double InitialPixelStep = 0.125;
    constexpr size_t MaxInitialRefinements = 8;
    constexpr size_t MaxSpacingLevels = 18;
    constexpr double ScoreGrowthLimit = 4.0;
    constexpr double RichardsonFactor = 15.0;
    constexpr double StencilCoefficientNorm = 1.5;
    const double float_epsilon = std::numeric_limits<float>::epsilon();

    for (size_t axis = 0; axis < 2; ++axis)
    {
        const Vector3d dp = axis == 0 ? surface.get_dpdx() : surface.get_dpdy();
        const Vector2d duv = uses_chart
            ? Vector2d(axis == 0 ? surface.get_duvdx(0) : surface.get_duvdy(0))
            : Vector2d(0.0);
        if (!finite_bump_vector(dp) || !std::isfinite(duv[0]) || !std::isfinite(duv[1]))
            return false;
        const double width = norm(dp);
        if (!std::isfinite(width))
            return false;
        if (!(width > 0.0))
        {
            result[axis + 1] = OSL::Vec3(0.0f);
            continue;
        }
        const Vector2f uv_point(surface.get_uv(0));
        const double uv_ulp = std::max(
            std::abs(std::nextafter(uv_point[0], std::numeric_limits<float>::infinity()) - uv_point[0]),
            std::abs(std::nextafter(uv_point[1], std::numeric_limits<float>::infinity()) - uv_point[1]));
        if (square_norm(duv) > 0.0 && !std::isfinite(uv_ulp))
            return false;
        const Vector3d origin_delta =
            (axis == 0 ? incoming.m_rx_org : incoming.m_ry_org) - incoming.m_org;
        const Vector3d direction_delta =
            (axis == 0 ? incoming.m_rx_dir : incoming.m_ry_dir) - incoming.m_dir;
        if (!finite_bump_vector(origin_delta) || !finite_bump_vector(direction_delta))
            return false;

        const auto sample_normal = [&](const double offset, Vector3d& normal)
        {
            const Vector3d shift = offset * direction_delta;
            const Vector3d direction = incoming.m_dir + shift;
            const Vector3d rx_direction = incoming.m_rx_dir + shift;
            const Vector3d ry_direction = incoming.m_ry_dir + shift;
            if (!(square_norm(direction) > 0.0) || !finite_bump_vector(direction) ||
                !(square_norm(rx_direction) > 0.0) || !finite_bump_vector(rx_direction) ||
                !(square_norm(ry_direction) > 0.0) || !finite_bump_vector(ry_direction))
                return false;
            ShadingRay ray(incoming);
            ray.m_org = incoming.m_org + offset * origin_delta;
            ray.m_dir = normalize(direction);
            ray.m_tmax = std::numeric_limits<double>::max();
            ray.m_rx_org = ray.m_org + incoming.m_rx_org - incoming.m_org;
            ray.m_ry_org = ray.m_org + incoming.m_ry_org - incoming.m_org;
            ray.m_rx_dir = normalize(rx_direction);
            ray.m_ry_dir = normalize(ry_direction);
            if (!finite_bump_vector(ray.m_org))
                return false;

            ShadingPoint neighbor;
            intersector.trace(ray, neighbor);
            if (!neighbor.hit_surface() ||
                &neighbor.get_object_instance() != &surface.get_object_instance() ||
                &neighbor.get_assembly_instance() != &surface.get_assembly_instance() ||
                neighbor.get_primitive_attribute_index() != surface.get_primitive_attribute_index() ||
                neighbor.get_side() != surface.get_side())
                return false;
            const Vector3d expected = offset * dp;
            const Vector3d displacement = neighbor.get_point() - surface.get_point();
            const double geometric_floor = PositionPrecisionUlps * std::numeric_limits<double>::epsilon() *
                std::max({norm(surface.get_point()), norm(incoming.m_org), std::numeric_limits<double>::min()});
            if (!finite_bump_vector(displacement) ||
                norm(displacement - expected) > MaxLinearizationError * std::max(norm(expected), geometric_floor))
                return false;
            if (square_norm(duv) > 0.0)
            {
                const Vector2d uv(surface.get_uv(0));
                const Vector2d delta = Vector2d(neighbor.get_uv(0)) - uv;
                const double uv_floor = UVPrecisionUlps * uv_ulp;
                if (norm(delta - offset * duv) >
                    MaxLinearizationError * std::max(std::abs(offset) * norm(duv), uv_floor))
                    return false;
            }

            neighbor.initialize_osl_shader_globals(*state.m_group,
                static_cast<VisibilityFlags::Type>(globals.raytype), m_osl_shading_system.renderer());
            auto sample = neighbor.get_osl_shader_globals();
            // Preserve an incident-direction override used by light shading.
            if (Vector3f(globals.I) != Vector3f(incoming.m_dir))
            {
                const OSL::Vec3 di = axis == 0 ? globals.dIdx : globals.dIdy;
                const OSL::Vec3 incident = globals.I + static_cast<float>(offset) * di;
                if (!finite_bump_vector(incident) || !(incident.length2() > 0.0f))
                    return false;
                sample.I = incident.normalized();
                sample.dIdx = globals.dIdx;
                sample.dIdy = globals.dIdy;
            }
            sample.Ps = globals.Ps + static_cast<float>(offset) *
                (axis == 0 ? globals.dPsdx : globals.dPsdy);
            sample.dPsdx = globals.dPsdx;
            sample.dPsdy = globals.dPsdy;
            return normal_at(sample, normal);
        };

        const auto estimate = [&](const double step, Vector3d& derivative)
        {
            Vector3d positive, negative, outer_positive, outer_negative;
            if (!sample_normal(step, positive) || !sample_normal(-step, negative) ||
                !sample_normal(2.0 * step, outer_positive) || !sample_normal(-2.0 * step, outer_negative))
                return false;
            derivative = (8.0 * (positive - negative) - (outer_positive - outer_negative)) / (12.0 * step);
            return finite_bump_vector(derivative);
        };

        // Balance truncation against float output quantization. Start with a
        // subpixel probe spanning several position ulps, then compare fourth-
        // order estimates at successive spacings. Work is bounded at seams.
        const double chart_step = square_norm(duv) > 0.0 ? PositionPrecisionUlps * uv_ulp / norm(duv) : 0.0;
        double step = std::max({InitialPixelStep, chart_step,
            PositionPrecisionUlps * float_epsilon * norm(Vector3d(globals.P)) / width});
        Vector3d previous;
        bool valid = false;
        for (size_t refinement = 0; refinement < MaxInitialRefinements; ++refinement)
        {
            if (estimate(step, previous))
            {
                valid = true;
                break;
            }
            step *= 0.5;
        }
        if (!valid)
            return false;

        Vector3d best = previous;
        double best_score = std::numeric_limits<double>::max();
        bool compared = false;
        for (size_t level = 0; level < MaxSpacingLevels; ++level)
        {
            Vector3d current;
            if (!estimate(2.0 * step, current))
                break;
            const double roundoff_floor = StencilCoefficientNorm * std::sqrt(3.0) * float_epsilon / step;
            const double score = norm(current - previous) / RichardsonFactor + roundoff_floor;
            if (score < best_score)
            {
                best = previous;
                best_score = score;
            }
            compared = true;
            if (level >= 2 && score > ScoreGrowthLimit * best_score)
                break;
            previous = current;
            step *= 2.0;
        }
        if (!compared)
        {
            Vector3d finer;
            if (!estimate(0.5 * step, finer))
                return false;
            best = finer;
        }
        if (!finite_bump_vector(best))
            return false;
        result[axis + 1] = Vector3f(best);
    }
    return true;
}

void OSLShaderGroupExec::choose_bsdf_closure_shading_basis(
    const ShadingPoint&             shading_point,
    const Vector2f&                 s) const
{
    CompositeSurfaceClosure c(
        Basis3f(shading_point.get_shading_basis()),
        shading_point.get_osl_shader_globals().Ci,
        m_arena);

    float pdfs[CompositeSurfaceClosure::MaxClosureEntries];
    const size_t num_closures = c.compute_pdfs(ScatteringMode::All, pdfs);
    if (num_closures == 0)
        return;

    const size_t index = c.choose_closure(s[1], num_closures, pdfs);
    shading_point.set_shading_basis(
        Basis3d(c.get_closure_shading_basis(index)));
}

}   // namespace renderer
