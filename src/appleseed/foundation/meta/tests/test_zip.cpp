
//
// This source file is part of appleseed.
// Visit https://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2017-2018 Gleb Mishchenko, The appleseedhq Organization
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
#include "foundation/utility/countof.h"
#include "foundation/utility/minizip/unzip.h"
#include "foundation/utility/test.h"
#include "foundation/utility/zip.h"

// Boost headers.
#include "boost/filesystem.hpp"

// OpenImageIO headers.
#include "foundation/platform/_beginoiioheaders.h"
#include "OpenImageIO/filesystem.h"
#include "foundation/platform/_endoiioheaders.h"

// Standard headers.
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <set>
#include <string>
#include <vector>

using namespace foundation;
namespace bf = boost::filesystem;

TEST_SUITE(Foundation_Utility_Zip)
{
    TEST_CASE(Unzip)
    {
        const std::string TargetDirectory = "unit tests/outputs/test_zip/";

        try
        {
            ASSERT_FALSE(bf::exists(TargetDirectory));

            unzip("unit tests/inputs/test_zip_validzipfile.zip", TargetDirectory);

            EXPECT_TRUE(bf::exists(TargetDirectory));
            EXPECT_FALSE(bf::is_empty(TargetDirectory));

            const std::string ExpectedFiles[] =
            {
                "subfolder/a.txt",
                "subfolder/b.txt",
                "c.txt",
                "d.png"
            };

            const std::set<std::string> actual_files = recursive_ls(TargetDirectory);

            for (size_t i = 0; i < countof(ExpectedFiles); ++i)
                EXPECT_EQ(1, actual_files.count(ExpectedFiles[i]));

            bf::remove_all(TargetDirectory);
        }
        catch (const std::exception& e)
        {
            bf::remove_all(TargetDirectory);
            throw e;
        }
    }

    TEST_CASE(ZipUnzipRoundtrip)
    {
        const std::string InitialDirectory = "unit tests/inputs/test_zip";
        const std::string TargetZip = "unit tests/outputs/test_zip.zip";
        const std::string TargetDirectory = "unit tests/outputs/test_zip";

        try
        {
            ASSERT_TRUE(bf::exists(InitialDirectory));
            ASSERT_FALSE(bf::exists(TargetZip));
            ASSERT_FALSE(bf::exists(TargetDirectory));

            zip(TargetZip, InitialDirectory);
            unzip(TargetZip, TargetDirectory);

            const std::set<std::string> expected_files = recursive_ls(InitialDirectory);
            const std::set<std::string> actual_files = recursive_ls(TargetDirectory);

            ASSERT_EQ(expected_files.size(), actual_files.size());

            for (std::set<std::string>::iterator it = actual_files.begin(); it != actual_files.end(); ++it)
                EXPECT_EQ(1, expected_files.count(*it));

            bf::remove(TargetZip);
            bf::remove_all(TargetDirectory);
        }
        catch (const std::exception& e)
        {
            bf::remove(TargetZip);
            bf::remove_all(TargetDirectory);
            throw e;
        }
    }

    struct TimestampFixture
    {
        const std::filesystem::path m_root;

        TimestampFixture()
          : m_root("unit tests/outputs/test_zip_timestamp")
        {
            std::filesystem::remove_all(m_root);
            std::filesystem::create_directories(m_root / "input");
            std::ofstream file;
            file.exceptions(std::ios::failbit | std::ios::badbit);
            file.open(m_root / "input/stamp.txt");
            file << "timestamp";
        }

        ~TimestampFixture()
        {
            std::error_code ignored;
            std::filesystem::remove_all(m_root, ignored);
        }
    };

    TEST_CASE_F(Zip_PreservesFileModificationTime, TimestampFixture)
    {
        std::tm date = {};
        date.tm_year = 2020 - 1900;
        date.tm_mon = 0;
        date.tm_mday = 2;
        date.tm_hour = 12;
        date.tm_min = 34;
        // DOS timestamps store seconds in pairs.
        date.tm_sec = 56;
        date.tm_isdst = -1;
        const std::time_t timestamp = std::mktime(&date);
        ASSERT_TRUE(timestamp != std::time_t(-1));
        OIIO::Filesystem::last_write_time((m_root / "input/stamp.txt").u8string(), timestamp);
        ASSERT_EQ(timestamp,
            OIIO::Filesystem::last_write_time((m_root / "input/stamp.txt").u8string()));

        const std::string archive_path = (m_root / "stamp.zip").string();
        zip(archive_path, (m_root / "input").string());

        struct ArchiveReader
        {
            unzFile m_file;
            ~ArchiveReader()
            {
                if (m_file)
                    unzClose(m_file);
            }
        } archive = { unzOpen(archive_path.c_str()) };
        ASSERT_TRUE(archive.m_file != nullptr);
        ASSERT_EQ(UNZ_OK, unzGoToFirstFile(archive.m_file));
        unz_file_info info = {};
        ASSERT_EQ(UNZ_OK, unzGetCurrentFileInfo(
            archive.m_file, &info, nullptr, 0, nullptr, 0, nullptr, 0));
        EXPECT_EQ(2020, info.tmu_date.tm_year);
        EXPECT_EQ(0, info.tmu_date.tm_mon);
        EXPECT_EQ(2, info.tmu_date.tm_mday);
        EXPECT_EQ(12, info.tmu_date.tm_hour);
        EXPECT_EQ(34, info.tmu_date.tm_min);
        EXPECT_EQ(56, info.tmu_date.tm_sec);
    }

    TEST_CASE(IsZipFile_GivenValidZipFile_ReturnsTrue)
    {
        EXPECT_TRUE(is_zip_file("unit tests/inputs/test_zip_validzipfile.zip"));
    }

    TEST_CASE(IsZipFile_GivenInvalidZipFile_ReturnsFalse)
    {
        EXPECT_FALSE(is_zip_file("unit tests/inputs/test_zip_invalidzipfile.zip"));
    }

    TEST_CASE(GetFilenamesWithExtensionFromZip)
    {
        const std::vector<std::string> files =
            get_filenames_with_extension_from_zip(
                "unit tests/inputs/test_zip_validzipfile.zip",
                ".txt");

        ASSERT_EQ(3, files.size());

        EXPECT_TRUE(find(files.begin(), files.end(), "subfolder/a.txt") != files.end());
        EXPECT_TRUE(find(files.begin(), files.end(), "subfolder/b.txt") != files.end());
        EXPECT_TRUE(find(files.begin(), files.end(), "c.txt")           != files.end());
    }
}
