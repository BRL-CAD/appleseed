
//
// This source file is part of appleseed.
// Visit https://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2013 Esteban Tovagliari, Jupiter Jazz Limited
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
#include "rendererservices.h"

// appleseed.renderer headers.
#include "renderer/global/globallogger.h"
#include "renderer/kernel/intersection/intersector.h"
#include "renderer/kernel/intersection/refining.h"
#include "renderer/kernel/shading/shadingpoint.h"
#include "renderer/kernel/texturing/texturecache.h"
#include "renderer/modeling/camera/camera.h"
#include "renderer/modeling/frame/frame.h"
#include "renderer/modeling/project/project.h"
#include "renderer/modeling/scene/scene.h"
#include "renderer/modeling/scene/visibilityflags.h"

// appleseed.foundation headers.
#include "foundation/core/version.h"
#include "foundation/image/canvasproperties.h"
#include "foundation/image/image.h"
#include "foundation/utility/searchpaths.h"

// Standard headers.
#include <filesystem>
#include <limits>
#include <stdexcept>

using namespace foundation;

namespace renderer
{

//
// RendererServices class implementation.
//

namespace
{
    OIIO::TypeDesc g_float_array2_typedesc(OIIO::TypeDesc::FLOAT, 2);
    OIIO::TypeDesc g_float_array4_typedesc(OIIO::TypeDesc::FLOAT, 4);
    OIIO::TypeDesc g_int_array2_typedesc(OIIO::TypeDesc::INT, 2);

    OIIO::ustring g_empty_ustr;

    OIIO::ustring g_orthographic_ustr("orthographic");
    OIIO::ustring g_perspective_ustr("perspective");
    OIIO::ustring g_spherical_ustr("spherical");
    OIIO::ustring g_unknown_proj_ustr("unknown");
    OIIO::ustring g_trace_ustr("trace");
    OIIO::ustring g_hit_ustr("hit");
    OIIO::ustring g_hitdist_ustr("hitdist");
    OIIO::ustring g_N_ustr("N");
    OIIO::ustring g_Ng_ustr("Ng");
    OIIO::ustring g_P_ustr("P");
    OIIO::ustring g_u_ustr("u");
    OIIO::ustring g_v_ustr("v");

    // Coordinate systems.
    OIIO::ustring g_camera_ustr("camera");
    OIIO::ustring g_common_ustr("common");
    OIIO::ustring g_NDC_ustr("NDC");
    OIIO::ustring g_object_ustr("object");
    OIIO::ustring g_raster_ustr("raster");
    OIIO::ustring g_screen_ustr("screen");
    OIIO::ustring g_shader_ustr("shader");
    OIIO::ustring g_world_ustr("world");

