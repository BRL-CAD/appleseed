
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
#include "regexfilter.h"

// PCRE2 headers.
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

// Standard headers.
#include <cassert>
#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>

namespace foundation
{

struct RegExFilter::Impl
{
    std::unique_ptr<pcre2_code, decltype(&pcre2_code_free)> m_expression{nullptr, pcre2_code_free};
};

RegExFilter::RegExFilter()
  : impl(new Impl())
{
}

RegExFilter::RegExFilter(const char* pattern, const CaseSensivity case_sensivity)
  : RegExFilter()
{
    set_pattern(pattern, case_sensivity);
}

RegExFilter::~RegExFilter()
{
    delete impl;
}

void RegExFilter::set_pattern(const char* pattern, const CaseSensivity case_sensivity)
{
    assert(pattern);

    // Recognize the line endings used in filters on all supported platforms.
    const std::unique_ptr<pcre2_compile_context, decltype(&pcre2_compile_context_free)> context(
        pcre2_compile_context_create(nullptr), pcre2_compile_context_free);
    if (!context)
        throw std::bad_alloc();
    pcre2_set_newline(context.get(), PCRE2_NEWLINE_ANYCRLF);

    // Dots span newlines; anchors also recognize the empty line after a final newline.
    const std::uint32_t options = PCRE2_DOTALL | PCRE2_MULTILINE | PCRE2_ALT_CIRCUMFLEX
        | (case_sensivity == CaseSensitive ? 0 : PCRE2_CASELESS);
    int error;
    PCRE2_SIZE error_offset;
    pcre2_code* expression = pcre2_compile(
        reinterpret_cast<PCRE2_SPTR>(pattern), PCRE2_ZERO_TERMINATED,
        options, &error, &error_offset, context.get());
    if (!expression && error == PCRE2_ERROR_HEAP_FAILED)
        throw std::bad_alloc();

    impl->m_expression.reset(expression);
}

bool RegExFilter::is_valid() const
{
    return impl->m_expression != nullptr;
}

bool RegExFilter::accepts(const char* name) const
{
    assert(is_valid());
    assert(name);

    // A compiled pattern is immutable; independent match data allows concurrent searches.
    const std::unique_ptr<pcre2_match_data, decltype(&pcre2_match_data_free)> match(
        pcre2_match_data_create_from_pattern(impl->m_expression.get(), nullptr), pcre2_match_data_free);
    if (!match)
        throw std::bad_alloc();

    const int result = pcre2_match(
        impl->m_expression.get(), reinterpret_cast<PCRE2_SPTR>(name), PCRE2_ZERO_TERMINATED,
        0, 0, match.get(), nullptr);
    if (result >= 0)
        return true;
    if (result == PCRE2_ERROR_NOMATCH)
        return false;
    if (result == PCRE2_ERROR_NOMEMORY)
        throw std::bad_alloc();

    constexpr size_t ErrorMessageSize = 256;
    PCRE2_UCHAR message[ErrorMessageSize];
    const int length = pcre2_get_error_message(result, message, ErrorMessageSize);
    throw std::runtime_error(
        "regular expression match failed: " + (length >= 0
            ? std::string(reinterpret_cast<const char*>(message), static_cast<size_t>(length))
            : std::to_string(result)));
}

}   // namespace foundation
