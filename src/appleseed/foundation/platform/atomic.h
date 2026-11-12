
//
// This source file is part of appleseed.
// Visit https://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2016-2018 Francois Beaune, The appleseedhq Organization
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

#pragma once

// appleseed.foundation headers.
#include "foundation/platform/compiler.h"
#ifdef _WIN32
#include "foundation/platform/windows.h"
#endif
#include "foundation/utility/casts.h"
#include "foundation/memory/memory.h"

// Standard headers.
#include <cassert>
#include <cstdint>

namespace foundation
{

//
// Atomic operations on native types.
//

std::uint32_t atomic_read(
    volatile std::uint32_t*     ptr);

void atomic_write(
    volatile std::uint32_t*     ptr,
    const std::uint32_t         value);

// Increment and decrement return the value before the update.
std::uint32_t atomic_inc(
    volatile std::uint32_t*     ptr);

std::uint32_t atomic_dec(
    volatile std::uint32_t*     ptr);

// Return the observed value, whether or not the exchange succeeds.
std::uint32_t atomic_cas(
    volatile std::uint32_t*     ptr,
    const std::uint32_t         expected_value,
    const std::uint32_t         new_value);

void atomic_add(
    volatile float*             ptr,
    const float                 operand);


//
// Implementation.
//

#ifdef _WIN32
namespace atomic_impl
{
    APPLESEED_FORCE_INLINE volatile LONG* native_pointer(volatile std::uint32_t* ptr)
    {
        static_assert(sizeof(LONG) == sizeof(std::uint32_t), "Interlocked requires 32-bit storage");
        return reinterpret_cast<volatile LONG*>(ptr);
    }
}
#endif

APPLESEED_FORCE_INLINE std::uint32_t atomic_read(
    volatile std::uint32_t*     ptr)
{
#ifdef _WIN32
    return static_cast<std::uint32_t>(InterlockedCompareExchange(atomic_impl::native_pointer(ptr), 0, 0));
#elif defined __GNUC__
    return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
#else
    #error Unsupported platform.
#endif
}

APPLESEED_FORCE_INLINE void atomic_write(
    volatile std::uint32_t*     ptr,
    const std::uint32_t         value)
{
#ifdef _WIN32
    InterlockedExchange(atomic_impl::native_pointer(ptr), binary_cast<LONG>(value));
#elif defined __GNUC__
    __atomic_store_n(ptr, value, __ATOMIC_SEQ_CST);
#else
    #error Unsupported platform.
#endif
}

APPLESEED_FORCE_INLINE std::uint32_t atomic_inc(
    volatile std::uint32_t*     ptr)
{
#ifdef _WIN32
    return static_cast<std::uint32_t>(InterlockedExchangeAdd(atomic_impl::native_pointer(ptr), 1));
#elif defined __GNUC__
    return __atomic_fetch_add(ptr, 1, __ATOMIC_SEQ_CST);
#else
    #error Unsupported platform.
#endif
}

APPLESEED_FORCE_INLINE std::uint32_t atomic_dec(
    volatile std::uint32_t*     ptr)
{
#ifdef _WIN32
    return static_cast<std::uint32_t>(InterlockedExchangeAdd(atomic_impl::native_pointer(ptr), -1));
#elif defined __GNUC__
    return __atomic_fetch_sub(ptr, 1, __ATOMIC_SEQ_CST);
#else
    #error Unsupported platform.
#endif
}

APPLESEED_FORCE_INLINE std::uint32_t atomic_cas(
    volatile std::uint32_t*     ptr,
    const std::uint32_t         expected_value,
    const std::uint32_t         new_value)
{
#ifdef _WIN32
    return static_cast<std::uint32_t>(InterlockedCompareExchange(
        atomic_impl::native_pointer(ptr), binary_cast<LONG>(new_value), binary_cast<LONG>(expected_value)));
#elif defined __GNUC__
    std::uint32_t observed = expected_value;
    __atomic_compare_exchange_n(ptr, &observed, new_value, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return observed;
#else
    #error Unsupported platform.
#endif
}

APPLESEED_FORCE_INLINE void atomic_add(
    volatile float*             ptr,
    const float                 operand)
{
    assert(is_aligned(ptr, alignof(std::uint32_t)));

#ifdef _WIN32
    // Interlocked operates on the representation of existing pixel storage.
    volatile std::uint32_t* iptr = reinterpret_cast<volatile std::uint32_t*>(ptr);
    std::uint32_t expected = atomic_read(iptr);
    while (true)
    {
        const float value = binary_cast<float>(expected);
        const std::uint32_t new_value = binary_cast<std::uint32_t>(value + operand);
        const std::uint32_t actual = atomic_cas(iptr, expected, new_value);
        if (actual == expected)
            return;
        expected = actual;
    }
#elif defined __GNUC__
    // Generic builtins operate on floats without accessing them through an
    // integer alias, including for the initial load under contention.
    float expected;
    __atomic_load(ptr, &expected, __ATOMIC_SEQ_CST);
    while (true)
    {
        float desired = expected + operand;
        if (__atomic_compare_exchange(ptr, &expected, &desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
            return;
    }
#else
    #error Unsupported platform.
#endif
}

}   // namespace foundation
