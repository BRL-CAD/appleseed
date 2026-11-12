
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

// Interface header.
#include "textrenderer.h"

// appleseed.foundation headers.
#include "foundation/image/canvasproperties.h"
#include "foundation/image/image.h"
#include "foundation/resources/fonts/Ubuntu-L.ttf.h"
#include "foundation/resources/fonts/Ubuntu-M.ttf.h"

// External headers.
#include <struetype.h>

// Standard headers.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>

namespace foundation
{

//
// TextRenderer class implementation.
//

namespace
{
    // Default spacing between lines as a factor of the font size.
    const float DefaultLineSpacing = 1.1f;

    struct BuiltinFonts
    {
        stt_fontinfo m_ubuntu_l_font;
        stt_fontinfo m_ubuntu_m_font;

        BuiltinFonts()
        {
            static_assert(Ubuntu_L_ttf_length <= std::numeric_limits<int>::max());
            static_assert(Ubuntu_M_ttf_length <= std::numeric_limits<int>::max());
            if (!stt_InitFont(&m_ubuntu_l_font, Ubuntu_L_ttf, int(Ubuntu_L_ttf_length), 0) ||
                !stt_InitFont(&m_ubuntu_m_font, Ubuntu_M_ttf, int(Ubuntu_M_ttf_length), 0))
                throw std::runtime_error("Cannot initialize built-in fonts");
        }
    };

    const stt_fontinfo& builtin_font(const TextRenderer::Font font)
    {
        static const BuiltinFonts fonts;
        return font == TextRenderer::Font::UbuntuL
            ? fonts.m_ubuntu_l_font
            : fonts.m_ubuntu_m_font;
    }

    void validate_height(const float height)
    {
        if (!std::isfinite(height) || height <= 0.0f)
            throw std::invalid_argument("Font height must be finite and positive");
    }

    float font_scale(const stt_fontinfo& font, const float height)
    {
        validate_height(height);
        const float scale = stt_ScaleForPixelHeight(&font, height);
        if (!std::isfinite(scale) || scale <= 0.0f)
            throw std::invalid_argument("Invalid font scale");
        return scale;
    }

