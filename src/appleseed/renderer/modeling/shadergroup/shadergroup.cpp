
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
#include "shadergroup.h"

// appleseed.renderer headers.
#include "renderer/global/globallogger.h"
#include "renderer/kernel/shading/oslshadingsystem.h"
#include "renderer/modeling/project/project.h"
#include "renderer/modeling/shadergroup/shader.h"
#include "renderer/modeling/shadergroup/shaderconnection.h"
#include "renderer/utility/paramarray.h"

// appleseed.foundation headers.
#include "foundation/hash/hash.h"
#include "foundation/utility/api/apistring.h"
#include "foundation/utility/api/specializedapiarrays.h"
#include "foundation/utility/job/abortswitch.h"
#include "foundation/utility/uid.h"

// Standard headers.
#include <exception>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

using namespace foundation;

namespace renderer
{

//
// ShaderGroup class implementation.
//

namespace
{
    const UniqueID g_class_uid = new_guid();

    const OIIO::ustring g_emission_str("emission");
    const OIIO::ustring g_transparent_str("transparent");
    const OIIO::ustring g_subsurface_str("as_subsurface");
    const OIIO::ustring g_randomwalk_glass_str("as_randomwalk_glass");
    const OIIO::ustring g_holdout_str("holdout");
    const OIIO::ustring g_matte_str("as_matte");
    const OIIO::ustring g_debug_str("debug");

    const OIIO::ustring g_npr_shading_str("as_npr_shading");
    const OIIO::ustring g_npr_contour_str("as_npr_contour");

    const OIIO::ustring g_dPdtime_str("dPdtime");

    bool is_subsurface_closure(const OIIO::ustring& closure_name)
    {
        return
            closure_name == g_subsurface_str ||
            closure_name == g_randomwalk_glass_str;
    }
}

struct ShaderGroup::Impl
{
    typedef std::pair<const AssemblyInstance*, const ObjectInstance*> SurfaceAreaKey;

    struct SurfaceAreaKeyHasher
    {
        size_t operator()(const SurfaceAreaKey& key) const noexcept
        {
            return combine_hashes(
                std::hash<const AssemblyInstance*>()(key.first),
                std::hash<const ObjectInstance*>()(key.second));
        }
    };

    typedef std::unordered_map<SurfaceAreaKey, float, SurfaceAreaKeyHasher> SurfaceAreaMap;

