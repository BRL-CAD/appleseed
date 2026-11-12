
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
#include "renderer/kernel/rendering/rendererservices.h"
#include "renderer/kernel/texturing/oiiotexturesystem.h"
#include "renderer/modeling/project/project.h"

// appleseed.foundation headers.
#include "foundation/utility/searchpaths.h"
#include "foundation/utility/test.h"

// Standard headers.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

using namespace foundation;
using namespace renderer;
namespace fs = std::filesystem;

TEST_SUITE(Renderer_Kernel_Rendering_TexturePaths)
{
    class TestDirectory
    {
        const fs::path m_path;
      public:
        explicit TestDirectory(const fs::path& path) : m_path(path)
        {
            if (!fs::create_directory(m_path))
                throw std::runtime_error("cannot create texture fixture directory");
        }
        ~TestDirectory()
        {
            std::error_code error;
            fs::remove_all(m_path, error);
            if (error)
            {
                std::fprintf(stderr, "texture fixture cleanup failed: %s\n", error.message().c_str());
                std::abort();
            }
        }
        const fs::path& path() const { return m_path; }
        TestDirectory(const TestDirectory&) = delete;
        TestDirectory& operator=(const TestDirectory&) = delete;
    };

    struct Fixture
    {
        static constexpr int ProjectWidth = 7;
        static constexpr int ShadowWidth = 3;
        const std::string directory;
        const std::string unicode_image = u8"texture \u03bb.pgm";
        TestDirectory root;
        TestDirectory shadow;
        auto_release_ptr<Project> project;
        std::shared_ptr<OIIOTextureSystem> textures;

        Fixture()
          : directory("texture-path-test-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()))
          , root(fs::temp_directory_path() / (directory + "-project"))
          , shadow(fs::current_path() / directory)
          , project(ProjectFactory::create("texture paths"))
          , textures(OIIOTextureSystemFactory::create(false))
        {
            fs::create_directory(root.path() / directory);
            project->search_paths().set_root_path(root.path().u8string().c_str());
            write_image(root.path() / directory / "shared image.pgm", ProjectWidth);
            write_image(shadow.path() / "shared image.pgm", ShadowWidth);
            write_image(shadow.path() / "cwd only.pgm", ShadowWidth);
            write_image(root.path() / directory / "tile.1001.pgm", ProjectWidth);
            write_image(shadow.path() / "tile.1001.pgm", ShadowWidth);
            write_image(root.path() / directory / fs::u8path(unicode_image), ProjectWidth);
            write_image(shadow.path() / fs::u8path(unicode_image), ShadowWidth);
        }

        static void write_image(const fs::path& path, const int width)
        {
            std::ofstream output(path, std::ios::binary);
            output << "P5\n" << width << " 1\n255\n" << std::string(width, '\0');
            output.close();
            if (!output)
                throw std::runtime_error("cannot write texture fixture");
        }

        bool resolution(RendererServices& services, const std::string& name, int& width,
            RendererServices::TextureHandle* handle = nullptr)
        {
            int size[2] = {};
            OIIO::ustringhash error;
            const bool result = services.get_texture_info(OIIO::ustringhash(name), handle,
                nullptr, nullptr, 0, OIIO::ustringhash("resolution"),
                OIIO::TypeDesc(OIIO::TypeDesc::INT, 2), size, &error);
            width = size[0];
            return result;
        }
    };

    TEST_CASE_F(ProjectRootWinsOverWorkingDirectory, Fixture)
    {
        RendererServices services(*project, *textures, true);
        int width = 0;
        EXPECT_TRUE(resolution(services, directory + "/shared image.pgm", width));
        EXPECT_EQ(ProjectWidth, width);
    }

    TEST_CASE_F(MissingProjectFileDoesNotUseWorkingDirectory, Fixture)
    {
        RendererServices services(*project, *textures, true);
        int width = 0;
        EXPECT_FALSE(resolution(services, directory + "/cwd only.pgm", width));
    }

    TEST_CASE_F(ExplicitAbsolutePathIsPreserved, Fixture)
    {
        RendererServices services(*project, *textures, true);
        int width = 0;
        EXPECT_TRUE(resolution(services, (shadow.path() / "shared image.pgm").u8string(), width));
        EXPECT_EQ(ShadowWidth, width);
    }

    TEST_CASE_F(PreboundHandleIsPreserved, Fixture)
    {
        RendererServices services(*project, *textures, true);
        const auto name = (root.path() / directory / "shared image.pgm").u8string();
        auto* handle = textures->get_texture_handle(OIIO::ustring(name));
        int width = 0;
        EXPECT_TRUE(resolution(services, (shadow.path() / "shared image.pgm").u8string(), width, handle));
        EXPECT_EQ(ProjectWidth, width);
    }

    TEST_CASE_F(ProjectRootAppliesToUdimPatterns, Fixture)
    {
        RendererServices services(*project, *textures, true);
        int width = 0;
        EXPECT_TRUE(resolution(services, directory + "/tile.<UDIM>.pgm", width));
        EXPECT_EQ(ProjectWidth, width);
    }

    TEST_CASE_F(UnicodeFilenameUsesProjectRoot, Fixture)
    {
        RendererServices services(*project, *textures, true);
        int width = 0;
        EXPECT_TRUE(resolution(services, directory + "/" + unicode_image, width));
        EXPECT_EQ(ProjectWidth, width);
    }

    TEST_CASE_F(RelativeLookupRequiresProjectRoot, Fixture)
    {
        project->search_paths().set_root_path("");
        RendererServices services(*project, *textures, true);
        int width = 0;
        EXPECT_EXCEPTION(std::runtime_error,
        {
            resolution(services, directory + "/shared image.pgm", width);
        });
    }

#ifdef _WIN32
    TEST_CASE_F(DriveRelativeFilenameIsRejected, Fixture)
    {
        RendererServices services(*project, *textures, true);
        int width = 0;
        EXPECT_EXCEPTION(std::runtime_error,
        {
            resolution(services, "C:ambiguous.pgm", width);
        });
    }
#endif

    TEST_CASE_F(EmptyFilenameReportsMissingTexture, Fixture)
    {
        RendererServices services(*project, *textures, true);
        int width = 0;
        EXPECT_FALSE(resolution(services, "", width));
    }

    TEST_CASE_F(DefaultLookupPolicyRemainsAvailable, Fixture)
    {
        RendererServices services(*project, *textures);
        int width = 0;
        EXPECT_TRUE(resolution(services, directory + "/shared image.pgm", width));
        EXPECT_EQ(ShadowWidth, width);
    }
}
