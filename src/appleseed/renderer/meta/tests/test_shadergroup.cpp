
//
// This source file is part of appleseed.
// Visit https://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
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
#include "renderer/kernel/lighting/lightsamplerbase.h"
#include "renderer/kernel/rendering/rendererservices.h"
#include "renderer/kernel/shading/closures.h"
#include "renderer/kernel/shading/oslshadingsystem.h"
#include "renderer/kernel/texturing/oiiotexturesystem.h"
#include "renderer/modeling/material/material.h"
#include "renderer/modeling/project/project.h"
#include "renderer/modeling/scene/assemblyinstance.h"
#include "renderer/modeling/scene/objectinstance.h"
#include "renderer/modeling/shadergroup/shadercompiler.h"
#include "renderer/modeling/shadergroup/shadergroup.h"
#include "renderer/utility/paramarray.h"

// appleseed.foundation headers.
#include "foundation/math/transform.h"
#include "foundation/memory/autoreleaseptr.h"
#include "foundation/utility/test.h"

// Standard headers.
#include <cstddef>
#include <memory>
#include <string>

using namespace foundation;
using namespace renderer;

TEST_SUITE(Renderer_Modeling_ShaderGroup)
{
    class EmittingMaterial
      : public Material
    {
      public:
        explicit EmittingMaterial(const ShaderGroup& group)
          : Material("emitter", ParamArray())
          , m_group(group)
        {
        }

        void release() override
        {
            delete this;
        }

        const char* get_model() const override
        {
            return "test_emitting_material";
        }

        const ShaderGroup* get_uncached_osl_surface() const override
        {
            return &m_group;
        }

      private:
        const ShaderGroup& m_group;
    };

    class AreaRegistrar
      : public LightSamplerBase
    {
      public:
        AreaRegistrar()
          : LightSamplerBase(ParamArray())
        {
        }

        using LightSamplerBase::store_object_area_in_shadergroups;
    };

    struct Fixture
    {
        static constexpr size_t AssemblyCount = 2;
        static constexpr size_t ObjectCount = 64;

        auto_release_ptr<Project> m_project;
        std::shared_ptr<OIIOTextureSystem> m_textures;
        RendererServices m_services;
        auto_release_ptr<OSLShadingSystem> m_system;
        auto_release_ptr<ShaderGroup> m_group;
        auto_release_ptr<AssemblyInstance> m_assemblies[AssemblyCount];
        auto_release_ptr<ObjectInstance> m_objects[ObjectCount];
        AreaRegistrar m_registrar;
        EmittingMaterial m_material;
        MaterialArray m_materials;

        Fixture()
          : m_project(ProjectFactory::create("area_test"))
          , m_textures(OIIOTextureSystemFactory::create())
          , m_services(*m_project, *m_textures)
          , m_system(OSLShadingSystemFactory::create(&m_services, m_textures.get()))
          , m_group(ShaderGroupFactory::create("area_test"))
          , m_material(*m_group)
        {
            register_closures(*m_system);
            m_materials.push_back(&m_material);

            // Only instance identity is relevant to area registration.
            for (size_t i = 0; i < AssemblyCount; ++i)
            {
                m_assemblies[i] = AssemblyInstanceFactory::create(
                    ("assembly_" + std::to_string(i)).c_str(), ParamArray(), "assembly");
            }
            for (size_t i = 0; i < ObjectCount; ++i)
            {
                m_objects[i] = ObjectInstanceFactory::create(
                    ("object_" + std::to_string(i)).c_str(), ParamArray(), "object",
                    Transformd::identity(), StringDictionary());
            }
        }

        bool initialize()
        {
            // Exercise staged headers rather than a producer-prefix fallback.
            auto compiler = ShaderCompilerFactory::create("../shaders/stdosl.h");
            m_group->add_source_shader(
                "surface", "area_test_emitter", "emitter",
                "surface area_test_emitter() { Ci = emission(); }", ParamArray());
            return m_group->create_optimized_osl_shader_group(*m_system, compiler.get())
                && m_group->has_emission();
        }

        void store(const size_t assembly, const size_t object, const float area)
        {
            m_registrar.store_object_area_in_shadergroups(
                m_assemblies[assembly].get(), m_objects[object].get(), area, m_materials);
        }

        float lookup(const size_t assembly, const size_t object) const
        {
            return m_group->get_surface_area(m_assemblies[assembly].get(), m_objects[object].get());
        }
    };

    TEST_CASE_F(SurfaceAreasDistinguishBothInstanceIdentities, Fixture)
    {
        ASSERT_TRUE(initialize());
        EXPECT_EQ(0.0f, lookup(0, 0));
        store(0, 0, 1.0f);
        store(0, 1, 2.0f);
        store(1, 0, 3.0f);

        EXPECT_EQ(1.0f, lookup(0, 0));
        EXPECT_EQ(2.0f, lookup(0, 1));
        EXPECT_EQ(3.0f, lookup(1, 0));
        EXPECT_EQ(0.0f, lookup(1, 1));

        store(0, 0, 4.0f);
        EXPECT_EQ(4.0f, lookup(0, 0));
        EXPECT_EQ(2.0f, lookup(0, 1));
        EXPECT_EQ(3.0f, lookup(1, 0));
        EXPECT_EQ(0.0f, lookup(1, 1));
    }

    TEST_CASE_F(SurfaceAreasSurviveIndexGrowth, Fixture)
    {
        ASSERT_TRUE(initialize());
        for (size_t assembly = 0; assembly < AssemblyCount; ++assembly)
        {
            for (size_t object = 0; object < ObjectCount; ++object)
                store(assembly, object, static_cast<float>(assembly * ObjectCount + object + 1));
        }

        for (size_t assembly = 0; assembly < AssemblyCount; ++assembly)
        {
            for (size_t object = 0; object < ObjectCount; ++object)
            {
                EXPECT_EQ(static_cast<float>(assembly * ObjectCount + object + 1),
                    lookup(assembly, object));
            }
        }
    }
}
