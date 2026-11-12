
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

#pragma once

// Standard headers.
#include <cstddef>
#include <future>
#include <vector>

namespace foundation
{
namespace test_impl
{
    template <typename Function>
    void run_concurrently(const size_t worker_count, const Function& function)
    {
        std::vector<std::future<void>> tasks;
        tasks.reserve(worker_count);

        // Destroy the promise before the tasks if launching a worker throws.
        // Its broken state releases workers waiting at the start gate.
        std::promise<void> start;
        const std::shared_future<void> ready = start.get_future().share();
        for (size_t worker = 0; worker < worker_count; ++worker)
        {
            tasks.emplace_back(std::async(std::launch::async, [&, ready, worker]()
            {
                ready.wait();
                function(worker);
            }));
        }

        start.set_value();
        for (auto& task : tasks)
            task.get();
    }
}
}
