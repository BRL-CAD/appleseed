
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
#include "datetime.h"

// Standard headers.
#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <ratio>
#include <sstream>
#include <stdexcept>

namespace foundation
{

namespace
{
    using Days = std::chrono::duration<std::int64_t, std::ratio<86400>>;

    constexpr int FirstYear = 1400;
    constexpr int LastYear = 9999;
    constexpr int EpochYear = 1970;
    constexpr int DaysPerCommonYear = 365;
    constexpr std::array<int, 12> MonthLengths = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    constexpr std::array<const char*, 12> MonthNames = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    constexpr size_t CompactLength = 15;       // YYYYMMDDTHHMMSS
    constexpr size_t FractionDigits = 6;      // microseconds

    constexpr bool is_leap_year(const int year)
    {
        return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    }

    constexpr int leap_years_before(const int year)
    {
        const int previous = year - 1;
        return previous / 4 - previous / 100 + previous / 400;
    }

    constexpr std::int64_t first_day_of_year(const int year)
    {
        return DaysPerCommonYear * (year - EpochYear)
            + leap_years_before(year) - leap_years_before(EpochYear);
    }

    constexpr int days_in_month(const int year, const int month)
    {
        return MonthLengths[month - 1] + (month == 2 && is_leap_year(year) ? 1 : 0);
    }

    int read_digits(const std::string& text, const size_t start, const size_t count)
    {
        int value = 0;
        for (size_t i = start; i < start + count; ++i)
        {
            if (text[i] < '0' || text[i] > '9')
                return -1;
            value = value * 10 + text[i] - '0';
        }
        return value;
    }
}

bool parse_timestamp(const std::string& text, Timestamp& timestamp) noexcept
{
    if (text.size() < CompactLength || text[8] != 'T')
        return false;

    int fraction = 0;
    if (text.size() > CompactLength)
    {
        if (text[CompactLength] != '.' ||
            text.size() == CompactLength + 1 ||
            text.size() > CompactLength + 1 + FractionDigits)
            return false;

        const size_t digits = text.size() - CompactLength - 1;
        fraction = read_digits(text, CompactLength + 1, digits);
        if (fraction < 0)
            return false;
        for (size_t i = digits; i < FractionDigits; ++i)
            fraction *= 10;
    }

    const int year = read_digits(text, 0, 4);
    const int month = read_digits(text, 4, 2);
    const int day = read_digits(text, 6, 2);
    const int hour = read_digits(text, 9, 2);
    const int minute = read_digits(text, 11, 2);
    const int second = read_digits(text, 13, 2);
    if (year < FirstYear || year > LastYear || month < 1 || month > 12 ||
        day < 1 || day > days_in_month(year, month) ||
        hour < 0 || hour >= 24 || minute < 0 || minute >= 60 || second < 0 || second >= 60)
        return false;

    std::int64_t days = first_day_of_year(year) + day - 1;
    for (int m = 1; m < month; ++m)
        days += days_in_month(year, m);

    timestamp = Timestamp(
        std::chrono::duration_cast<Timestamp::duration>(Days(days))
        + std::chrono::hours(hour) + std::chrono::minutes(minute)
        + std::chrono::seconds(second) + std::chrono::microseconds(fraction));
    return true;
}

std::string format_timestamp(const Timestamp timestamp, const TimestampFormat format)
{
    using namespace std::chrono;

    const auto elapsed = timestamp.time_since_epoch();
    // Bound the value before converting civil days back to microseconds.
    // This also prevents overflow for arbitrary time_points near the limits
    // of their representation, beyond the supported legacy calendar range.
    if (elapsed < duration_cast<microseconds>(Days(first_day_of_year(FirstYear))) ||
        elapsed >= duration_cast<microseconds>(Days(first_day_of_year(LastYear + 1))))
        throw std::out_of_range("timestamp year outside 1400-9999");

    const Days days = floor<Days>(elapsed);
    auto remainder = elapsed - days;

    int year = FirstYear;
    int upper = LastYear + 1;
    while (year + 1 < upper)
    {
        const int middle = year + (upper - year) / 2;
        if (days.count() >= first_day_of_year(middle))
            year = middle;
        else
            upper = middle;
    }

    int day = static_cast<int>(days.count() - first_day_of_year(year));
    int month = 1;
    while (day >= days_in_month(year, month))
    {
        day -= days_in_month(year, month);
        ++month;
    }

    const auto hour = duration_cast<hours>(remainder);
    remainder -= hour;
    const auto minute = duration_cast<minutes>(remainder);
    remainder -= minute;
    const auto second = duration_cast<seconds>(remainder);
    remainder -= second;

    if (format != TimestampFormat::Compact && format != TimestampFormat::Extended &&
        format != TimestampFormat::Readable)
        throw std::invalid_argument("invalid timestamp format");

    const char* date_separator = format == TimestampFormat::Compact ? "" : "-";
    const char* time_separator = format == TimestampFormat::Compact ? "" : ":";
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setfill('0') << std::setw(4) << year << date_separator;
    if (format == TimestampFormat::Readable)
        stream << MonthNames[month - 1];
    else
        stream << std::setw(2) << month;
    stream << date_separator << std::setw(2) << day + 1
        << (format == TimestampFormat::Readable ? ' ' : 'T')
        << std::setw(2) << hour.count() << time_separator
        << std::setw(2) << minute.count() << time_separator
        << std::setw(2) << second.count();
    if (remainder.count() != 0)
        stream << '.' << std::setw(FractionDigits) << remainder.count();
    if (!stream)
        throw std::runtime_error("could not format timestamp");
    return stream.str();
}

}   // namespace foundation
