
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
#include "foundation/core/exceptions/exception.h"
#include "foundation/math/rng/distribution.h"
#include "foundation/math/rng/lcg.h"
#include "foundation/utility/cache.h"
#include "foundation/utility/test.h"

// Standard headers.
#include <cassert>
#include <cstddef>
#include <memory>
#include <vector>

using namespace foundation;

namespace
{
    typedef size_t Key;
    typedef size_t Element;

    const Key InvalidKey = ~Key(0);

    struct KeyHasher
    {
        size_t operator()(const Key key) const
        {
            return static_cast<size_t>(key);
        }
    };

    struct ElementSwapperCountingUnloads
    {
        size_t m_unload_count;

        ElementSwapperCountingUnloads()
          : m_unload_count(0)
        {
        }

        void load(const Key key, Element& element) const
        {
        }

        bool unload(const Key key, Element& element)
        {
            ++m_unload_count;
            return true;
        }

        bool is_full(const size_t element_count) const
        {
            return false;
        }
    };
}

TEST_SUITE(Foundation_Utility_Cache_SACache)
{
    TEST_CASE(Destructor_UnloadsElementsStillInCache)
    {
        ElementSwapperCountingUnloads element_swapper;

        {
            KeyHasher key_hasher;
            SACache<Key, KeyHasher, Element, ElementSwapperCountingUnloads, 4, 1> cache(
                key_hasher,
                element_swapper,
                InvalidKey);

            cache.get(1);
            cache.get(2);
            cache.get(3);
        }

        EXPECT_EQ(3, element_swapper.m_unload_count);
    }

    TEST_CASE(Get_DoesNotCallUnloadOnEmptyCacheLine)
    {
        KeyHasher key_hasher;
        ElementSwapperCountingUnloads element_swapper;
        SACache<Key, KeyHasher, Element, ElementSwapperCountingUnloads, 4, 1> cache(
            key_hasher,
            element_swapper,
            InvalidKey);

        cache.get(0);

        EXPECT_EQ(0, element_swapper.m_unload_count);
    }
}