    bool get_name_attribute(const Entity& entity, const OIIO::TypeDesc type, void* value)
    {
        if (type != OIIO::TypeString)
            return false;

        const auto* name = static_cast<const OIIO::ustring*>(entity.get_name_as_ustring());
        *static_cast<OIIO::ustringhash*>(value) = OIIO::ustringhash(*name);
        return true;
    }
}

RendererServices::RendererServices(
    const Project&              project,
    OIIO::TextureSystem&        texture_sys,
    const bool                  project_rooted_textures)
  : OSL::RendererServices(&texture_sys)
  , m_texture_sys(texture_sys)
  , m_project(project)
  , m_texture_store(nullptr)
  , m_project_rooted_textures(project_rooted_textures)
{
    // Set up attribute getters.
    m_global_attr_getters[OIIO::ustringhash("object:object_instance_id")] = {&RendererServices::get_attr_object_instance_id, true};
    m_global_attr_getters[OIIO::ustringhash("object:object_instance_index")] = {&RendererServices::get_attr_object_instance_index, true};
    m_global_attr_getters[OIIO::ustringhash("object:assembly_instance_id")] = {&RendererServices::get_attr_assembly_instance_id, true};
    m_global_attr_getters[OIIO::ustringhash("object:assembly_name")] = {&RendererServices::get_attr_assembly_name, true};
    m_global_attr_getters[OIIO::ustringhash("object:assembly_instance_name")] = {&RendererServices::get_attr_assembly_instance_name, true};
    m_global_attr_getters[OIIO::ustringhash("object:object_instance_name")] = {&RendererServices::get_attr_object_instance_name, true};
    m_global_attr_getters[OIIO::ustringhash("object:object_name")] = {&RendererServices::get_attr_object_name, true};

    m_global_attr_getters[OIIO::ustringhash("camera:resolution")] = {&RendererServices::get_attr_camera_resolution, false};
    m_global_attr_getters[OIIO::ustringhash("camera:projection")] = {&RendererServices::get_attr_camera_projection, false};
    m_global_attr_getters[OIIO::ustringhash("camera:pixelaspect")] = {&RendererServices::get_attr_camera_pixelaspect, false};
    m_global_attr_getters[OIIO::ustringhash("camera:screen_window")] = {&RendererServices::get_attr_camera_screen_window, false};
    m_global_attr_getters[OIIO::ustringhash("camera:fov")] = {&RendererServices::get_attr_camera_fov, false};
    m_global_attr_getters[OIIO::ustringhash("camera:clip")] = {&RendererServices::get_attr_camera_clip, false};
    m_global_attr_getters[OIIO::ustringhash("camera:clip_near")] = {&RendererServices::get_attr_camera_clip_near, false};
    m_global_attr_getters[OIIO::ustringhash("camera:clip_far")] = {&RendererServices::get_attr_camera_clip_far, false};
    m_global_attr_getters[OIIO::ustringhash("camera:shutter")] = {&RendererServices::get_attr_camera_shutter, false};
    m_global_attr_getters[OIIO::ustringhash("camera:shutter_open")] = {&RendererServices::get_attr_camera_shutter_open, false};
    m_global_attr_getters[OIIO::ustringhash("camera:shutter_close")] = {&RendererServices::get_attr_camera_shutter_close, false};

    m_global_attr_getters[OIIO::ustringhash("path:ray_depth")] = {&RendererServices::get_attr_ray_depth, true};
    m_global_attr_getters[OIIO::ustringhash("path:ray_length")] = {&RendererServices::get_attr_ray_length, true};
    m_global_attr_getters[OIIO::ustringhash("path:ray_ior")] = {&RendererServices::get_attr_ray_ior, true};
    m_global_attr_getters[OIIO::ustringhash("path:outside_ior")] = {&RendererServices::get_attr_outside_ior, true};
    m_global_attr_getters[OIIO::ustringhash("path:ray_has_differentials")] = {&RendererServices::get_attr_ray_has_differentials, true};

    m_global_attr_getters[OIIO::ustringhash("surface_shader:diffuse")] = {&RendererServices::get_attr_surface_shader_diffuse, true};
    m_global_attr_getters[OIIO::ustringhash("surface_shader:glossy")] = {&RendererServices::get_attr_surface_shader_glossy, true};
    m_global_attr_getters[OIIO::ustringhash("surface_shader:emission")] = {&RendererServices::get_attr_surface_shader_emission, true};

    m_global_attr_getters[OIIO::ustringhash("appleseed:version_major")] = {&RendererServices::get_attr_appleseed_version_major, false};
    m_global_attr_getters[OIIO::ustringhash("appleseed:version_minor")] = {&RendererServices::get_attr_appleseed_version_minor, false};
    m_global_attr_getters[OIIO::ustringhash("appleseed:version_patch")] = {&RendererServices::get_attr_appleseed_version_patch, false};
    m_global_attr_getters[OIIO::ustringhash("appleseed:version")] = {&RendererServices::get_attr_appleseed_version, false};

    m_global_attr_getters[OIIO::ustringhash("geom:normal")] = {&RendererServices::get_attr_shading_normal, true};

    // Set up user data getters.
    m_global_user_data_getters[OIIO::ustringhash("Tn")] = &RendererServices::get_user_data_tn;
    m_global_user_data_getters[OIIO::ustringhash("Bn")] = &RendererServices::get_user_data_bn;
    m_global_user_data_getters[OIIO::ustringhash("dNdu")] = &RendererServices::get_user_data_dndu;
    m_global_user_data_getters[OIIO::ustringhash("dNdv")] = &RendererServices::get_user_data_dndv;
    m_global_user_data_getters[OIIO::ustringhash("vertex_color")] = &RendererServices::get_user_data_vertex_color;
}

void RendererServices::initialize(TextureStore& texture_store)
{
    m_texture_store = &texture_store;
    m_camera = m_project.get_uncached_active_camera();

    if (m_camera)
    {
        m_cam_projection_str =
            strcmp(m_camera->get_model(), "orthographic_camera") == 0 ? g_orthographic_ustr :
            strcmp(m_camera->get_model(), "pinhole_camera") == 0 ? g_perspective_ustr :
            strcmp(m_camera->get_model(), "thinlens_camera") == 0 ? g_perspective_ustr :
            strcmp(m_camera->get_model(), "spherical_camera") == 0 ? g_spherical_ustr :
            g_unknown_proj_ustr;

        m_shutter[0] = m_camera->get_shutter_open_begin_time();
        m_shutter[1] = m_camera->get_shutter_close_end_time();
        m_shutter_interval = m_camera->get_shutter_time_interval();
    }

    const CanvasProperties& props = m_project.get_frame()->image().properties();
    m_resolution[0] = static_cast<int>(props.m_canvas_width);
    m_resolution[1] = static_cast<int>(props.m_canvas_height);
}

// OSL runtime callbacks bypass get_texture_handle for dynamic names, so
// cached handles and uncached lookups must share the same path policy.
OIIO::ustring RendererServices::texture_filename(OIIO::ustring filename) const
{
    if (!m_project_rooted_textures || filename.empty())
        return filename;
    const std::filesystem::path path = std::filesystem::u8path(filename.c_str());
    if (path.is_absolute())
        return filename;
    // Drive-relative names depend on process state outside the project.
    if (path.has_root_name())
        throw std::runtime_error("project-rooted textures require fully absolute or relative filenames");
    const SearchPaths& search_paths = m_project.search_paths();
    if (!search_paths.has_root_path())
        throw std::runtime_error("project-rooted textures require a project root path");
    const auto root = std::filesystem::absolute(
        std::filesystem::u8path(search_paths.get_root_path().c_str()));
    return OIIO::ustring((root / path).u8string());
}

OIIO::ustringhash RendererServices::texture_filename(
    OIIO::ustringhash filename, TextureHandle* handle) const
{
    if (handle || !m_project_rooted_textures)
        return filename;
    return OIIO::ustringhash(texture_filename(OIIO::ustring::from_hash(filename.hash())));
}

RendererServices::TextureHandle* RendererServices::get_texture_handle(
    OIIO::ustring filename, OSL::ShadingContext* context, const OSL::TextureOpt* options)
{
    return OSL::RendererServices::get_texture_handle(texture_filename(filename), context, options);
}

bool RendererServices::texture(
    OIIO::ustringhash filename, TextureHandle* handle, TexturePerthread* thread,
    OSL::TextureOpt& options, OSL::ShaderGlobals* sg,
    float s, float t, float dsdx, float dtdx, float dsdy, float dtdy,
    int channels, float* result, float* dresultds, float* dresultdt,
    OIIO::ustringhash* error)
{
    return OSL::RendererServices::texture(texture_filename(filename, handle), handle,
        thread, options, sg, s, t, dsdx, dtdx, dsdy, dtdy, channels,
        result, dresultds, dresultdt, error);
}

bool RendererServices::texture3d(
    OIIO::ustringhash filename, TextureHandle* handle, TexturePerthread* thread,
    OSL::TextureOpt& options, OSL::ShaderGlobals* sg,
    const OSL::Vec3& p, const OSL::Vec3& dpdx, const OSL::Vec3& dpdy,
    const OSL::Vec3& dpdz, int channels, float* result,
    float* dresultds, float* dresultdt, float* dresultdr, OIIO::ustringhash* error)
{
    return OSL::RendererServices::texture3d(texture_filename(filename, handle), handle,
        thread, options, sg, p, dpdx, dpdy, dpdz, channels,
        result, dresultds, dresultdt, dresultdr, error);
}

bool RendererServices::environment(
    OIIO::ustringhash filename, TextureHandle* handle, TexturePerthread* thread,
    OSL::TextureOpt& options, OSL::ShaderGlobals* sg,
    const OSL::Vec3& r, const OSL::Vec3& drdx, const OSL::Vec3& drdy,
    int channels, float* result, float* dresultds, float* dresultdt,
    OIIO::ustringhash* error)
{
    return OSL::RendererServices::environment(texture_filename(filename, handle), handle,
        thread, options, sg, r, drdx, drdy, channels, result, dresultds, dresultdt, error);
}

bool RendererServices::get_texture_info(
    OIIO::ustringhash filename, TextureHandle* handle, TexturePerthread* thread,
    OSL::ShaderGlobals* sg, int subimage, OIIO::ustringhash name,
    OIIO::TypeDesc type, void* data, OIIO::ustringhash* error)
{
    return OSL::RendererServices::get_texture_info(texture_filename(filename, handle),
        handle, thread, sg, subimage, name, type, data, error);
}

bool RendererServices::get_texture_info(
    OIIO::ustringhash filename, TextureHandle* handle, float s, float t,
    TexturePerthread* thread, OSL::ShaderGlobals* sg, int subimage,
    OIIO::ustringhash name, OIIO::TypeDesc type, void* data, OIIO::ustringhash* error)
{
    return OSL::RendererServices::get_texture_info(texture_filename(filename, handle),
        handle, s, t, thread, sg, subimage, name, type, data, error);
}

OIIO::TextureSystem* RendererServices::texturesys() const
{
    return &m_texture_sys;
}

bool RendererServices::get_matrix(
    OSL::ShaderGlobals*         sg,
    OSL::Matrix44&              result,
    OSL::TransformationPtr      xform,
    float                       time)
{
    if (!xform)
        return false;

    const ShadingPoint::OSLObjectTransformInfo* obj_xform =
        reinterpret_cast<const ShadingPoint::OSLObjectTransformInfo*>(xform);

    result = obj_xform->get_transform(time);
    return true;
}

bool RendererServices::get_inverse_matrix(
    OSL::ShaderGlobals*         sg,
    OSL::Matrix44&              result,
    OSL::TransformationPtr      xform,
    float                       time)
{
    if (!xform)
        return false;

    const ShadingPoint::OSLObjectTransformInfo* obj_xform =
        reinterpret_cast<const ShadingPoint::OSLObjectTransformInfo*>(xform);

    result = obj_xform->get_inverse_transform(time);
    return true;
}

bool RendererServices::get_matrix(
    OSL::ShaderGlobals*         sg,
    OSL::Matrix44&              result,
    OSL::TransformationPtr      xform)
{
    if (!xform)
        return false;

    const ShadingPoint::OSLObjectTransformInfo* obj_xform =
        reinterpret_cast<const ShadingPoint::OSLObjectTransformInfo*>(xform);

    if (obj_xform->is_animated())
        return false;

    result = obj_xform->get_transform();
    return true;
}

bool RendererServices::get_inverse_matrix(
    OSL::ShaderGlobals*         sg,
    OSL::Matrix44&              result,
    OSL::TransformationPtr      xform)
{
    if (!xform)
        return false;

    const ShadingPoint::OSLObjectTransformInfo* obj_xform =
        reinterpret_cast<const ShadingPoint::OSLObjectTransformInfo*>(xform);

    if (obj_xform->is_animated())
        return false;

    result = obj_xform->get_inverse_transform();
    return true;
}

bool RendererServices::get_matrix(
    OSL::ShaderGlobals*         sg,
    OSL::Matrix44&              result,
    OIIO::ustringhash           from,
    float                       time)
{
    if (from == g_camera_ustr)
    {
        Transformd scratch;
        const Transformd& transform =
            m_camera->transform_sequence().evaluate(time, scratch);
        result = Matrix4f(transform.get_local_to_parent());
        return true;
    }

    return false;
}

bool RendererServices::get_inverse_matrix(
    OSL::ShaderGlobals*         sg,
    OSL::Matrix44&              result,
    OSL::ustringhash            to,
    float                       time)
{
    if (to == g_camera_ustr)
    {
        Transformd scratch;
        const Transformd& transform =
            m_camera->transform_sequence().evaluate(time, scratch);
        result = Matrix4f(transform.get_parent_to_local());
        return true;
    }

    return OSL::RendererServices::get_inverse_matrix(
        sg,
        result,
        to,
        time);
}

bool RendererServices::get_matrix(
    OSL::ShaderGlobals*         sg,
    OSL::Matrix44&              result,
    OIIO::ustringhash           from)
{
    if (from == g_camera_ustr)
    {
        if (m_camera->transform_sequence().size() > 1)
            return false;

        const Transformd& scratch =
            m_camera->transform_sequence().get_earliest_transform();
        result = Matrix4f(scratch.get_local_to_parent());
        return true;
    }

    return false;
}

bool RendererServices::get_inverse_matrix(
    OSL::ShaderGlobals*         sg,
    OSL::Matrix44&              result,
    OSL::ustringhash            to)
{
    if (to == g_camera_ustr)
    {
        if (m_camera->transform_sequence().size() > 1)
            return false;

        const Transformd& scratch =
            m_camera->transform_sequence().get_earliest_transform();
        result = Matrix4f(scratch.get_parent_to_local());
        return true;
    }

    return OSL::RendererServices::get_inverse_matrix(sg, result, to);
}

namespace
{
    bool can_transform(const OSL::TypeDesc::VECSEMANTICS vectype)
    {
        // We only transform points and vectors for now.
        return
            vectype == OSL::TypeDesc::POINT ||
            vectype == OSL::TypeDesc::VECTOR;
    }

