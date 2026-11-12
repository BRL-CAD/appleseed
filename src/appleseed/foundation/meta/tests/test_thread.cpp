
//
// This source file is part of appleseed.
// Visit https://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2015-2018 Francois Beaune, The appleseedhq Organization
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
#include "foundation/platform/defaulttimers.h"
#include "foundation/platform/thread.h"
#include "foundation/utility/job/abortswitch.h"
#include "foundation/utility/stopwatch.h"
#include "foundation/utility/test.h"

// Standard headers.
#include <atomic>
#include <cstddef>

using namespace foundation;

TEST_SUITE(Foundation_Platform_Thread)
{
    TEST_CASE(Spinlock_ExcludesOtherOwnersAndCanBeReacquired)
    {
        Spinlock lock;
        EXPECT_TRUE(lock.try_lock());
        EXPECT_FALSE(lock.try_lock());
        lock.unlock();
        EXPECT_TRUE(lock.try_lock());
        lock.unlock();
    }

    TEST_CASE(Spinlock_ProtectsConcurrentUpdates)
    {
        const size_t WorkerCount = 8;
        const size_t UpdatesPerWorker = 1024;
        Spinlock lock;
        size_t count = 0;
        test_impl::run_concurrently(WorkerCount, [&](const size_t)
        {
            for (size_t i = 0; i < UpdatesPerWorker; ++i)
            {
                Spinlock::ScopedLock guard(lock);
                ++count;
            }
        });
        EXPECT_EQ(WorkerCount * UpdatesPerWorker, count);
    }

    TEST_CASE(ReadWriteLock_ReadersObserveCompleteUpdates)
    {
        const size_t WorkerCount = 8;
        const size_t IterationsPerWorker = 1024;
        ReadWriteLock<YieldWaitPolicy> lock;
        size_t first = 0;
        size_t second = 0;
        std::atomic<bool> consistent(true);
        test_impl::run_concurrently(WorkerCount, [&](const size_t worker)
        {
            for (size_t i = 0; i < IterationsPerWorker; ++i)
            {
                if (worker % 2 == 0)
                {
                    ReadWriteLock<YieldWaitPolicy>::ScopedWriteLock guard(lock);
                    ++first;
                    ++second;
                }
                else
                {
                    ReadWriteLock<YieldWaitPolicy>::ScopedReadLock guard(lock);
                    if (first != second)
                        consistent.store(false);
                }
            }
        });
        EXPECT_TRUE(consistent.load());
        EXPECT_EQ(WorkerCount / 2 * IterationsPerWorker, first);
        EXPECT_EQ(first, second);
    }

    TEST_CASE(InterruptibleSleep_AbortSwitchIsTriggered)
    {
        AbortSwitch abort_switch;
        abort_switch.abort();
        sleep(1000 * 3600, abort_switch);
    }

#ifdef EXPLORATION_TESTS

    TEST_CASE(Sleep_CheckElapsedTime)
    {
        Stopwatch<DefaultWallclockTimer> stopwatch;
        stopwatch.start();

        sleep(1000);

        stopwatch.measure();
        const double elapsed = stopwatch.get_seconds();
    }

    TEST_CASE(InterruptibleSleep_CheckElapsedTime)
    {
        AbortSwitch abort_switch;

        Stopwatch<DefaultWallclockTimer> stopwatch;
        stopwatch.start();

        sleep(1000, abort_switch);

        stopwatch.measure();
        const double elapsed = stopwatch.get_seconds();
    }

#endif
}