TEST_SUITE(Foundation_Utility_Cache_LRUCache)
{
    struct CollidingKeyHasher
    {
        size_t operator()(const Key key) const
        {
            return 0;
        }
    };

    struct TrackingSwapper
    {
        const size_t        m_capacity;
        Key                 m_pinned_key;
        std::vector<Key>    m_loads;
        std::vector<Key>    m_unload_attempts;

        explicit TrackingSwapper(const size_t capacity)
          : m_capacity(capacity)
          , m_pinned_key(InvalidKey)
        {
        }

        void load(const Key key, Element& element)
        {
            m_loads.push_back(key);
            element = key;
        }

        bool unload(const Key key, Element& element)
        {
            m_unload_attempts.push_back(key);
            return key != m_pinned_key;
        }

        bool is_full(const size_t element_count) const
        {
            return element_count > m_capacity;
        }
    };

    struct AllocationLedger
    {
        size_t m_allocation_count = 0;
        size_t m_live_bytes = 0;
    };

    template <typename T>
    struct TrackingAllocator
    {
        typedef T value_type;

        template <typename U>
        struct rebind
        {
            typedef TrackingAllocator<U> other;
        };

        AllocationLedger* m_ledger;

        explicit TrackingAllocator(AllocationLedger& ledger)
          : m_ledger(&ledger)
        {
        }

        template <typename U>
        TrackingAllocator(const TrackingAllocator<U>& other)
          : m_ledger(other.m_ledger)
        {
        }

        T* allocate(const size_t count)
        {
            T* memory = std::allocator<T>().allocate(count);
            ++m_ledger->m_allocation_count;
            m_ledger->m_live_bytes += count * sizeof(T);
            return memory;
        }

        void deallocate(T* memory, const size_t count) noexcept
        {
            m_ledger->m_live_bytes -= count * sizeof(T);
            std::allocator<T>().deallocate(memory, count);
        }

        template <typename U>
        bool operator==(const TrackingAllocator<U>& other) const
        {
            return m_ledger == other.m_ledger;
        }

        template <typename U>
        bool operator!=(const TrackingAllocator<U>& other) const
        {
            return !(*this == other);
        }
    };

    TEST_CASE(Get_WithCollidingKeys_EvictsLeastRecentlyUsedElement)
    {
        CollidingKeyHasher hasher;
        TrackingSwapper swapper(2);
        LRUCache<Key, CollidingKeyHasher, Element, TrackingSwapper> cache(hasher, swapper);

        EXPECT_EQ(1, cache.get(1));
        EXPECT_EQ(2, cache.get(2));
        EXPECT_EQ(1, cache.get(1));
        EXPECT_EQ(3, cache.get(3));
        ASSERT_EQ(1, swapper.m_unload_attempts.size());
        EXPECT_EQ(2, swapper.m_unload_attempts[0]);

        EXPECT_EQ(1, cache.get(1));
        EXPECT_EQ(2, cache.get(2));
        ASSERT_EQ(2, swapper.m_unload_attempts.size());
        EXPECT_EQ(3, swapper.m_unload_attempts[1]);
        EXPECT_EQ(4, swapper.m_loads.size());
        EXPECT_EQ(2, cache.get_hit_count());
        EXPECT_EQ(4, cache.get_miss_count());
    }

    TEST_CASE(Get_WhenOldestElementCannotUnload_EvictsNextOldest)
    {
        CollidingKeyHasher hasher;
        TrackingSwapper swapper(2);
        swapper.m_pinned_key = 1;
        LRUCache<Key, CollidingKeyHasher, Element, TrackingSwapper> cache(hasher, swapper);

        cache.get(1);
        cache.get(2);
        cache.get(3);

        // Unpin before any assertions can leave scope and destroy the cache.
        swapper.m_pinned_key = InvalidKey;
        ASSERT_EQ(2, swapper.m_unload_attempts.size());
        EXPECT_EQ(1, swapper.m_unload_attempts[0]);
        EXPECT_EQ(2, swapper.m_unload_attempts[1]);

        std::vector<Key> keys;
        auto checker = [&](const Key key, const Element element)
        {
            EXPECT_EQ(key, element);
            keys.push_back(key);
        };
        cache.check_integrity(checker);
        ASSERT_EQ(2, keys.size());
        EXPECT_EQ(3, keys[0]);
        EXPECT_EQ(1, keys[1]);

        cache.get(4);
        ASSERT_EQ(3, swapper.m_unload_attempts.size());
        EXPECT_EQ(1, swapper.m_unload_attempts[2]);
    }

    TEST_CASE(Get_AcrossIndexGrowth_PreservesElementsAndAllocatorState)
    {
        const size_t ElementCount = 128;
        AllocationLedger ledger;
        CollidingKeyHasher hasher;
        TrackingSwapper swapper(ElementCount);

        {
            TrackingAllocator<void> allocator(ledger);
            LRUCache<Key, CollidingKeyHasher, Element, TrackingSwapper,
                TrackingAllocator<void>> cache(hasher, swapper, allocator);

            Element* first = &cache.get(1);
            for (Key key = 2; key <= ElementCount; ++key)
                EXPECT_EQ(key, cache.get(key));

            // List storage must remain stable while the hash index grows.
            EXPECT_EQ(first, &cache.get(1));
            EXPECT_EQ(1, *first);
            for (Key key = 1; key <= ElementCount; ++key)
                EXPECT_EQ(key, cache.get(key));
            EXPECT_EQ(ElementCount, swapper.m_loads.size());
            EXPECT_EQ(0, swapper.m_unload_attempts.size());
            EXPECT_TRUE(ledger.m_live_bytes > 0);
        }

        EXPECT_EQ(ElementCount, swapper.m_unload_attempts.size());
        EXPECT_TRUE(ledger.m_allocation_count > 0);
        EXPECT_EQ(0, ledger.m_live_bytes);
    }

    TEST_CASE(Clear_RemovesIndexEntriesAndAllowsReload)
    {
        CollidingKeyHasher hasher;
        TrackingSwapper swapper(2);
        LRUCache<Key, CollidingKeyHasher, Element, TrackingSwapper> cache(hasher, swapper);

        cache.get(1);
        cache.get(2);
        cache.clear();
        EXPECT_EQ(2, swapper.m_unload_attempts.size());
        EXPECT_EQ(1, cache.get(1));
        EXPECT_EQ(3, swapper.m_loads.size());
        EXPECT_EQ(3, cache.get_miss_count());
    }

    TEST_CASE(Destructor_UnloadsElementsStillInCache)
    {
        ElementSwapperCountingUnloads element_swapper;

        {
            KeyHasher key_hasher;
            LRUCache<Key, KeyHasher, Element, ElementSwapperCountingUnloads> cache(key_hasher, element_swapper);

            cache.get(1);
            cache.get(2);
            cache.get(3);
        }

        EXPECT_EQ(3, element_swapper.m_unload_count);
    }

    struct ElementSwapperTrackingSize
    {
        size_t m_memory_size;

        ElementSwapperTrackingSize()
          : m_memory_size(0)
        {
        }

        void load(const Key key, Element& element)
        {
            m_memory_size += key * 1000;
        }

        bool unload(const Key key, Element& element)
        {
            m_memory_size -= key * 1000;
            return true;
        }

        bool is_full(const size_t element_count) const
        {
            return m_memory_size >= 8 * 1000;
        }
    };

    TEST_CASE(MemoryLimitIsHonored)
    {
        KeyHasher key_hasher;
        ElementSwapperTrackingSize element_swapper;
        LRUCache<Key, KeyHasher, Element, ElementSwapperTrackingSize> cache(key_hasher, element_swapper);

        cache.get(1);
        ASSERT_EQ(1000, element_swapper.m_memory_size);

        cache.get(2);
        ASSERT_EQ(3000, element_swapper.m_memory_size);

        cache.get(3);
        ASSERT_EQ(6000, element_swapper.m_memory_size);

        cache.get(4);   // flushes 1 and 2, cache contains 3 and 4
        ASSERT_EQ(7000, element_swapper.m_memory_size);

        cache.get(5);   // flushes 3 and 4, cache contains 5
        ASSERT_EQ(5000, element_swapper.m_memory_size);

        cache.get(6);   // flushes 5, cache contains 6
        ASSERT_EQ(6000, element_swapper.m_memory_size);

        cache.get(9);   // flushes 6, cache contains 9
        ASSERT_EQ(9000, element_swapper.m_memory_size);
    }
}