    void transform(
        const OSL::Vec3&                  Pin,
        const OSL::Matrix44&              m,
        const OSL::TypeDesc::VECSEMANTICS vectype,
        OSL::Vec3&                        Pout)
    {
        if (vectype == OSL::TypeDesc::POINT)
            Pout = Pin * m;
        else if (vectype == OSL::TypeDesc::VECTOR)
            m.multDirMatrix(Pin, Pout);
    }
}

bool RendererServices::transform_points(
    OSL::ShaderGlobals*         sg,
    OSL::ustringhash            from,
    OSL::ustringhash            to,
    float                       time,
    const OSL::Vec3*            Pin,
    OSL::Vec3*                  Pout,
    int                         npoints,
    OSL::TypeDesc::VECSEMANTICS vectype)
{
    if (!can_transform(vectype))
        return false;

    if (to == g_NDC_ustr || to == g_raster_ustr)
    {
        if (from == g_world_ustr || from == g_common_ustr || from == g_shader_ustr)
        {
            for (int i = 0; i < npoints; ++i)
                Pout[i] = Pin[i];
        }
        else if (from == g_object_ustr)
        {
            // Convert from object to world.
            OSL::Matrix44 m;
            get_matrix(sg, m, sg->object2common, time);

            for (int i = 0; i < npoints; ++i)
                transform(Pin[i], m, vectype, Pout[i]);
        }
        else if (from == g_camera_ustr)
        {
            OSL::Matrix44 m;
            get_matrix(sg, m, g_camera_ustr, time);

            // Convert from camera to world.
            for (int i = 0; i < npoints; ++i)
                transform(Pin[i], m, vectype, Pout[i]);
        }
        else return false;

        // Transform to NDC.
        if (vectype == OSL::TypeDesc::VECTOR)
        {
            Vector2d ndc_p(-1.0, -1.0);

            if (sg)
            {
                m_camera->project_point(
                    time,
                    Vector3d(sg->P.x, sg->P.y, sg->P.z),
                    ndc_p);
            }

            for (int i = 0; i < npoints; ++i)
            {
                const OSL::Vec3 Q = sg->P + Pout[i];

                Vector2d ndc_q(-1.0, -1.0);
                m_camera->project_point(time, Vector3d(Q.x, Q.y, Q.z), ndc_q);
                Pout[i] = OSL::Vec3(
                    static_cast<float>(ndc_q[0] - ndc_p[0]),
                    static_cast<float>(ndc_q[1] - ndc_p[1]),
                    0.0f);
            }
        }
        else
        {
            assert(vectype == OSL::TypeDesc::POINT);

            for (int i = 0; i < npoints; ++i)
            {
                Vector2d ndc(-1.0, -1.0);
                m_camera->project_point(time, Vector3d(Pout[i].x, Pout[i].y, Pout[i].z), ndc);
                Pout[i] = OSL::Vec3(
                    static_cast<float>(ndc[0]),
                    static_cast<float>(ndc[1]),
                    0.0f);
            }
        }

        if (to == g_raster_ustr)
        {
            // Transform from NDC to raster.
            for (int i = 0; i < npoints; ++i)
            {
                Pout[i].x *= m_resolution[0];
                Pout[i].y *= m_resolution[1];
            }
        }

        return true;
    }

    return false;
}

bool RendererServices::trace(
    TraceOpt&                   options,
    OSL::ShaderGlobals*         sg,
    const OSL::Vec3&            P,
    const OSL::Vec3&            dPdx,
    const OSL::Vec3&            dPdy,
    const OSL::Vec3&            R,
    const OSL::Vec3&            dRdx,
    const OSL::Vec3&            dRdy)
{
    assert(m_texture_store);

    const ShadingPoint* parent =
        reinterpret_cast<const ShadingPoint*>(sg->renderstate);

    Vector3d pos;
    const ShadingPoint* origin_shading_point;

    if (P == sg->P)
    {
        Vector3d front(P);
        Vector3d back = front;

        fixed_offset(
            parent->get_point(),
            parent->get_geometric_normal(),
            front,
            back);

        pos = sg->N.dot(R) >= 0.0f ? front : back;
        origin_shading_point = parent;
    }
    else
    {
        pos = Vector3d(P);
        origin_shading_point = nullptr;
    }

    const Vector3d dir(R);
    const ShadingRay ray(
        pos,
        normalize(dir),
        options.mindist,
        options.maxdist,
        parent->get_ray().m_time,
        VisibilityFlags::ProbeRay,
        parent->get_ray().m_depth + 1);

    // todo: move this out of the hot code path (but it must remain thread-local).
    TextureCache texture_cache(*m_texture_store);
    Intersector intersector(m_project.get_trace_context(), texture_cache);

    ShadingPoint shading_point;
    intersector.trace(
        ray,
        shading_point,
        origin_shading_point);

    ShadingPoint::OSLTraceData* trace_data =
        reinterpret_cast<ShadingPoint::OSLTraceData*>(sg->tracedata);

    trace_data->m_traced = true;
    trace_data->m_hit = shading_point.hit_surface();

    if (trace_data->m_hit)
    {
        trace_data->m_P = Imath::V3d(shading_point.get_point());
        trace_data->m_hit_distance = static_cast<float>(shading_point.get_distance());
        trace_data->m_N = Imath::V3d(shading_point.get_shading_normal());
        trace_data->m_Ng = Imath::V3d(shading_point.get_geometric_normal());
        const Vector2f& uv = shading_point.get_uv(0);
        trace_data->m_u = uv[0];
        trace_data->m_v = uv[1];
        return true;
    }

    return false;
}

bool RendererServices::getmessage(
    OSL::ShaderGlobals*         sg,
    OIIO::ustringhash           source,
    OIIO::ustringhash           name,
    OIIO::TypeDesc              type,
    void*                       val,
    bool                        derivatives)
{
    const ShadingPoint::OSLTraceData* trace_data =
        reinterpret_cast<ShadingPoint::OSLTraceData*>(sg->tracedata);

    if (!trace_data->m_traced || source != g_trace_ustr)
        return false;

    if (name == g_hit_ustr && type == OIIO::TypeInt)
        reinterpret_cast<int*>(val)[0] = trace_data->m_hit ? 1 : 0;
    // A miss has no surface data; leave the destination unchanged.
    else if (!trace_data->m_hit)
        return false;
    else if (name == g_hitdist_ustr && type == OIIO::TypeFloat)
        reinterpret_cast<float*>(val)[0] = trace_data->m_hit_distance;
    else if (name == g_N_ustr && type == OIIO::TypeNormal)
        *reinterpret_cast<OSL::Vec3*>(val) = trace_data->m_N;
    else if (name == g_Ng_ustr && type == OIIO::TypeNormal)
        *reinterpret_cast<OSL::Vec3*>(val) = trace_data->m_Ng;
    else if (name == g_P_ustr && type == OIIO::TypePoint)
        *reinterpret_cast<OSL::Vec3*>(val) = trace_data->m_P;
    else if (name == g_u_ustr && type == OIIO::TypeFloat)
        reinterpret_cast<float*>(val)[0] = trace_data->m_u;
    else if (name == g_v_ustr && type == OIIO::TypeFloat)
        reinterpret_cast<float*>(val)[0] = trace_data->m_v;
    else
        return false;

    // For now, set derivatives to zero.
    if (derivatives)
        memset(reinterpret_cast<char*>(val) + type.size(), 0, 2 * type.size());

    return true;
}

bool RendererServices::get_attribute(
    OSL::ShaderGlobals*         sg,
    bool                        derivatives,
    OIIO::ustringhash           object,
    OIIO::TypeDesc              type,
    OIIO::ustringhash           name,
    void*                       val)
{
    // We don't support getting attributes from named objects, yet.
    if (object != g_empty_ustr)
        return false;

    // Try global attributes.
    AttrGetterMapType::const_iterator i = m_global_attr_getters.find(name);
    if (i != m_global_attr_getters.end())
    {
        const AttributeGetter& getter = i->second;
        if (getter.m_requires_shading_point && (!sg || !sg->renderstate))
            return false;
        return (this->*(getter.m_function))(sg, derivatives, object, type, name, val);
    }

    // Try user data from the current object.
    if (object.empty())
        return get_userdata(derivatives, name, type, sg, val);

    return false;
}

bool RendererServices::get_array_attribute(
    OSL::ShaderGlobals*         sg,
    bool                        derivatives,
    OIIO::ustringhash           object,
    OIIO::TypeDesc              type,
    OIIO::ustringhash           name,
    int                         index,
    void*                       val)
{
    return false;
}

bool RendererServices::get_userdata(
    bool                        derivatives,
    OIIO::ustringhash           name,
    OIIO::TypeDesc              type,
    OSL::ShaderGlobals*         sg,
    void*                       val)
{
    if (!sg || !sg->renderstate)
        return false;

    UserDataGetterMapType::const_iterator i = m_global_user_data_getters.find(name);
    if (i != m_global_user_data_getters.end())
    {
        UserDataGetterFun getter = i->second;
        return (this->*(getter))(derivatives, name, type, sg, val);
    }

    const auto* shading_point = static_cast<const ShadingPoint*>(sg->renderstate);
    const auto& attributes = shading_point->get_object().get_attributes();
    const auto value = attributes.find(OIIO::ustring::from_hash(name.hash()), type);
    if (value == attributes.end() || value->type() != type ||
        value->nvalues() != 1 || type.is_unsized_array())
        return false;

    if (type.basetype == OIIO::TypeDesc::STRING)
    {
        const auto* strings = static_cast<const OIIO::ustring*>(value->data());
        auto* output = static_cast<OIIO::ustringhash*>(val);
        for (size_t index = 0; index < type.numelements(); ++index)
            output[index] = OIIO::ustringhash(strings[index]);
    }
    else
        memcpy(val, value->data(), type.size());

    if (derivatives)
        clear_derivatives(type, val);

    return true;
}

#define IMPLEMENT_ATTR_GETTER(name)         \
    bool RendererServices::get_attr_##name( \
        OSL::ShaderGlobals*     sg,         \
        bool                    derivs,     \
        OIIO::ustringhash       object,     \
        OIIO::TypeDesc          type,       \
        OIIO::ustringhash       name,       \
        void*                   val) const

