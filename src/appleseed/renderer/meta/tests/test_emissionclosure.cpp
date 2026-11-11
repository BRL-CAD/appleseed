
//
// This source file is part of appleseed.
// Visit https://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2026 The appleseedhq Organization
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
#include "renderer/kernel/shading/closures.h"

// appleseed.foundation headers.
#include "foundation/memory/arena.h"
#include "foundation/utility/test.h"

// OSL headers.
#include "foundation/platform/_beginoslheaders.h"
#include "OSL/oslclosure.h"
#include "foundation/platform/_endoslheaders.h"

// Standard headers.
#include <cmath>
#include <cstring>
#include <new>

using namespace foundation;
using namespace renderer;

TEST_SUITE(Renderer_Kernel_Shading_EmissionClosure)
{
    TEST_CASE(WeightedEmissionClosuresHaveNormalizedSamplingProbabilities)
    {
        OSL::ClosureComponent red = {};
        red.id = EmissionID;
        red.w = OSL::Vec3(0.25f, 0.0f, 0.0f);
        OSL::ClosureComponent green = {};
        green.id = EmissionID;
        green.w = OSL::Vec3(0.0f, 0.75f, 0.0f);
        OSL::ClosureAdd sum = {};
        sum.id = OSL::ClosureColor::ADD;
        sum.closureA = &red;
        sum.closureB = &green;
        Arena arena;

        // Fresh arena memory must not influence closure selection.  Poison the
        // object storage to expose reads from fields its constructor omits.
        alignas(CompositeEmissionClosure) unsigned char storage[sizeof(CompositeEmissionClosure)];
        std::memset(storage, 0xff, sizeof(storage));
        auto* closure = new (storage) CompositeEmissionClosure(Basis3f(Vector3f(0.0f, 1.0f, 0.0f)), &sum, arena);
        EXPECT_EQ(2, closure->get_closure_count());
        const float red_pdf = closure->get_closure_pdf(0);
        const float green_pdf = closure->get_closure_pdf(1);
        EXPECT_TRUE(std::isfinite(red_pdf));
        EXPECT_TRUE(std::isfinite(green_pdf));
        EXPECT_FEQ(0.25f, red_pdf);
        EXPECT_FEQ(0.75f, green_pdf);
        if (std::isfinite(red_pdf) && std::isfinite(green_pdf))
        {
            EXPECT_EQ(0, closure->choose_closure(0.125f));
            EXPECT_EQ(1, closure->choose_closure(0.5f));
            EXPECT_EQ(1, closure->choose_closure(0.875f));
        }
        closure->~CompositeEmissionClosure();
    }

    TEST_CASE(SingleEmissionClosureHasUnitProbability)
    {
        OSL::ClosureComponent component = {};
        component.id = EmissionID;
        component.w = OSL::Vec3(0.2f, 0.4f, 0.6f);
        Arena arena;
        CompositeEmissionClosure closure(Basis3f(Vector3f(0.0f, 1.0f, 0.0f)), &component, arena);
        EXPECT_EQ(1, closure.get_closure_count());
        EXPECT_TRUE(std::isfinite(closure.get_closure_pdf(0)));
        EXPECT_FEQ(1.0f, closure.get_closure_pdf(0));
    }

    TEST_CASE(EmptyAndZeroWeightEmissionHaveNoSampledClosures)
    {
        OSL::ClosureComponent component = {};
        component.id = EmissionID;
        component.w = OSL::Vec3(0.0f);
        Arena arena;
        const Basis3f basis(Vector3f(0.0f, 1.0f, 0.0f));
        CompositeEmissionClosure zero(basis, &component, arena);
        CompositeEmissionClosure empty(basis, nullptr, arena);
        EXPECT_EQ(0, zero.get_closure_count());
        EXPECT_EQ(0, empty.get_closure_count());
    }
}