TEST_SUITE(Foundation_Utility_Cache_DualStageCache)
{
    struct ElementSwapper
    {
        const size_t m_cache_size;

        explicit ElementSwapper(const size_t cache_size)
          : m_cache_size(cache_size)
        {
        }

        void load(const Key key, Element& element) const
        {
            element = static_cast<Element>(key * 10);
        }

        bool unload(const Key key, Element& element) const
        {
            element = 0;
            return true;
        }

        bool is_full(const size_t element_count) const
        {
            assert(element_count <= m_cache_size);
            return element_count == m_cache_size;
        }
    };

    struct IntegrityChecker
    {
        void operator()(const Key key, const Element element) const
        {
            if (key != InvalidKey)
            {
                const Element expected = 10 * key;

                if (expected != element)
                    throw Exception("cache integrity check failed");
            }
        }
    };

    TEST_CASE(StressTest)
    {
        const size_t Stage0LineCount = 128;
        const size_t Stage0WayCount = 1;
        const size_t Stage1LineCount = 4;

        KeyHasher key_hasher;
        ElementSwapper element_swapper(Stage1LineCount);

        DualStageCache<
            Key,
            KeyHasher,
            Element,
            ElementSwapper,
            Stage0LineCount,
            Stage0WayCount> cache(key_hasher, element_swapper, InvalidKey);

        IntegrityChecker checker;
        cache.check_integrity(checker);

        LCG rng;

        for (size_t i = 0; i < 1000; ++i)
        {
            const Key key = rand_int1(rng, 1, 1000);
            const Element expected = key * 10;

            const Element value = cache.get(key);
            EXPECT_EQ(expected, value);

            cache.check_integrity(checker);
        }
    }
}