IMPLEMENT_ATTR_GETTER(object_instance_id)
{
    if (type == OIIO::TypeInt)
    {
        const ShadingPoint* shading_point =
            reinterpret_cast<const ShadingPoint*>(sg->renderstate);
        reinterpret_cast<int*>(val)[0] =
            static_cast<int>(shading_point->get_object_instance().get_uid());

        if (derivs)
            clear_derivatives(type, val);

        return true;
    }

    return false;
}

IMPLEMENT_ATTR_GETTER(object_instance_index)
{
    if (type == OIIO::TypeInt)
    {
        const ShadingPoint* shading_point =
            reinterpret_cast<const ShadingPoint*>(sg->renderstate);
        reinterpret_cast<int*>(val)[0] =
            static_cast<int>(shading_point->get_object_instance_index());

        if (derivs)
            clear_derivatives(type, val);

        return true;
    }

    return false;
}

IMPLEMENT_ATTR_GETTER(assembly_instance_id)
{
    if (type == OIIO::TypeInt)
    {
        const ShadingPoint* shading_point =
            reinterpret_cast<const ShadingPoint*>(sg->renderstate);
        reinterpret_cast<int*>(val)[0] =
            static_cast<int>(shading_point->get_assembly_instance().get_uid());

        if (derivs)
            clear_derivatives(type, val);

        return true;
    }

    return false;
}

