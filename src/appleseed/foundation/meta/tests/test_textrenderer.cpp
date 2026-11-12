
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

// appleseed.foundation headers.
#include "foundation/image/color.h"
#include "foundation/image/image.h"
#include "foundation/image/pixel.h"
#include "foundation/image/text/textrenderer.h"
#include "foundation/resources/fonts/Ubuntu-L.ttf.h"
#include "foundation/utility/iostreamop.h"
#include "foundation/utility/test.h"

// External headers.
#include <struetype.h>

// Standard headers.
#include <cmath>
#include <limits>
#include <stdexcept>

using namespace foundation;

TEST_SUITE(Foundation_Image_TextRenderer)
{
    constexpr size_t Width = 128;
    constexpr size_t Height = 64;
    constexpr float FontHeight = 24.0f;

    TEST_CASE(BuiltinFonts_HavePositiveMetricsAndLinearSizeScaling)
    {
        for (const auto font : {TextRenderer::Font::UbuntuL, TextRenderer::Font::UbuntuM})
        {
            const float width = TextRenderer::compute_string_width(font, FontHeight, "art OSL");
            EXPECT_TRUE(std::isfinite(width) && width > 0.0f);
            EXPECT_FEQ(2.0f * width,
                TextRenderer::compute_string_width(font, 2.0f * FontHeight, "art OSL"));
            EXPECT_EQ(0.0f, TextRenderer::compute_string_width(font, FontHeight, ""));
        }
    }

    TEST_CASE(CustomFont_UsesExactBufferSizeAndMatchesBuiltin)
    {
        stt_fontinfo font{};
        EXPECT_FALSE(stt_InitFont(&font, Ubuntu_L_ttf, 16, 0));
        EXPECT_TRUE(stt_InitFont(&font, Ubuntu_L_ttf, int(Ubuntu_L_ttf_length), 0));
        EXPECT_EQ(
            TextRenderer::compute_string_width(TextRenderer::Font::UbuntuL, FontHeight, "art"),
            TextRenderer::compute_string_width(font, FontHeight, "art"));
    }

    TEST_CASE(Kerning_ReducesWidthOfAVPair)
    {
        constexpr auto Font = TextRenderer::Font::UbuntuL;
        const float separate = TextRenderer::compute_string_width(Font, FontHeight, "A") +
            TextRenderer::compute_string_width(Font, FontHeight, "V");
        EXPECT_TRUE(TextRenderer::compute_string_width(Font, FontHeight, "AV") < separate);
    }

    TEST_CASE(Paragraphs_MeasureLongestLineAndExplicitSpacing)
    {
        constexpr auto Font = TextRenderer::Font::UbuntuL;
        EXPECT_FEQ(TextRenderer::compute_string_width(Font, FontHeight, "art OSL"),
            TextRenderer::compute_string_width(Font, FontHeight, "A\nart OSL"));
        EXPECT_EQ(3.0f * FontHeight,
            TextRenderer::compute_string_height(FontHeight, 2.0f, "art\nOSL"));
    }

    TEST_CASE(DrawString_ProducesFinitePremultipliedCoverageWithBothFonts)
    {
        const Color4f color(0.8f, 0.4f, 0.2f, 0.5f);
        for (const auto font : {TextRenderer::Font::UbuntuL, TextRenderer::Font::UbuntuM})
        {
            Image image(Width, Height, Width, Height, 4, PixelFormatFloat);
            TextRenderer::draw_string(image, font, FontHeight, color, 5.25f, 4.5f, "art\nOSL");
            size_t covered = 0;
            for (size_t y = 0; y < Height; ++y)
            {
                for (size_t x = 0; x < Width; ++x)
                {
                    Color4f pixel;
                    image.get_pixel(x, y, pixel);
                    EXPECT_TRUE(std::isfinite(pixel.a) && pixel.a >= 0.0f && pixel.a <= color.a);
                    EXPECT_FEQ(color.r * pixel.a, pixel.r);
                    EXPECT_FEQ(color.g * pixel.a, pixel.g);
                    EXPECT_FEQ(color.b * pixel.a, pixel.b);
                    if (pixel.a > 0.0f)
                        ++covered;
                }
            }
            EXPECT_TRUE(covered > 0);
        }
    }

    TEST_CASE(DrawString_ClipsAndLeavesOffCanvasTextInvisible)
    {
        Image image(Width, Height, Width, Height, 4, PixelFormatFloat);
        TextRenderer::draw_string(image, TextRenderer::Font::UbuntuL, FontHeight,
            Color4f(1.0f), -2.5f * Width, 0.0f, "art");
        for (size_t y = 0; y < Height; ++y)
        {
            for (size_t x = 0; x < Width; ++x)
            {
                Color4f pixel;
                image.get_pixel(x, y, pixel);
                EXPECT_EQ(Color4f(0.0f), pixel);
            }
        }
    }

    TEST_CASE(InvalidMetrics_AreRejected)
    {
        constexpr auto Font = TextRenderer::Font::UbuntuL;
        for (const float height : {0.0f, -1.0f, std::numeric_limits<float>::infinity(),
                                  std::numeric_limits<float>::quiet_NaN()})
        {
            EXPECT_EXCEPTION(std::invalid_argument,
                TextRenderer::compute_string_width(Font, height, "art"));
            EXPECT_EXCEPTION(std::invalid_argument,
                TextRenderer::compute_string_height(height, "art"));
        }
        EXPECT_EXCEPTION(std::invalid_argument,
            TextRenderer::compute_string_width(Font, FontHeight, nullptr));
        EXPECT_EXCEPTION(std::invalid_argument,
            TextRenderer::compute_string_height(FontHeight, 0.0f, "art"));
    }
}
