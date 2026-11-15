
//
// This source file is part of appleseed.
// Visit https://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2018 Francois Beaune, The appleseedhq Organization
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
#include "renderer/kernel/rendering/permanentshadingresultframebufferfactory.h"
#include "renderer/modeling/aov/aov.h"
#include "renderer/modeling/aov/aovcontainer.h"
#include "renderer/modeling/aov/diffuseaov.h"
#include "renderer/modeling/aov/glossyaov.h"
#include "renderer/modeling/aov/normalaov.h"
#include "renderer/modeling/frame/frame.h"
#include "renderer/utility/paramarray.h"

// appleseed.foundation headers.
#include "foundation/image/color.h"
#include "foundation/image/genericprogressiveimagefilereader.h"
#include "foundation/image/image.h"
#include "foundation/image/imageattributes.h"
#include "foundation/image/tile.h"
#include "foundation/memory/autoreleaseptr.h"
#include "foundation/platform/thread.h"
#include "foundation/string/string.h"
#include "foundation/utility/iostreamop.h"
#include "foundation/utility/test.h"

// OpenImageIO headers.
#include "OpenImageIO/imageio.h"

// Standard headers.
#include <filesystem>
#include <memory>

namespace bf = std::filesystem;
using namespace foundation;
using namespace renderer;