    struct BitmapDeleter
    {
        void operator()(unsigned char* bitmap) const
        {
            stt_FreeBitmap(bitmap, nullptr);
        }
    };
}

float TextRenderer::compute_string_width(
    const Font              font,
    const float             font_height,
    const char*             string)
{
    const stt_fontinfo& font_info = builtin_font(font);

    return compute_string_width(font_info, font_height, string);
}

float TextRenderer::compute_string_width(
    const stt_fontinfo&      font_info,
    const float             font_height,
    const char*             string)
{
    if (!string)
        throw std::invalid_argument("Text must not be null");
    float width = 0.0f, max_width = 0.0f;

    // Compute font scaling factor for the desired pixel height.
    const float scale = font_scale(font_info, font_height);

    for (const char* s = string; *s; ++s)
    {
        // Codepoint to render.
        const unsigned char cp = static_cast<unsigned char>(s[0]);

        // Handle carriage returns.
        if (cp == '\n')
        {
            if (max_width < width)
                max_width = width;
            width = 0.0f;
            continue;
        }

        // Advance cursor.
        int advance_width;
        stt_GetCodepointHMetrics(&font_info, cp, &advance_width, nullptr);
        width += advance_width * scale;

        // Apply kerning.
        const unsigned char next_cp = static_cast<unsigned char>(s[1]);
        if (next_cp != 0)
        {
            const int kern_advance =
                stt_GetCodepointKernAdvance(&font_info, cp, next_cp);
            width += kern_advance * scale;
        }
    }

    return std::max(width, max_width);
}
    
float TextRenderer::compute_string_height(
    const float             font_height,
    const char*             string)
{
    return
        compute_string_height(
            font_height,
            DefaultLineSpacing,
            string);
}

float TextRenderer::compute_string_height(
    const float             font_height,
    const float             line_spacing,
    const char*             string)
{
    validate_height(font_height);
    if (!string || !std::isfinite(line_spacing) || line_spacing <= 0.0f)
        throw std::invalid_argument("Invalid text or line spacing");
    size_t lines = 1;

    for (const char* s = string; *s; ++s)
    {
        if (*s == '\n')
            ++lines;
    }

    return font_height * (1 + (lines - 1) * line_spacing);
}

void TextRenderer::draw_string(
    Image&                  image,
    const Font              font,
    const float             font_height,
    const Color4f&          color,
    const float             origin_x,
    const float             origin_y,
    const char*             string)
{
    const stt_fontinfo& font_info = builtin_font(font);

    draw_string(
        image,
        font_info,
        font_height,
        color,
        origin_x,
        origin_y,
        string);
}

void TextRenderer::draw_string(
    Image&                  image,
    const Font              font,
    const float             font_height,
    const float             line_spacing,
    const Color4f&          color,
    const float             origin_x,
    const float             origin_y,
    const char*             string)
{
    const stt_fontinfo& font_info = builtin_font(font);

    draw_string(
        image,
        font_info,
        font_height,
        line_spacing,
        color,
        origin_x,
        origin_y,
        string);
}

void TextRenderer::draw_string(
    Image&                  image,
    const stt_fontinfo&      font_info,
    const float             font_height,
    const Color4f&          color,
    const float             origin_x,
    const float             origin_y,
    const char*             string)
{
    draw_string(
        image,
        font_info,
        font_height,
        DefaultLineSpacing,
        color,
        origin_x,
        origin_y,
        string);
}

void TextRenderer::draw_string(
    Image&                  image,
    const stt_fontinfo&      font_info,
    const float             font_height,
    const float             line_spacing,
    const Color4f&          color,
    const float             origin_x,
    const float             origin_y,
    const char*             string)
{
    if (!string || !std::isfinite(line_spacing) || line_spacing <= 0.0f ||
        !std::isfinite(origin_x) || !std::isfinite(origin_y))
        throw std::invalid_argument("Invalid text, line spacing, or origin");
    const CanvasProperties& props = image.properties();
    const size_t canvas_width = props.m_canvas_width;
    const size_t canvas_height = props.m_canvas_height;

    // Compute font scaling factor for the desired pixel height.
    const float scale = font_scale(font_info, font_height);

    // Compute baseline.
    int ascent;
    stt_GetFontVMetrics(&font_info, &ascent, nullptr, nullptr);
    const double baseline = double(ascent) * scale;

    float x = origin_x;
    float y = origin_y;

    for (const char* s = string; *s; ++s)
    {
        // Codepoint to render.
        const unsigned char cp = static_cast<unsigned char>(s[0]);

        // Handle carriage returns.
        if (cp == '\n')
        {
            x = origin_x;
            y += font_height * line_spacing;
            continue;
        }

        if (!std::isfinite(x) || !std::isfinite(y))
            throw std::overflow_error("Text position is out of range");

        // Compute subpixel shifts.
        const float shift_x = x - std::floor(x);
        const float shift_y = y - std::floor(y);

        // Let the bounded backend validate and allocate the glyph bitmap.
        int glyph_w = 0, glyph_h = 0, glyph_x0 = 0, glyph_y0 = 0;
        const std::unique_ptr<unsigned char, BitmapDeleter> glyph_bitmap(
            stt_GetCodepointBitmapSubpixel(
                &font_info, scale, scale, shift_x, shift_y, cp,
                &glyph_w, &glyph_h, &glyph_x0, &glyph_y0));
        if (glyph_w < 0 || glyph_h < 0 || (glyph_w && glyph_h && !glyph_bitmap))
            throw std::runtime_error("Cannot rasterize text glyph");

        // Blit glyph bitmap to image.
        for (int j = 0; j < glyph_h; ++j)
        {
            const double pixel_y = double(y) + j + glyph_y0 + baseline;
            if (pixel_y < 0 || pixel_y >= canvas_height)
                continue;
            const size_t iy = static_cast<size_t>(pixel_y);

            for (int i = 0; i < glyph_w; ++i)
            {
                const double pixel_x = double(x) + i + glyph_x0;
                if (pixel_x < 0 || pixel_x >= canvas_width)
                    continue;
                const size_t ix = static_cast<size_t>(pixel_x);

                // Compute text opacity.
                const std::uint8_t alpha_uint8 =
                    glyph_bitmap.get()[size_t(j) * size_t(glyph_w) + size_t(i)];
                if (alpha_uint8 == 0)
                    continue;
                const float alpha = alpha_uint8 * (1.0f / 255.0f);

                // Retrieve background color.
                Color4f background_premult;
                image.get_pixel(ix, iy, background_premult);

                // Compute premultiplied text color.
                Color4f color_premult = color;
                color_premult.a *= alpha;
                color_premult.premultiply_in_place();

                // Composite text over background.
                color_premult += background_premult * (1.0f - color_premult.a);

                // Write final color to image.
                image.set_pixel(ix, iy, color_premult);
            }
        }

        // Advance cursor.
        int advance_width;
        stt_GetCodepointHMetrics(&font_info, cp, &advance_width, nullptr);
        x += advance_width * scale;

        // Apply kerning.
        const unsigned char next_cp = static_cast<unsigned char>(s[1]);
        if (next_cp != 0)
        {
            const int kern_advance =
                stt_GetCodepointKernAdvance(&font_info, cp, next_cp);
            x += kern_advance * scale;
        }
    }
}

}   // namespace foundation