IMPLEMENT_ATTR_GETTER(assembly_instance_name)
{
    const auto* shading_point = static_cast<const ShadingPoint*>(sg->renderstate);
    return get_name_attribute(shading_point->get_assembly_instance(), type, val);
}

IMPLEMENT_ATTR_GETTER(assembly_name)
{
    const auto* shading_point = static_cast<const ShadingPoint*>(sg->renderstate);
    return get_name_attribute(shading_point->get_assembly(), type, val);
}

IMPLEMENT_ATTR_GETTER(object_instance_name)
{
    const auto* shading_point = static_cast<const ShadingPoint*>(sg->renderstate);
    return get_name_attribute(shading_point->get_object_instance(), type, val);
}

IMPLEMENT_ATTR_GETTER(object_name)
{
    const auto* shading_point = static_cast<const ShadingPoint*>(sg->renderstate);
    return get_name_attribute(shading_point->get_object(), type, val);
}

IMPLEMENT_ATTR_GETTER(camera_resolution)
{
    if (type == g_int_array2_typedesc)
    {
        reinterpret_cast<int*>(val)[0] = m_resolution[0];
        reinterpret_cast<int*>(val)[1] = m_resolution[1];

        if (derivs)
            clear_derivatives(type, val);

        return true;
    }

    return false;
}