    ShaderContainer             m_shaders;
    ShaderConnectionContainer   m_connections;
    mutable OSL::ShaderGroupRef m_shader_group_ref;
    SurfaceAreaMap              m_surface_areas;
    std::unordered_map<std::uint64_t, OSL::ShaderGroupRef> m_bump_groups;
};

ShaderGroup::ShaderGroup(const char* name)
  : ConnectableEntity(g_class_uid, ParamArray())
  , impl(new Impl())
{
    set_name(name);
    clear();
}

ShaderGroup::~ShaderGroup()
{
    delete impl;
}

void ShaderGroup::release()
{
    delete this;
}

const char* ShaderGroup::get_model() const
{
    return ShaderGroupFactory::get_model();
}

void ShaderGroup::clear()
{
    impl->m_shaders.clear();
    impl->m_connections.clear();
    impl->m_shader_group_ref.reset();
    impl->m_bump_groups.clear();
    m_flags = 0;
}

void ShaderGroup::add_shader(
    const char*             type,
    const char*             name,
    const char*             layer,
    const ParamArray&       params)
{
    auto_release_ptr<Shader> shader(
        new Shader(
            type,
            name,
            layer,
            params));

    impl->m_shaders.insert(shader);

    RENDERER_LOG_DEBUG("created shader %s, layer = %s.", name, layer);
}

void ShaderGroup::add_source_shader(
    const char*             type,
    const char*             name,
    const char*             layer,
    const char*             source,
    const ParamArray&       params)
{
    auto_release_ptr<Shader> shader(
        new Shader(
            type,
            name,
            layer,
            source,
            params));

    impl->m_shaders.insert(shader);

    RENDERER_LOG_DEBUG("created source shader %s, layer = %s.", name, layer);
}

void ShaderGroup::add_connection(
    const char*             src_layer,
    const char*             src_param,
    const char*             dst_layer,
    const char*             dst_param)
{
    auto_release_ptr<ShaderConnection> connection(
        new ShaderConnection(
            src_layer,
            src_param,
            dst_layer,
            dst_param));

    impl->m_connections.insert(connection);

    RENDERER_LOG_DEBUG(
        "created shader connection: src_layer = %s, src_param = %s, dst_layer = %s, dst_param = %s.",
            src_layer,
            src_param,
            dst_layer,
            dst_param);
}

bool ShaderGroup::create_optimized_osl_shader_group(
    OSLShadingSystem&       shading_system,
    const ShaderCompiler*   shader_compiler,
    IAbortSwitch*           abort_switch)
{
    if (is_valid())
        return true;

    RENDERER_LOG_DEBUG("setting up shader group \"%s\"...", get_path().c_str());

    if (!compile_source_shaders(shader_compiler))
        return false;

    try
    {
        // Prefix groups keep numerical probes out of the active closure pool.
        // Use the same parameter and connection setup as the material group.
        const auto build_group = [&](const char* name, const Shader* last) -> OSL::ShaderGroupRef
        {
            OSL::ShaderGroupRef group = shading_system.ShaderGroupBegin(name);
            if (!group)
            {
                RENDERER_LOG_ERROR("failed to begin OSL group \"%s\".", name);
                return {};
            }

            struct GroupScope
            {
                OSLShadingSystem& system;
                bool active = true;
                ~GroupScope()
                {
                    if (active)
                        system.ShaderGroupEnd();
                }
            } scope{shading_system};

            std::unordered_set<std::string> layers;
            for (Shader& shader : impl->m_shaders)
            {
                if (is_aborted(abort_switch) || !shader.add(shading_system))
                    return {};
                layers.insert(shader.get_layer());
                if (&shader == last)
                    break;
            }
            for (ShaderConnection& connection : impl->m_connections)
            {
                if (is_aborted(abort_switch))
                    return {};
                if ((!last || (layers.count(connection.get_src_layer()) &&
                    layers.count(connection.get_dst_layer()))) &&
                    !connection.add(shading_system))
                    return {};
            }
            const bool ended = shading_system.ShaderGroupEnd();
            scope.active = false;
            if (!ended)
                RENDERER_LOG_ERROR("failed to finish OSL group \"%s\".", name);
            return ended ? group : OSL::ShaderGroupRef();
        };

        OSL::ShaderGroupRef group = build_group(get_name(), nullptr);
        if (!group)
            return is_aborted(abort_switch);

        std::unordered_map<std::uint64_t, OSL::ShaderGroupRef> bump_groups;
        for (Shader& shader : impl->m_shaders)
        {
            if (!shader.is_bump_shader())
                continue;
            const std::string name = std::string(get_name()) + "." + shader.get_layer() + ".bump";
            OSL::ShaderGroupRef prefix = build_group(name.c_str(), &shader);
            if (!prefix)
                return is_aborted(abort_switch);
            const std::string output = std::string(shader.get_layer()) + ".out_normal";
            const char* outputs[] = {output.c_str()};
            if (!shading_system.attribute(prefix.get(), "renderer_outputs",
                OSL::TypeDesc(OSL::TypeDesc::STRING, 1), outputs))
                return false;
            bump_groups.emplace(OIIO::ustringhash(shader.get_layer()).hash(), std::move(prefix));
        }
        impl->m_shader_group_ref = std::move(group);
        impl->m_bump_groups = std::move(bump_groups);

        get_shadergroup_closures_info(shading_system);
        report_has_closure("bsdf", HasBSDFs);
        report_has_closure(g_emission_str.c_str(), HasEmission);
        report_has_closure(g_transparent_str.c_str(), HasTransparency);
        report_has_closure(g_subsurface_str.c_str(), HasSubsurface);
        report_has_closure(g_debug_str.c_str(), HasDebug);

        report_has_closure("NPR", HasNPR);
        report_has_closure(g_matte_str.c_str(), HasMatte);

        get_shadergroup_globals_info(shading_system);
        report_uses_global("dPdtime", UsesdPdTime);

        return true;
    }
    catch (const std::exception& e)
    {
        RENDERER_LOG_ERROR("failed to setup shader group \"%s\": %s.", get_path().c_str(), e.what());
        return false;
    }
}

void ShaderGroup::release_optimized_osl_shader_group()
{
    impl->m_bump_groups.clear();
    impl->m_shader_group_ref.reset();
}

const ShaderContainer& ShaderGroup::shaders() const
{
    return impl->m_shaders;
}

const ShaderConnectionContainer& ShaderGroup::shader_connections() const
{
    return impl->m_connections;
}

bool ShaderGroup::is_valid() const
{
    return impl->m_shader_group_ref.get() != nullptr;
}

float ShaderGroup::get_surface_area(
    const AssemblyInstance* assembly_instance,
    const ObjectInstance*   object_instance) const
{
    assert(has_emission());
    // Area entries are populated before rendering. Unknown procedural shapes
    // have no registered area; shader execution must not mutate this shared map.
    const auto& areas = impl->m_surface_areas;
    const auto entry = areas.find(Impl::SurfaceAreaKey(assembly_instance, object_instance));
    return entry == areas.end() ? 0.0f : entry->second;
}

void* ShaderGroup::osl_shader_group() const
{
    return impl->m_shader_group_ref.get();
}

void* ShaderGroup::osl_bump_shader_group(const std::uint64_t layer_hash) const
{
    const auto entry = impl->m_bump_groups.find(layer_hash);
    return entry == impl->m_bump_groups.end() ? nullptr : entry->second.get();
}

bool ShaderGroup::compile_source_shaders(const ShaderCompiler* compiler)
{
    for (Shader& shader : impl->m_shaders)
    {
        const bool success = shader.compile_shader(compiler);

        if (!success)
            return false;
    }

    return true;
}

void ShaderGroup::get_shadergroup_closures_info(OSLShadingSystem& shading_system)
{
    // Assume the shader group has all closure types.
    m_flags |= HasAllClosures;

    int num_unknown_closures = 0;
    if (!shading_system.getattribute(
            impl->m_shader_group_ref.get(),
            "unknown_closures_needed",
            num_unknown_closures))
    {
        RENDERER_LOG_WARNING(
            "getattribute: unknown_closures_needed call failed for shader group \"%s\"; "
            "assuming shader group has all kinds of closures.",
            get_path().c_str());
        return;
    }

    if (num_unknown_closures != 0)
    {
        RENDERER_LOG_WARNING(
            "shader group \"%s\" has unknown closures; "
            "assuming shader group has all kinds of closures.",
            get_path().c_str());
        return;
    }

    int num_closures = 0;
    if (!shading_system.getattribute(
            impl->m_shader_group_ref.get(),
            "num_closures_needed",
            num_closures))
    {
        RENDERER_LOG_WARNING(
            "getattribute: num_closures_needed call failed for shader group \"%s\"; "
            "assuming shader group has all kinds of closures.",
            get_path().c_str());
    }

    if (num_closures != 0)
    {
        OIIO::ustring* closures = nullptr;
        if (!shading_system.getattribute(
                impl->m_shader_group_ref.get(),
                "closures_needed",
                OIIO::TypeDesc::PTR,
                &closures))
        {
            RENDERER_LOG_WARNING(
                "getattribute: closures_needed call failed for shader group \"%s\"; "
                "assuming shader group has all kinds of closures.",
                get_path().c_str());
            return;
        }

        // Clear all closure flags.
        m_flags &= ~HasAllClosures;

        // Set the closure flags.
        for (int i = 0; i < num_closures; ++i)
        {
            if (closures[i] == g_emission_str)
                m_flags |= HasEmission;
            else if (closures[i] == g_transparent_str)
                m_flags |= HasTransparency;
            else if (is_subsurface_closure(closures[i]))
                m_flags |= HasSubsurface;
            else if (closures[i] == g_debug_str)
                m_flags |= HasDebug;
            else if (closures[i] == g_npr_shading_str)
                m_flags |= HasNPR;
            else if (closures[i] == g_npr_contour_str)
                m_flags |= HasNPR;
            else if (
                closures[i] == g_matte_str ||
                closures[i] == g_holdout_str)
                m_flags |= HasMatte;
            else
                m_flags |= HasBSDFs;
        }
    }
    else
    {
        // Shader group uses no closures.
        m_flags &= ~HasAllClosures;
    }
}

void ShaderGroup::report_has_closure(const char* closure_name, const Flags flag) const
{
    if (m_flags & flag)
    {
        RENDERER_LOG_DEBUG(
            "shader group \"%s\" has %s closures.",
            get_path().c_str(),
            closure_name);
    }
    else
    {
        RENDERER_LOG_DEBUG(
            "shader group \"%s\" does not have %s closures.",
            get_path().c_str(),
            closure_name);
    }
}

void ShaderGroup::get_shadergroup_globals_info(OSLShadingSystem& shading_system)
{
    // Assume the shader group uses all globals.
    m_flags |= UsesAllGlobals;

    int num_globals = 0;
    if (!shading_system.getattribute(
            impl->m_shader_group_ref.get(),
            "num_globals_needed",
            num_globals))
    {
        RENDERER_LOG_WARNING(
            "getattribute: num_globals_needed call failed for shader group \"%s\"; "
            "assuming shader group uses all globals.",
            get_path().c_str());
        return;
    }

    if (num_globals != 0)
    {
        OIIO::ustring* globals = nullptr;
        if (!shading_system.getattribute(
                impl->m_shader_group_ref.get(),
                "globals_needed",
                OIIO::TypeDesc::PTR,
                &globals))
        {
            RENDERER_LOG_WARNING(
                "getattribute: globals_needed call failed for shader group \"%s\"; "
                "assuming shader group uses all globals.",
                get_path().c_str());
            return;
        }

        // Clear all globals flags.
        m_flags &= ~UsesAllGlobals;

        // Set the globals flags.
        for (int i = 0; i < num_globals; ++i)
        {
            if (globals[i] == g_dPdtime_str)
                m_flags |= UsesdPdTime;
        }
    }
    else
    {
        // The shader group uses no globals.
        m_flags &= ~UsesAllGlobals;
    }
}

void ShaderGroup::report_uses_global(const char* global_name, const Flags flag) const
{
    if (m_flags & flag)
    {
        RENDERER_LOG_DEBUG(
            "shader group \"%s\" uses the %s global.",
            get_path().c_str(),
            global_name);
    }
    else
    {
        RENDERER_LOG_DEBUG(
            "shader group \"%s\" does not use the %s global.",
            get_path().c_str(),
            global_name);
    }
}

void ShaderGroup::set_surface_area(
    const AssemblyInstance* assembly_instance,
    const ObjectInstance*   object_instance,
    const float             area) const
{
    assert(has_emission());
    impl->m_surface_areas[Impl::SurfaceAreaKey(assembly_instance, object_instance)] = area;
}


//
// ShaderGroupFactory class implementation.
//

const char* ShaderGroupFactory::get_model()
{
    return "shader_group";
}

DictionaryArray ShaderGroupFactory::get_input_metadata()
{
    DictionaryArray metadata;
    return metadata;
}

auto_release_ptr<ShaderGroup> ShaderGroupFactory::create(const char* name)
{
    return auto_release_ptr<ShaderGroup>(new ShaderGroup(name));
}

auto_release_ptr<ShaderGroup> ShaderGroupFactory::create(
    const char*         name,
    const ParamArray&   params)
{
    return create(name);
}

}   // namespace renderer
