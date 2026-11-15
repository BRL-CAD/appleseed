
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

#pragma once

#include "foundation/math/basis.h"
#include "foundation/math/vector.h"

#include <cassert>
#include <cmath>

namespace foundation
{

// Differentiate normalization of a nonzero vector, retaining its computed
// unit value so callers follow the same value path.
template <typename T, size_t N>
inline Vector<T, N> differentiate_normalize(
    const Vector<T, N>& raw,
    const Vector<T, N>& unit,
    const Vector<T, N>& derivative)
{
    const T length = norm(raw);
    assert(length > T(0.0));
    return (derivative - unit * dot(unit, derivative)) / length;
}

// Differentiate Basis3(normal, tangent). The reference tangent must not be
// collinear with the unit normal; its normal component must be retained.
template <typename T>
inline void differentiate_basis(
    const Basis3<T>& basis,
    const Vector<T, 3>& tangent,
    const Vector<T, 3>& normal_derivative,
    const Vector<T, 3>& tangent_derivative,
    Vector<T, 3>& du,
    Vector<T, 3>& dv)
{
    const auto& n = basis.get_normal();
    const auto& v = basis.get_tangent_v();
    dv = differentiate_normalize(cross(tangent, n), v,
        cross(tangent_derivative, n) + cross(tangent, normal_derivative));
    du = cross(normal_derivative, v) + cross(n, dv);
}

// Differentiate Basis3(normal)'s signed Duff construction within its current
// hemisphere branch, including the arbitrary-frame fallback used by shading.
template <typename T>
inline void differentiate_basis(
    const Vector<T, 3>& normal,
    const Vector<T, 3>& derivative,
    Vector<T, 3>& du,
    Vector<T, 3>& dv)
{
    const T sign = std::copysign(T(1.0), normal.z);
    const T a = T(-1.0) / (sign + normal.z);
    const T da = a * a * derivative.z;
    const T db = (derivative.x * normal.y + normal.x * derivative.y) * a
        + normal.x * normal.y * da;
    du = Vector<T, 3>(db,
        T(2.0) * normal.y * derivative.y * a + normal.y * normal.y * da,
        -derivative.y);
    dv = Vector<T, 3>(sign * (T(2.0) * normal.x * derivative.x * a + normal.x * normal.x * da),
        sign * db, -sign * derivative.x);
}

}   // namespace foundation
