
//
// This source file is part of appleseed.
// Visit https://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2019 Kevin Masson, The appleseedhq Organization
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
#include "foundation/math/ray.h"
#include "foundation/math/scalar.h"
#include "foundation/math/vector.h"

// Standard headers.
#include <cmath>

namespace foundation
{

//
// 3D ray-disk intersection functions.
//

// Test the intersection between a ray segment and the surface of a disk.
// The disk is assumed to be centered at the origin and pointing to the Y direction.
template <typename T>
bool intersect_disk(
    const Ray<T, 3>&        ray,
    const T                 radius);

// Test the intersection between a ray segment and the surface of a disk.
// The disk is assumed to be centered at the origin and pointing to the Y direction.
// If the ray segment and the disk intersect, the distance to the closest
// intersection is returned in `tmin` and the parametric representation of the surface
// hit is returned in `u` and `v`. Otherwise `tmin`, `u` and `v` are left unchanged.
template <typename T>
bool intersect_disk(
    const Ray<T, 3>&        ray,
    const T                 radius,
    T&                      tmin,
    T&                      u,
    T&                      v);


//
// 3D ray-disk intersection functions implementation.
//

// Polar coordinates shared by traced and sampled disk surfaces.
template <typename T>
inline Vector<T, 2> disk_surface_uv(const Vector<T, 3>& point, const T radius)
{
    T phi = std::atan2(point.x, point.z);
    if (phi < T(0.0))
        phi += TwoPi<T>();
    return Vector<T, 2>(phi * RcpTwoPi<T>(),
        T(1.0) - std::sqrt(square(point.x) + square(point.z)) / radius);
}

// Return only the distance; leave it unchanged on a miss. Affine transforms
// need not preserve the length of the ray direction.
template <typename T>
inline bool intersect_disk(const Ray<T, 3>& ray, const T radius, T& t)
{
    if (ray.m_dir.y == T(0.0))
        return false;
    const T distance = -ray.m_org.y / ray.m_dir.y;
    if (distance < ray.m_tmin || distance >= ray.m_tmax)
        return false;
    const Vector<T, 3> point = ray.point_at(distance);
    if (square(point.x) + square(point.z) > square(radius))
        return false;
    t = distance;
    return true;
}

template <typename T>
inline bool intersect_disk(const Ray<T, 3>& ray, const T radius)
{
    T distance;
    return intersect_disk(ray, radius, distance);
}

template <typename T>
inline bool intersect_disk(
    const Ray<T, 3>&        ray,
    const T                 radius,
    T&                      t,
    T&                      u,
    T&                      v)
{
    if (!intersect_disk(ray, radius, t))
        return false;
    const Vector<T, 2> uv = disk_surface_uv(ray.point_at(t), radius);
    u = uv[0];
    v = uv[1];
    return true;
}

}   // namespace foundation
