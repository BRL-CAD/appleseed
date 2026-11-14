
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
#include "renderer/kernel/rendering/ephemeralshadingresultframebufferfactory.h"
#include "renderer/kernel/rendering/final/adaptivetilerenderer.h"
#include "renderer/kernel/rendering/final/texturecontrolledpixelrenderer.h"
#include "renderer/kernel/rendering/final/uniformpixelrenderer.h"
#include "renderer/kernel/rendering/generic/generictilerenderer.h"
#include "renderer/kernel/rendering/isamplerenderer.h"
#include "renderer/kernel/shading/shadingresult.h"
#include "renderer/modeling/frame/frame.h"
#include "renderer/utility/paramarray.h"

// appleseed.foundation headers.
#include "foundation/image/color.h"
#include "foundation/image/image.h"
#include "foundation/image/tile.h"
#include "foundation/memory/autoreleaseptr.h"
#include "foundation/utility/job/abortswitch.h"
#include "foundation/utility/statistics.h"
#include "foundation/utility/test.h"

// Standard headers.
#include <filesystem>

using namespace foundation;
using namespace renderer;

TEST_SUITE(Renderer_SamplingAbort)
{
    constexpr size_t SampleBudget = 32;
    constexpr size_t AbortAfterSamples = 2;

    struct SampleCounter
    {
        AbortSwitch abort_switch;
        size_t calls = 0;
        size_t abort_after = AbortAfterSamples;
    };

    class CountingSampleRenderer : public ISampleRenderer
    {
      public:
        explicit CountingSampleRenderer(SampleCounter& counter) : m_counter(counter)
        {
        }
        void release() override
        {
            delete this;
        }
        void print_settings() const override
        {
        }
        StatisticsVector get_statistics() const override
        {
            return StatisticsVector();
        }
        void render_sample(SamplingContext&, const PixelContext&, const Vector2d&,
                           AOVAccumulatorContainer&, ShadingResult& result) override
        {
            result.m_main = Color4f(1.0f);
            ++m_counter.calls;
            if (m_counter.calls == m_counter.abort_after)
                m_counter.abort_switch.abort();
        }

      private:
        SampleCounter& m_counter;
    };

    class CountingSampleFactory : public ISampleRendererFactory
    {
      public:
        explicit CountingSampleFactory(SampleCounter& counter) : m_counter(counter)
        {
        }
        void release() override
        {
            delete this;
        }
        ISampleRenderer* create(size_t) override
        {
            return new CountingSampleRenderer(m_counter);
        }

      private:
        SampleCounter& m_counter;
    };

    class CountingFrameBuffers : public EphemeralShadingResultFrameBufferFactory
    {
      public:
        size_t active = 0;
        ShadingResultFrameBuffer* create(const Frame& frame, size_t x, size_t y,
                                         const AABB2u& bounds) override
        {
            ShadingResultFrameBuffer* buffer =
                EphemeralShadingResultFrameBufferFactory::create(frame, x, y, bounds);
            ++active;
            return buffer;
        }
        void destroy(ShadingResultFrameBuffer* buffer) override
        {
            --active;
            EphemeralShadingResultFrameBufferFactory::destroy(buffer);
        }
    };

    struct Fixture
    {
        auto_release_ptr<Frame> frame;
        SampleCounter counter;
        CountingSampleFactory samples;
        CountingFrameBuffers framebuffers;

        Fixture()
            : frame(FrameFactory::create(
                  "sampling_abort",
                  ParamArray().insert("resolution", "1 1").insert("tile_size", "1 1"))),
              samples(counter)
        {
            frame->clear_main_and_aov_images();
            counter.abort_switch.clear();
        }

        void render_generic(IPixelRendererFactory& pixels)
        {
            GenericTileRendererFactory factory(*frame, &pixels, &framebuffers, ParamArray());
            auto_release_ptr<ITileRenderer> renderer(factory.create(0));
            renderer->render_tile(*frame, 0, 0, 0, counter.abort_switch);
        }

        void render_adaptive(size_t minimum)
        {
            AdaptiveTileRendererFactory factory(*frame, &samples, &framebuffers,
                                                ParamArray()
                                                    .insert("sampling_mode", "qmc")
                                                    .insert("batch_size", SampleBudget)
                                                    .insert("min_samples", minimum)
                                                    .insert("max_samples", SampleBudget)
                                                    .insert("noise_threshold", 0.1f));
            auto_release_ptr<ITileRenderer> renderer(factory.create(0));
            renderer->render_tile(*frame, 0, 0, 0, counter.abort_switch);
        }
    };

    TEST_CASE_F(UniformChecksAbortBetweenSamples, Fixture)
    {
        UniformPixelRendererFactory pixels(
            *frame, &samples,
            ParamArray().insert("sampling_mode", "qmc").insert("samples", SampleBudget));
        render_generic(pixels);
        EXPECT_EQ(AbortAfterSamples, counter.calls);
        EXPECT_EQ(0, framebuffers.active);
    }

    TEST_CASE_F(UniformReleasesFrameBufferWhenNextPixelSeesAbort, Fixture)
    {
        frame = FrameFactory::create(
            "sampling_abort", ParamArray().insert("resolution", "2 1").insert("tile_size", "2 1"));
        frame->clear_main_and_aov_images();
        UniformPixelRendererFactory pixels(
            *frame, &samples,
            ParamArray().insert("sampling_mode", "qmc").insert("samples", SampleBudget));
        render_generic(pixels);
        EXPECT_EQ(AbortAfterSamples, counter.calls);
        EXPECT_EQ(0, framebuffers.active);
        Color4f pixel;
        frame->image().tile(0, 0).get_pixel(0, 0, pixel);
        for (size_t channel = 0; channel < 4; ++channel)
            EXPECT_FEQ(0.0f, pixel[channel]);
    }

    TEST_CASE_F(UniformFinishesWithoutAbort, Fixture)
    {
        counter.abort_after = 0;
        UniformPixelRendererFactory pixels(
            *frame, &samples,
            ParamArray().insert("sampling_mode", "qmc").insert("samples", SampleBudget));
        render_generic(pixels);
        EXPECT_EQ(SampleBudget, counter.calls);
    }

    TEST_CASE_F(TextureControlledChecksAbortBetweenSamples, Fixture)
    {
        std::filesystem::create_directories("unit tests/outputs");
        const char* path = "unit tests/outputs/sampling-abort-texture.png";
        frame->image().tile(0, 0).clear(Color4f(1.0f));
        const bool written = frame->write_main_image(path);
        EXPECT_TRUE(written);
        if (!written)
            return;
        TextureControlledPixelRendererFactory pixels(*frame, &samples,
                                                     ParamArray()
                                                         .insert("sampling_mode", "qmc")
                                                         .insert("min_samples", SampleBudget)
                                                         .insert("max_samples", SampleBudget));
        const bool loaded = pixels.load_texture(path);
        EXPECT_TRUE(loaded);
        if (!loaded)
            return;
        render_generic(pixels);
        EXPECT_EQ(AbortAfterSamples, counter.calls);
        EXPECT_EQ(0, framebuffers.active);
    }

    TEST_CASE_F(AdaptiveChecksAbortInsideInitialUniformPass, Fixture)
    {
        render_adaptive(SampleBudget);
        EXPECT_EQ(AbortAfterSamples, counter.calls);
        EXPECT_EQ(0, framebuffers.active);
    }

    TEST_CASE_F(AdaptiveInitialPassFinishesWithoutAbort, Fixture)
    {
        counter.abort_after = 0;
        render_adaptive(SampleBudget);
        EXPECT_EQ(SampleBudget, counter.calls);
        EXPECT_EQ(0, framebuffers.active);
    }

    TEST_CASE_F(AdaptiveBatchFinishesWithoutAbort, Fixture)
    {
        counter.abort_after = 0;
        render_adaptive(0);
        EXPECT_EQ(SampleBudget, counter.calls);
        EXPECT_EQ(0, framebuffers.active);
    }

    TEST_CASE_F(AdaptiveChecksAbortInsideBatch, Fixture)
    {
        render_adaptive(0);
        EXPECT_EQ(AbortAfterSamples, counter.calls);
        EXPECT_EQ(0, framebuffers.active);
    }
}
