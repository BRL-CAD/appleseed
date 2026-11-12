
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
#include "foundation/platform/datetime.h"
#include "foundation/utility/test.h"

// Standard headers.
#include <chrono>
#include <cstdint>
#include <stdexcept>

using namespace foundation;

TEST_SUITE(Foundation_Platform_DateTime)
{
    struct TimestampCase
    {
        const char*     text;
        std::int64_t    microseconds;
    };

    // Known Unix microseconds, independent of the parser and formatter.
    const TimestampCase Cases[] = {
        {"14000101T000000", -17987443200000000LL},
        {"19691231T235959.999999", -1LL},
        {"19700101T000000", 0LL},
        {"20000229T000000", 951782400000000LL},
        {"20100622T174531", 1277228731000000LL},
        {"20100622T174531.001234", 1277228731001234LL},
        {"99991231T235959.999999", 253402300799999999LL}
    };

    TEST_CASE(CompactTimestamp_PreservesLegacyCalendarRangeAndMicroseconds)
    {
        for (const auto& test : Cases)
        {
            const Timestamp value{std::chrono::microseconds(test.microseconds)};
            EXPECT_EQ(test.text, format_timestamp(value));
        }
    }

    TEST_CASE(ParseTimestamp_PreservesLegacyCalendarRangeAndMicroseconds)
    {
        for (const auto& test : Cases)
        {
            Timestamp value;
            ASSERT_TRUE(parse_timestamp(test.text, value));
            EXPECT_EQ(test.microseconds, value.time_since_epoch().count());
        }
    }

    TEST_CASE(ExtendedTimestamp_PreservesUTCLogFormat)
    {
        const Timestamp value{std::chrono::microseconds(1277228731123456LL)};
        EXPECT_EQ("2010-06-22T17:45:31.123456", format_timestamp(value, TimestampFormat::Extended));
    }

    TEST_CASE(ReadableTimestamp_PreservesBenchmarkTooltipFormat)
    {
        const Timestamp value{std::chrono::microseconds(1277228731123456LL)};
        EXPECT_EQ("2010-Jun-22 17:45:31.123456", format_timestamp(value, TimestampFormat::Readable));
    }

    TEST_CASE(ParseTimestamp_PadsShortFractionsToMicroseconds)
    {
        Timestamp value;
        ASSERT_TRUE(parse_timestamp("19700101T000000.1", value));
        EXPECT_EQ(100000, value.time_since_epoch().count());
        ASSERT_TRUE(parse_timestamp("19700101T000000.000001", value));
        EXPECT_EQ(1, value.time_since_epoch().count());
    }

    TEST_CASE(ParseTimestamp_RejectsInvalidTextWithoutChangingOutput)
    {
        const char* Invalid[] = {
            "", "19700101", "19700101t000000", "19700101T00000", "19700101T000000Z",
            "19700101T000000.", "19700101T000000.1234567", "19700101T000000.x",
            "13991231T235959", "00000101T000000", "19000229T000000", "20190229T000000",
            "20000001T000000", "20001301T000000", "20000100T000000", "20000431T000000",
            "20000101T240000", "20000101T006000", "20000101T000060", "20000x01T000000"
        };
        const Timestamp initial{std::chrono::microseconds(42)};
        for (const char* text : Invalid)
        {
            Timestamp value = initial;
            EXPECT_FALSE(parse_timestamp(text, value));
            EXPECT_EQ(initial, value);
        }
    }

    TEST_CASE(FormatTimestamp_RejectsRepresentationLimitsOutsideCalendarRange)
    {
        EXPECT_EXCEPTION(std::out_of_range,
        {
            format_timestamp(Timestamp::min());
        });
        EXPECT_EXCEPTION(std::out_of_range,
        {
            format_timestamp(Timestamp::max());
        });
    }
}