IMPLEMENT_ATTR_GETTER(camera_projection)
{
    if (type == OIIO::TypeString)
    {
        // OSL optimization uses strings; execution uses string hashes.
        if (sg)
            *static_cast<OIIO::ustringhash*>(val) = OIIO::ustringhash(m_cam_projection_str);
        else
            *static_cast<OIIO::ustring*>(val) = m_cam_projection_str;
        return true;
    }

    return false;
}

IMPLEMENT_ATTR_GETTER(camera_fov)
{
    return false;
}

IMPLEMENT_ATTR_GETTER(camera_pixelaspect)
{
    if (type == OIIO::TypeFloat)
    {
        reinterpret_cast<float*>(val)[0] = 1.0f;

        if (derivs)
            clear_derivatives(type, val);

        return true;
    }

    return false;
}

IMPLEMENT_ATTR_GETTER(camera_clip)
{
    if (type == g_float_array2_typedesc)
    {
        reinterpret_cast<float*>(val)[0] = 0.0f;
        reinterpret_cast<float*>(val)[1] = std::numeric_limits<float>::max();

        if (derivs)
            clear_derivatives(type, val);

        return true;
    }

    return false;
}

IMPLEMENT_ATTR_GETTER(camera_clip_near)
{
    if (type == OIIO::TypeFloat)
    {
        reinterpret_cast<float*>(val)[0] = 0.0f;

        if (derivs)
            clear_derivatives(type, val);

        return true;
    }

    return false;
}

IMPLEMENT_ATTR_GETTER(camera_clip_far)
{
    if (type == OIIO::TypeFloat)
    {
        reinterpret_cast<float*>(val)[0] = std::numeric_limits<float>::max();

        if (derivs)
            clear_derivatives(type, val);

        return true;
    }

    return false;
}

IMPLEMENT_ATTR_GETTER(camera_shutter)
{
    if (type == g_float_array2_typedesc)
    {
        reinterpret_cast<float*>(val)[0] = m_shutter[0];
        reinterpret_cast<float*>(val)[1] = m_shutter[1];

        if (derivs)
            clear_derivatives(type, val);

        return true;
    }

    return false;
}

IMPLEMENT_ATTR_GETTER(camera_shutter_open)
{
    if (type == OIIO::TypeFloat)
    {
        reinterpret_cast<float*>(val)[0] = m_shutter[0];

        if (derivs)
            clear_derivatives(type, val);

        return true;
    }

    return false;
}

IMPLEMENT_ATTR_GETTER(camera_shutter_close)
{
    if (type == OIIO::TypeFloat)
    {
        reinterpret_cast<float*>(val)[0] = m_shutter[1];

        if (derivs)
            clear_derivatives(type, val);

        return true;
    }

    return false;
}