TEST_SUITE(Renderer_Modeling_Frame_Frame)
{
    struct Fixture
    {
        const bf::path              m_output_directory;
        auto_release_ptr<Frame>     m_frame;

        Fixture()
          : m_output_directory(bf::absolute("unit tests/outputs/test_frame/"))
        {
            remove_all(m_output_directory);

            // On Windows, the create_directory() call below will fail with an Access Denied error
            // if a File Explorer window was opened in the output directory that we just deleted.
            // A small pause solves the problem. The namespace qualifier is required on Linux.
            foundation::sleep(50);

            create_directory(m_output_directory);

            AOVContainer aovs;

            aovs.insert(
                DirectDiffuseAOVFactory().create(
                    ParamArray()
                        .insert("output_filename", (m_output_directory / "default-direct-diffuse.exr").string())));

            aovs.insert(
                IndirectDiffuseAOVFactory().create(
                    ParamArray()
                        .insert("output_filename", (m_output_directory / "default-indirect-diffuse.png").string())));

            aovs.insert(
                DirectGlossyAOVFactory().create(
                    ParamArray()
                        .insert("output_filename", (m_output_directory / "default-direct-glossy").string())));

            aovs.insert(
                IndirectGlossyAOVFactory().create(
                    ParamArray()
                        .insert("output_filename", (m_output_directory / "default-indirect-glossy.").string())));

            m_frame =
                FrameFactory::create(
                    "beauty",
                    ParamArray()
                        .insert("resolution", "64 64")
                        .insert("output_filename", (m_output_directory / "default-main.png").string()),
                    aovs);

            m_frame->clear_main_and_aov_images();
        }
    };

    TEST_CASE_F(WriteMainAndAOVImages, Fixture)
    {
        m_frame->write_main_and_aov_images();

        EXPECT_TRUE(bf::exists(m_output_directory / "default-main.png"));

        EXPECT_TRUE(bf::exists(m_output_directory / "default-direct-diffuse.exr"));
        EXPECT_TRUE(bf::exists(m_output_directory / "default-indirect-diffuse.exr"));       // note: png -> exr
        EXPECT_TRUE(bf::exists(m_output_directory / "default-direct-glossy.exr"));          // note: exr extension added
        EXPECT_TRUE(bf::exists(m_output_directory / "default-indirect-glossy.exr"));        // note: exr extension added
    }

    TEST_CASE_F(SaveCheckpoint_KeepsEveryBufferChannelNameAlive, Fixture)
    {
        const bf::path path = m_output_directory / "checkpoint.exr";
        AOVContainer aovs;
        auto frame = FrameFactory::create(
            "beauty",
            ParamArray()
                .insert("resolution", "4 4")
                .insert("checkpoint_create", true)
                .insert("checkpoint_create_path", path.string()),
            aovs);
        frame->clear_main_and_aov_images();
        PermanentShadingResultFrameBufferFactory buffers(*frame);
        frame->save_checkpoint(&buffers, 0);

        auto reader = OIIO::ImageInput::open(path.string());
        ASSERT_TRUE(reader.get() != nullptr);
        ASSERT_TRUE(reader->seek_subimage(1, 0));
        const OIIO::ImageSpec& spec = reader->spec();
        ASSERT_EQ(5, spec.nchannels);
        for (size_t channel = 0; channel < spec.channelnames.size(); ++channel)
        {
            const std::string expected = "channel_" + pad_left(to_string(channel + 1), '0', 4);
            EXPECT_EQ(expected, spec.channelnames[channel]);
        }
        EXPECT_TRUE(reader->close());
    }

    TEST_CASE_F(WriteMultipart_KeepsColorImageStorageAliveUntilWrite, Fixture)
    {
        const bf::path path = m_output_directory / "multipart-colors.exr";
        const Color4f beauty(0.25f, 0.5f, 4.0f, 1.0f);
        m_frame->image().clear(beauty);
        float value = 1.0f;
        for (const AOV& aov : m_frame->aovs())
        {
            aov.get_image().clear(Color4f(value, 0.5f, 0.25f, 1.0f));
            value += 1.0f;
        }
        m_frame->write_main_and_aov_images_to_multipart_exr(path.string().c_str());

        GenericProgressiveImageFileReader reader;
        reader.open(path.string().c_str());
        for (size_t part = 0; part <= m_frame->aovs().size(); ++part)
        {
            ASSERT_TRUE(reader.choose_subimage(part));
            const std::unique_ptr<Tile> tile(reader.read_tile(0, 0));
            Color4f pixel;
            tile->get_pixel(0, 0, pixel);
            const Color4f expected = part == 0
                ? beauty : Color4f(static_cast<float>(part), 0.5f, 0.25f, 1.0f);
            EXPECT_EQ(expected, pixel);
        }
        EXPECT_FALSE(reader.choose_subimage(1 + m_frame->aovs().size()));
        reader.close();
    }

    TEST_CASE_F(WriteSidecarsAndMultipart_KeepColorAndDataMetadataIndependent, Fixture)
    {
        AOVContainer aovs;
        aovs.insert(DirectDiffuseAOVFactory().create(ParamArray()));
        aovs.insert(NormalAOVFactory().create(ParamArray()));
        auto frame = FrameFactory::create("beauty", ParamArray().insert("resolution", "4 4"), aovs);
        frame->clear_main_and_aov_images();
        const Color3f beauty(0.25f, 0.5f, 4.0f);
        const Color3f diffuse(1.0f, 0.5f, 0.25f);
        const Color3f normal(0.25f, 0.5f, 0.75f);
        frame->image().clear(Color4f(beauty, 1.0f));
        frame->aovs().get_by_name("direct_diffuse")->get_image().clear(Color4f(diffuse, 1.0f));
        frame->aovs().get_by_name("normal")->get_image().clear(normal);

        const bf::path main_path = m_output_directory / "metadata.exr";
        const bf::path multipart_path = m_output_directory / "metadata-multipart.exr";
        ASSERT_TRUE(frame->write_main_image(main_path.string().c_str()));
        ASSERT_TRUE(frame->write_aov_images(main_path.string().c_str()));
        frame->write_main_and_aov_images_to_multipart_exr(multipart_path.string().c_str());

        auto check_part = [&](const bf::path& path, const size_t part, const char* interop,
            const Color3f& expected, const bool color_data)
        {
            GenericProgressiveImageFileReader reader;
            reader.open(path.string().c_str());
            ASSERT_TRUE(reader.choose_subimage(part));
            ImageAttributes attributes;
            reader.read_image_attributes(attributes);
            EXPECT_EQ(std::string(interop), attributes.get<std::string>("colorInteropID"));
            EXPECT_EQ(color_data, attributes.exist("chromaticities"));
            const std::unique_ptr<Tile> tile(reader.read_tile(0, 0));
            Color3f pixel;
            tile->get_pixel(0, 0, pixel);
            EXPECT_EQ(expected, pixel);
            reader.close();
        };
        check_part(main_path, 0, "lin_rec709_scene", beauty, true);
        check_part(m_output_directory / "metadata.direct_diffuse.exr", 0, "lin_rec709_scene", diffuse, true);
        check_part(m_output_directory / "metadata.normal.exr", 0, "data", normal, false);
        check_part(multipart_path, 0, "lin_rec709_scene", beauty, true);
        check_part(multipart_path, 1, "lin_rec709_scene", diffuse, true);
        check_part(multipart_path, 2, "data", normal, false);
    }

    TEST_CASE_F(WriteMainImage_FilenameHasEXRExtension_WritesEXRFile, Fixture)
    {
        m_frame->write_main_image((m_output_directory / "override.exr").string().c_str());

        EXPECT_TRUE(bf::exists(m_output_directory / "override.exr"));                       // note: file name overridden
    }

    TEST_CASE_F(WriteMainImage_FilenameHasPNGExtension_WritesPNGFile, Fixture)
    {
        m_frame->write_main_image((m_output_directory / "override.png").string().c_str());

        EXPECT_TRUE(bf::exists(m_output_directory / "override.png"));                       // note: file name overridden
    }

    TEST_CASE_F(WriteMainImage_FilenameHasNoExtension_WritesEXRFile, Fixture)
    {
        m_frame->write_main_image((m_output_directory / "override").string().c_str());

        EXPECT_TRUE(bf::exists(m_output_directory / "override.exr"));                       // note: file name overridden and exr extension added
    }

    TEST_CASE_F(WriteMainImage_FilenameEndsWithDot_WritesEXRFile, Fixture)
    {
        m_frame->write_main_image((m_output_directory / "override.").string().c_str());

        EXPECT_TRUE(bf::exists(m_output_directory / "override.exr"));                       // note: file name overridden and exr extension added
    }

    TEST_CASE_F(WriteAOVImages_FilenameHasEXRExtension_WritesEXRFiles, Fixture)
    {
        m_frame->write_aov_images((m_output_directory / "override.exr").string().c_str());

        EXPECT_TRUE(bf::exists(m_output_directory / "override.direct_diffuse.exr"));        // note: file name overridden
        EXPECT_TRUE(bf::exists(m_output_directory / "override.indirect_diffuse.exr"));      // note: file name overridden and png -> exr
        EXPECT_TRUE(bf::exists(m_output_directory / "override.direct_glossy.exr"));         // note: file name overridden and png -> exr
        EXPECT_TRUE(bf::exists(m_output_directory / "override.indirect_glossy.exr"));       // note: file name overridden and png -> exr
    }

    TEST_CASE_F(WriteAOVImages_FilenameHasPNGExtension_WritesEXRFiles, Fixture)
    {
        m_frame->write_aov_images((m_output_directory / "override.png").string().c_str());

        EXPECT_TRUE(bf::exists(m_output_directory / "override.direct_diffuse.exr"));        // note: file name overridden and png -> exr
        EXPECT_TRUE(bf::exists(m_output_directory / "override.indirect_diffuse.exr"));      // note: file name overridden and png -> exr
        EXPECT_TRUE(bf::exists(m_output_directory / "override.direct_glossy.exr"));         // note: file name overridden and png -> exr
        EXPECT_TRUE(bf::exists(m_output_directory / "override.indirect_glossy.exr"));       // note: file name overridden and png -> exr
    }

    TEST_CASE_F(WriteAOVImages_FilenameHasNoExtension_WritesEXRFiles, Fixture)
    {
        m_frame->write_aov_images((m_output_directory / "override").string().c_str());

        EXPECT_TRUE(bf::exists(m_output_directory / "override.direct_diffuse.exr"));        // note: file name overridden and exr extension added
        EXPECT_TRUE(bf::exists(m_output_directory / "override.indirect_diffuse.exr"));      // note: file name overridden and exr extension added
        EXPECT_TRUE(bf::exists(m_output_directory / "override.direct_glossy.exr"));         // note: file name overridden and exr extension added
        EXPECT_TRUE(bf::exists(m_output_directory / "override.indirect_glossy.exr"));       // note: file name overridden and exr extension added
    }

    TEST_CASE_F(WriteAOVImages_FilenameEndsWithDot_WritesEXRFiles, Fixture)
    {
        m_frame->write_aov_images((m_output_directory / "override.").string().c_str());

        EXPECT_TRUE(bf::exists(m_output_directory / "override.direct_diffuse.exr"));        // note: file name overridden and exr extension added
        EXPECT_TRUE(bf::exists(m_output_directory / "override.indirect_diffuse.exr"));      // note: file name overridden and exr extension added
        EXPECT_TRUE(bf::exists(m_output_directory / "override.direct_glossy.exr"));         // note: file name overridden and exr extension added
        EXPECT_TRUE(bf::exists(m_output_directory / "override.indirect_glossy.exr"));       // note: file name overridden and exr extension added
    }
}
