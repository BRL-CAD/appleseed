
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
#include "foundation/string/string.h"
#include "foundation/utility/test.h"

// Boost headers.
#include "boost/date_time/gregorian/gregorian.hpp"
#include "boost/date_time/posix_time/posix_time.hpp"

// Standard headers.
#include <string>

using namespace boost;
using namespace foundation;

TEST_SUITE(Foundation_Platform_DateTime)
{
    TEST_CASE(MicrosecondsToTimeDuration)
    {
        using namespace posix_time;

        const time_duration Expected(17, 29, 43, 1234);

        const time_duration result(
            microseconds_to_time_duration(Expected.total_microseconds()));

        EXPECT_EQ(Expected, result);
    }

    TEST_CASE(PTimeToString)
    {
        using namespace gregorian;
        using namespace posix_time;

        const ptime time(date(2010, Jun, 22), time_duration(17, 45, 31));
        const std::string result = to_string(time);

        EXPECT_EQ("20100622T174531", result);
    }
    TEST_CASE(CompactTimestamp_PreservesMicroseconds)
    {
        const posix_time::ptime time(gregorian::date(2010, 6, 22), posix_time::time_duration(17, 45, 31, 1234));
        EXPECT_EQ("20100622T174531.001234", to_string(time));
    }

    TEST_CASE(ExtendedTimestamp_PreservesUTCLogFormat)
    {
        const posix_time::ptime time(gregorian::date(2010, 6, 22), posix_time::time_duration(17, 45, 31, 123456));
        EXPECT_EQ("2010-06-22T17:45:31.123456", posix_time::to_iso_extended_string(time));
    }

    TEST_CASE(ReadableTimestamp_PreservesBenchmarkTooltipFormat)
    {
        const posix_time::ptime time(gregorian::date(2010, 6, 22), posix_time::time_duration(17, 45, 31, 123456));
        EXPECT_EQ("2010-Jun-22 17:45:31.123456", posix_time::to_simple_string(time));
    }

    TEST_CASE(CompactTimestamp_PreservesLegacyCalendarRange)
    {
        EXPECT_EQ("14000101T000000", to_string(posix_time::ptime(gregorian::date(1400, 1, 1))));
        EXPECT_EQ("19691231T235959.999999", to_string(posix_time::ptime(gregorian::date(1969, 12, 31), posix_time::time_duration(23, 59, 59, 999999))));
        EXPECT_EQ("19700101T000000", to_string(posix_time::ptime(gregorian::date(1970, 1, 1))));
        EXPECT_EQ("20000229T000000", to_string(posix_time::ptime(gregorian::date(2000, 2, 29))));
        EXPECT_EQ("99991231T235959.999999", to_string(posix_time::ptime(gregorian::date(9999, 12, 31), posix_time::time_duration(23, 59, 59, 999999))));
    }

}