IMPLEMENT_ATTR_GETTER(camera_screen_window)
{
    if (type == g_float_array4_typedesc)
    {
        const float aspect = static_cast<float>(m_resolution[0]) / m_resolution[1];
        reinterpret_cast<float*>(val)[0] = -aspect;
        reinterpret_cast<float*>(val)[1] = -1.0f;
        reinterpret_cast<float*>(val)[2] =  aspect;
        reinterpret_cast<float*>(val)[3] =  1.0f;

        if (derivs)
            clear_derivatives(type, val);

        return true;
    }

    return false;
}

IMPLEMENT_ATTR_GETTER(shading_normal)
{
    if (type != OIIO::TypeNormal)
        return false;

    const auto* shading_point = reinterpret_cast<const ShadingPoint*>(sg->renderstate);
    auto* result = reinterpret_cast<OSL::Vec3*>(val);
    result[0] = sg->N;
    if (derivs)
    {
        Vector3f dndx, dndy;
        shading_point->compute_screen_space_normal_derivatives(dndx, dndy);
        result[1] = dndx;
        result[2] = dndy;
    }
    return true;
}

IMPLEMENT_ATTR_GETTER(ray_depth)
{
    if (type == OIIO::TypeInt)
    {
        const ShadingPoint* shading_point =
            reinterpret_cast<const ShadingPoint*>(sg->renderstate);
        reinterpret_cast<int*>(val)[0] = static_cast<int>(shading_point->get_ray().m_depth);

        if (derivs)
            clear_derivatives(type, val);

        return true;
    }

    return false;
}

IMPLEMENT_ATTR_GETTER(ray_length)
{
    if (type == OIIO::TypeFloat)
    {
        const ShadingPoint* shading_point =
            reinterpret_cast<const ShadingPoint*>(sg->renderstate);
        reinterpret_cast<float*>(val)[0] = static_cast<float>(shading_point->get_distance());

        if (derivs)
            clear_derivatives(type, val);

        return true;
    }

    return false;
}

IMPLEMENT_ATTR_GETTER(ray_ior)
{
    if (type == OIIO::TypeFloat)
    {
        const ShadingPoint* shading_point =
            reinterpret_cast<const ShadingPoint*>(sg->renderstate);
        reinterpret_cast<float*>(val)[0] = shading_point->get_ray().get_current_ior();

        if (derivs)
            clear_derivatives(type, val);

        return true;
    }

    return false;
}

IMPLEMENT_ATTR_GETTER(outside_ior)
{
    if (type == OIIO::TypeFloat)
    {
        const auto* shading_point = static_cast<const ShadingPoint*>(sg->renderstate);
        reinterpret_cast<float*>(val)[0] = shading_point->get_outside_ior();
        if (derivs)
            clear_derivatives(type, val);
        return true;
    }
    return false;
}

IMPLEMENT_ATTR_GETTER(ray_has_differentials)
{
    if (type == OIIO::TypeInt)
    {
        const ShadingPoint* shading_point =
            reinterpret_cast<const ShadingPoint*>(sg->renderstate);
        reinterpret_cast<int*>(val)[0] = static_cast<int>(shading_point->get_ray().m_has_differentials);

        if (derivs)
            clear_derivatives(type, val);

        return true;
    }

    return false;
}

IMPLEMENT_ATTR_GETTER(surface_shader_diffuse)
{
    if (type == OIIO::TypeColor)
    {
        const ShadingPoint* shading_point = reinterpret_cast<const ShadingPoint*>(sg->renderstate);
        reinterpret_cast<float*>(val)[0] = shading_point->m_surface_shader_diffuse[0];
        reinterpret_cast<float*>(val)[1] = shading_point->m_surface_shader_diffuse[1];
        reinterpret_cast<float*>(val)[2] = shading_point->m_surface_shader_diffuse[2];

        if (derivs)
            clear_derivatives(type, val);

        return true;
    }

    return false;
}

IMPLEMENT_ATTR_GETTER(surface_shader_glossy)
{
    if (type == OIIO::TypeColor)
    {
        const ShadingPoint* shading_point = reinterpret_cast<const ShadingPoint*>(sg->renderstate);
        reinterpret_cast<float*>(val)[0] = shading_point->m_surface_shader_glossy[0];
        reinterpret_cast<float*>(val)[1] = shading_point->m_surface_shader_glossy[1];
        reinterpret_cast<float*>(val)[2] = shading_point->m_surface_shader_glossy[2];

        if (derivs)
            clear_derivatives(type, val);

        return true;
    }

    return false;
}

IMPLEMENT_ATTR_GETTER(surface_shader_emission)
{
    if (type == OIIO::TypeColor)
    {
        const ShadingPoint* shading_point = reinterpret_cast<const ShadingPoint*>(sg->renderstate);
        reinterpret_cast<float*>(val)[0] = shading_point->m_surface_shader_emission[0];
        reinterpret_cast<float*>(val)[1] = shading_point->m_surface_shader_emission[1];
        reinterpret_cast<float*>(val)[2] = shading_point->m_surface_shader_emission[2];

        if (derivs)
            clear_derivatives(type, val);

        return true;
    }

    return false;
}

IMPLEMENT_ATTR_GETTER(appleseed_version_major)
{
    if (type == OIIO::TypeInt)
    {
        reinterpret_cast<int*>(val)[0] = APPLESEED_VERSION_MAJOR;

        if (derivs)
            clear_derivatives(type, val);

        return true;
    }

    return false;
}

