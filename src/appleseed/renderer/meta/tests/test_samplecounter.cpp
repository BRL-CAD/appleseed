
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

// appleseed.renderer headers.
#include "renderer/kernel/rendering/progressive/samplecounter.h"
#include "renderer/kernel/rendering/globalsampleaccumulationbuffer.h"

// appleseed.foundation headers.
#include "foundation/meta/tests/concurrenttest.hpp"
#include "foundation/utility/test.h"

// Standard headers.
#include <cstdint>
#include <numeric>
#include <vector>

using namespace renderer;

TEST_SUITE(Renderer_Kernel_Rendering_Progressive_SampleCounter)
{
    TEST_CASE(Constructor_InitializesSampleCountToZero)
    {
        SampleCounter sample_counter(3);

        EXPECT_EQ(0, sample_counter.read());
    }

    TEST_CASE(Clear_GivenThatOneSampleWasReserved_ResetSampleCountToZero)
    {
        SampleCounter sample_counter(3);
        sample_counter.reserve(1);

        sample_counter.clear();

        EXPECT_EQ(0, sample_counter.read());
    }

    TEST_CASE(Reserve_ReserveOneGivenMaxSampleCountIsZero_ReturnsZero)
    {
        SampleCounter sample_counter(0);

        EXPECT_EQ(0, sample_counter.reserve(1));
    }

    TEST_CASE(Reserve_ReserveOneGivenMaxSampleCountIsThree_ReturnsOne)
    {
        SampleCounter sample_counter(3);

        EXPECT_EQ(1, sample_counter.reserve(1));
    }

    TEST_CASE(Reserve_ReserveThreeGivenMaxSampleCountIsOne_ReturnsOne)
    {
        SampleCounter sample_counter(1);

        EXPECT_EQ(1, sample_counter.reserve(3));
    }

    TEST_CASE(Reserve_ReserveThreeAfterReserveThreeGivenMaxSampleCountIsOne_ReturnsZero)
    {
        SampleCounter sample_counter(1);
        sample_counter.reserve(3);

        EXPECT_EQ(0, sample_counter.reserve(3));
    }
}

TEST_SUITE(Renderer_Kernel_Rendering_Progressive_SampleCounter_Concurrency)
{
    TEST_CASE(ConcurrentReservations_DoNotExceedTheLimit)
    {
        const size_t WorkerCount = 8;
        const std::uint64_t MaximumSamples = 10003;
        const std::uint64_t RequestSize = 7;
        SampleCounter counter(MaximumSamples);
        std::vector<std::uint64_t> reserved(WorkerCount, 0);
        foundation::test_impl::run_concurrently(WorkerCount, [&](const size_t worker)
        {
            while (const auto count = counter.reserve(RequestSize))
                reserved[worker] += count;
        });
        EXPECT_EQ(MaximumSamples, counter.read());
        EXPECT_EQ(MaximumSamples, std::accumulate(reserved.begin(), reserved.end(), std::uint64_t(0)));
        EXPECT_EQ(0, counter.reserve(RequestSize));
        counter.clear();
        EXPECT_EQ(0, counter.read());
        EXPECT_EQ(RequestSize, counter.reserve(RequestSize));
    }
}

TEST_SUITE(Renderer_Kernel_Rendering_SampleAccumulationBuffer)
{
    TEST_CASE(Constructor_InitializesStoredSampleCount)
    {
        GlobalSampleAccumulationBuffer buffer(2, 2);
        EXPECT_EQ(0, buffer.get_sample_count());
    }
}
