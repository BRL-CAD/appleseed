
//
// This source file is part of appleseed.
// Visit https://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
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
#include "foundation/platform/atomic.h"
#include "foundation/platform/thread.h"
#include "foundation/utility/test.h"
#include "foundation/utility/uid.h"

// Standard headers.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <thread>
#include <vector>

using namespace foundation;

TEST_SUITE(Foundation_Platform_Atomic)
{
    TEST_CASE(ReadWrite_RoundTripAllBits)
    {
        std::uint32_t value = 0;
        const auto maximum = std::numeric_limits<std::uint32_t>::max();
        atomic_write(&value, maximum);
        EXPECT_EQ(maximum, atomic_read(&value));
        atomic_write(&value, 0);
        EXPECT_EQ(0, atomic_read(&value));
    }

    TEST_CASE(IncrementAndDecrement_ReturnOldValueAndWrap)
    {
        const auto maximum = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t value = maximum;
        EXPECT_EQ(maximum, atomic_inc(&value));
        EXPECT_EQ(0, atomic_read(&value));
        EXPECT_EQ(0, atomic_dec(&value));
        EXPECT_EQ(maximum, atomic_read(&value));
    }

    TEST_CASE(CompareExchange_ReturnsObservedValueOnSuccessAndFailure)
    {
        std::uint32_t value = 7;
        EXPECT_EQ(7, atomic_cas(&value, 7, 11));
        EXPECT_EQ(11, atomic_read(&value));
        EXPECT_EQ(11, atomic_cas(&value, 7, 19));
        EXPECT_EQ(11, atomic_read(&value));
    }

    TEST_CASE(ConcurrentIncrements_ReturnEveryOldValueExactlyOnce)
    {
        const size_t WorkerCount = 8;
        const size_t IncrementsPerWorker = 4096;
        const size_t TotalIncrements = WorkerCount * IncrementsPerWorker;
        std::uint32_t value = 0;
        std::vector<std::uint32_t> observed(TotalIncrements);
        test_impl::run_concurrently(WorkerCount, [&](const size_t worker)
        {
            for (size_t i = 0; i < IncrementsPerWorker; ++i)
                observed[worker * IncrementsPerWorker + i] = atomic_inc(&value);
        });

        EXPECT_EQ(TotalIncrements, atomic_read(&value));
        std::sort(observed.begin(), observed.end());
        EXPECT_EQ(0, observed.front());
        EXPECT_EQ(TotalIncrements - 1, observed.back());
        EXPECT_TRUE(std::adjacent_find(observed.begin(), observed.end()) == observed.end());
    }

    TEST_CASE(ConcurrentCompareExchanges_DoNotLoseUpdates)
    {
        const size_t WorkerCount = 8;
        const size_t IncrementsPerWorker = 4096;
        std::uint32_t value = 0;
        test_impl::run_concurrently(WorkerCount, [&](const size_t worker)
        {
            for (size_t i = 0; i < IncrementsPerWorker; ++i)
            {
                auto expected = atomic_read(&value);
                while (true)
                {
                    const auto observed = atomic_cas(&value, expected, expected + 1);
                    if (observed == expected)
                        break;
                    expected = observed;
                }
            }
        });
        EXPECT_EQ(WorkerCount * IncrementsPerWorker, atomic_read(&value));
    }

    TEST_CASE(ConcurrentFloatAdds_DoNotLoseUpdates)
    {
        const size_t WorkerCount = 8;
        const size_t AdditionsPerWorker = 4096;
        const float Operand = 0.5f;
        float value = 0.0f;
        test_impl::run_concurrently(WorkerCount, [&](const size_t worker)
        {
            for (size_t i = 0; i < AdditionsPerWorker; ++i)
                atomic_add(&value, Operand);
        });
        // The sum is exactly representable, so rounding cannot mask lost updates.
        EXPECT_EQ(WorkerCount * AdditionsPerWorker * Operand, value);
    }
}

TEST_SUITE(Foundation_Platform_ThreadFlag)
{
    TEST_CASE(CopiesPreserveSnapshotsWithoutSharingFlagState)
    {
        ThreadFlag original;
        EXPECT_TRUE(original.is_clear());
        original.set();
        ThreadFlag copy(original);
        original.clear();
        EXPECT_TRUE(copy.is_set());
        EXPECT_TRUE(original.is_clear());
        copy = original;
        EXPECT_TRUE(copy.is_clear());
    }

    TEST_CASE(Set_PublishesPreviouslyWrittenData)
    {
        ThreadFlag ready;
        std::array<size_t, 64> payload = {};
        std::array<size_t, 64> observed = {};
        test_impl::run_concurrently(2, [&](const size_t worker)
        {
            if (worker == 0)
            {
                for (size_t i = 0; i < payload.size(); ++i)
                    payload[i] = i + 1;
                ready.set();
            }
            else
            {
                while (ready.is_clear())
                    std::this_thread::yield();
                observed = payload;
            }
        });
        EXPECT_EQ(payload.size(), observed.back());
        EXPECT_TRUE(payload == observed);
    }
}

TEST_SUITE(Foundation_Utility_UID)
{
    TEST_CASE(ConcurrentGeneration_DoesNotProduceDuplicateIdentifiers)
    {
        const size_t WorkerCount = 8;
        const size_t IdentifiersPerWorker = 4096;
        std::vector<UniqueID> identifiers(WorkerCount * IdentifiersPerWorker);
        test_impl::run_concurrently(WorkerCount, [&](const size_t worker)
        {
            for (size_t i = 0; i < IdentifiersPerWorker; ++i)
                identifiers[worker * IdentifiersPerWorker + i] = new_guid();
        });
        std::sort(identifiers.begin(), identifiers.end());
        EXPECT_TRUE(std::adjacent_find(identifiers.begin(), identifiers.end()) == identifiers.end());
    }
}