IMPLEMENT_ATTR_GETTER(appleseed_version_minor)
{
    if (type == OIIO::TypeInt)
    {
        reinterpret_cast<int*>(val)[0] = APPLESEED_VERSION_MINOR;

        if (derivs)
            clear_derivatives(type, val);

        return true;
    }

    return false;
}

IMPLEMENT_ATTR_GETTER(appleseed_version_patch)
{
    if (type == OIIO::TypeInt)
    {
        reinterpret_cast<int*>(val)[0] = APPLESEED_VERSION_PATCH;

        if (derivs)
            clear_derivatives(type, val);

        return true;
    }

    return false;
}

IMPLEMENT_ATTR_GETTER(appleseed_version)
{
    if (type == OIIO::TypeInt)
    {
        reinterpret_cast<int*>(val)[0] = APPLESEED_VERSION;

        if (derivs)
            clear_derivatives(type, val);

        return true;
    }

    return false;
}

#undef IMPLEMENT_ATTR_GETTER

#define IMPLEMENT_USER_DATA_GETTER(name)         \
    bool RendererServices::get_user_data_##name( \
        bool                    derivatives,     \
        OIIO::ustringhash       name,            \
        OIIO::TypeDesc          type,            \
        OSL::ShaderGlobals*     sg,              \
        void*                   val) const

IMPLEMENT_USER_DATA_GETTER(tn)
{
    if (type == OIIO::TypeVector)
    {
        const ShadingPoint* shading_point =
            reinterpret_cast<const ShadingPoint*>(sg->renderstate);

        const Vector3d& tn = shading_point->get_shading_basis().get_tangent_u();
        reinterpret_cast<float*>(val)[0] = static_cast<float>(tn.x);
        reinterpret_cast<float*>(val)[1] = static_cast<float>(tn.y);
        reinterpret_cast<float*>(val)[2] = static_cast<float>(tn.z);

        if (derivatives)
        {
            Vector3f ux, uy, vx, vy;
            shading_point->compute_screen_space_basis_derivatives(ux, uy, vx, vy);
            auto* result = reinterpret_cast<OSL::Vec3*>(val);
            result[1] = ux;
            result[2] = uy;
        }

        return true;
    }

    return false;
}

IMPLEMENT_USER_DATA_GETTER(bn)
{
    if (type == OIIO::TypeVector)
    {
        const ShadingPoint* shading_point =
            reinterpret_cast<const ShadingPoint*>(sg->renderstate);

        const Vector3d& bn = shading_point->get_shading_basis().get_tangent_v();
        OSL::Vec3 v(
            static_cast<float>(bn.x),
            static_cast<float>(bn.y),
            static_cast<float>(bn.z));

        // Make sure Bn points in the same direction as dPdv.
        const bool reverse = v.dot(sg->dPdv) < 0.0f;
        if (reverse)
            v = -v;

        reinterpret_cast<float*>(val)[0] = v.x;
        reinterpret_cast<float*>(val)[1] = v.y;
        reinterpret_cast<float*>(val)[2] = v.z;

        if (derivatives)
        {
            Vector3f ux, uy, vx, vy;
            shading_point->compute_screen_space_basis_derivatives(ux, uy, vx, vy);
            auto* result = reinterpret_cast<OSL::Vec3*>(val);
            result[1] = reverse ? -vx : vx;
            result[2] = reverse ? -vy : vy;
        }

        return true;
    }

    return false;
}

IMPLEMENT_USER_DATA_GETTER(dndu)
{
    if (type == OIIO::TypeVector)
    {
        const ShadingPoint* shading_point =
            reinterpret_cast<const ShadingPoint*>(sg->renderstate);

        const Vector3d& dndu = shading_point->get_dndu(0);
        reinterpret_cast<float*>(val)[0] = static_cast<float>(dndu.x);
        reinterpret_cast<float*>(val)[1] = static_cast<float>(dndu.y);
        reinterpret_cast<float*>(val)[2] = static_cast<float>(dndu.z);

        if (derivatives)
            clear_derivatives(type, val);

        return true;
    }

    return false;
}

IMPLEMENT_USER_DATA_GETTER(dndv)
{
    if (type == OIIO::TypeVector)
    {
        const ShadingPoint* shading_point =
            reinterpret_cast<const ShadingPoint*>(sg->renderstate);

        const Vector3d& dndv = shading_point->get_dndv(0);
        reinterpret_cast<float*>(val)[0] = static_cast<float>(dndv.x);
        reinterpret_cast<float*>(val)[1] = static_cast<float>(dndv.y);
        reinterpret_cast<float*>(val)[2] = static_cast<float>(dndv.z);

        if (derivatives)
            clear_derivatives(type, val);

        return true;
    }

    return false;
}

IMPLEMENT_USER_DATA_GETTER(vertex_color)
{
    if (type == OIIO::TypeColor)
    {
        const ShadingPoint* shading_point =
            reinterpret_cast<const ShadingPoint*>(sg->renderstate);

        const Color3f& cv = shading_point->get_per_vertex_color();
        reinterpret_cast<float*>(val)[0] = cv.r;
        reinterpret_cast<float*>(val)[1] = cv.g;
        reinterpret_cast<float*>(val)[2] = cv.b;

        if (derivatives)
            clear_derivatives(type, val);

        return true;
    }

    return false;
}

#undef IMPLEMENT_USER_DATA_GETTER

void RendererServices::clear_derivatives(
    const OIIO::TypeDesc&       type,
    void*                       val)
{
    if (type.basetype != OIIO::TypeDesc::STRING)
        memset(reinterpret_cast<char*>(val) + type.size(), 0, 2 * type.size());
}

}   // namespace renderer
