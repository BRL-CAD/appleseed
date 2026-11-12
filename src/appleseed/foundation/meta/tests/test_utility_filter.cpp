
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

// appleseed.foundation headers.
#include "foundation/meta/tests/concurrenttest.hpp"
#include "foundation/utility/filter.h"
#include "foundation/utility/test.h"

// Standard headers.
#include <atomic>
#include <cstddef>

using namespace foundation;

TEST_SUITE(Foundation_Utility_Filter_RegExFilter)
{
    TEST_CASE(IsValid_GivenEmptyRegularExpression_ReturnsTrue)
    {
        const RegExFilter filter("");

        const bool result = filter.is_valid();

        EXPECT_TRUE(result);
    }

    TEST_CASE(IsValid_GivenValidRegularExpression_ReturnsTrue)
    {
        const RegExFilter filter(".*");

        const bool result = filter.is_valid();

        EXPECT_TRUE(result);
    }

    TEST_CASE(IsValid_GivenInvalidRegularExpression_ReturnsFalse)
    {
        const RegExFilter filter("*");

        const bool result = filter.is_valid();

        EXPECT_FALSE(result);
    }

    TEST_CASE(Accepts_GivenCaseSensitiveRegExAndNameMatchingPattern_ReturnsTrue)
    {
        const RegExFilter filter("hell", RegExFilter::CaseSensitive);

        const bool result = filter.accepts("hello");

        EXPECT_TRUE(result);
    }

    TEST_CASE(Accepts_GivenCaseSensitiveRegExAndNameNotMatchingPattern_ReturnsFalse)
    {
        const RegExFilter filter("hell", RegExFilter::CaseSensitive);

        const bool result = filter.accepts("jell");

        EXPECT_FALSE(result);
    }

    TEST_CASE(Accepts_GivenCaseSensitiveRegExAndNameCaseNotMatchingPatternCase_ReturnsFalse)
    {
        const RegExFilter filter("hell", RegExFilter::CaseSensitive);

        const bool result = filter.accepts("HELLO");

        EXPECT_FALSE(result);
    }

    TEST_CASE(Accepts_GivenCaseInsensitiveRegExAndNameMatchingPattern_ReturnsTrue)
    {
        const RegExFilter filter("hell", RegExFilter::CaseInsensitive);

        const bool result = filter.accepts("HELLO");

        EXPECT_TRUE(result);
    }

    TEST_CASE(Accepts_GivenCaseInsensitiveRegExAndNameNotMatchingPattern_ReturnsFalse)
    {
        const RegExFilter filter("hell", RegExFilter::CaseInsensitive);

        const bool result = filter.accepts("jell");

        EXPECT_FALSE(result);
    }
    TEST_CASE(DefaultPattern_IsInvalid)
    {
        const RegExFilter filter;
        EXPECT_FALSE(filter.is_valid());
    }

    TEST_CASE(PatternChanges_ClearErrorsAndRetainSearchSemantics)
    {
        RegExFilter filter("steel");
        EXPECT_TRUE(filter.accepts("part_steel_instance"));
        EXPECT_FALSE(filter.accepts("part_glass_instance"));
        filter.set_pattern("[");
        EXPECT_FALSE(filter.is_valid());
        filter.set_pattern("glass");
        EXPECT_TRUE(filter.is_valid());
        EXPECT_TRUE(filter.accepts("part_glass_instance"));
    }

    TEST_CASE(CaseSensitivity_CanBeChanged)
    {
        RegExFilter filter("steel");
        EXPECT_FALSE(filter.accepts("STEEL"));
        filter.set_pattern("steel", RegExFilter::CaseInsensitive);
        EXPECT_TRUE(filter.accepts("STEEL"));
        filter.set_pattern("steel", RegExFilter::CaseSensitive);
        EXPECT_FALSE(filter.accepts("STEEL"));
    }

    TEST_CASE(PerlPatterns_PreserveAdvancedFilterSyntax)
    {
        struct PatternCase
        {
            const char* pattern;
            const char* subject;
        };
        const PatternCase cases[] = {
            {"(?<=part_)steel", "part_steel"},
            {"(?i:steel)", "STEEL"},
            {"(?<material>steel)\\k<material>", "steelsteel"},
            {"\\Apart_steel\\z", "part_steel"},
            {"(part_)?(?(1)steel|glass)", "part_steel"}
        };
        for (const auto& test : cases)
        {
            RegExFilter filter(test.pattern);
            ASSERT_TRUE(filter.is_valid());
            EXPECT_TRUE(filter.accepts(test.subject));
        }
        RegExFilter atomic("(?>a|ab)c");
        EXPECT_FALSE(atomic.accepts("abc"));
        RegExFilter possessive("a*+a");
        EXPECT_FALSE(possessive.accepts("aaa"));
    }

    TEST_CASE(Newlines_PreserveDotAndAnchorDefaults)
    {
        RegExFilter dot("a.b");
        EXPECT_TRUE(dot.accepts("a\nb"));
        RegExFilter anchors("^b$");
        EXPECT_TRUE(anchors.accepts("a\nb\nc"));
        RegExFilter final_newline("a$");
        EXPECT_TRUE(final_newline.accepts("a\n"));
    }

    TEST_CASE(ConcurrentSearches_UseIndependentMatchState)
    {
        const RegExFilter filter("(?<=part_)(steel|glass)");
        std::atomic<bool> correct(true);
        test_impl::run_concurrently(8, [&](const size_t)
        {
            for (size_t i = 0; i < 256; ++i)
            {
                if (!filter.accepts("part_steel") || filter.accepts("part_wood"))
                    correct.store(false);
            }
        });
        EXPECT_TRUE(correct.load());
    }
}
