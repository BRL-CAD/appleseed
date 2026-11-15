
//
// This source file is part of appleseed.
// Visit https://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2017-2018 Francois Beaune, The appleseedhq Organization
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

// appleseed.renderer headers.
#include "renderer/modeling/object/object.h"

// appleseed.foundation headers.
#include "foundation/math/ray.h"
#include "foundation/math/vector.h"

// appleseed.main headers.
#include "main/dllsymbol.h"

// Standard headers.
#include <cstddef>
#include <cstdint>

// Forward declarations.
namespace renderer  { class ParamArray; }
namespace renderer  { class ShadingRay; }

namespace renderer
{

//
// An object whose surface is defined procedurally.
//

class APPLESEED_DLLSYMBOL ProceduralObject
  : public Object
{
  public:
    // Object-space derivatives of position and the unit, unflipped shading
    // normal with respect to the supplied UV mapping.  Leave invalid when
    // the mapping is unavailable or singular; no arbitrary UV basis is implied.
    struct SurfaceDerivatives
    {
        bool                        m_valid = false;
        foundation::Vector3d        m_dpdu = foundation::Vector3d(0.0);
        foundation::Vector3d        m_dpdv = foundation::Vector3d(0.0);
        foundation::Vector3d        m_dndu = foundation::Vector3d(0.0);
        foundation::Vector3d        m_dndv = foundation::Vector3d(0.0);
        bool                        m_has_tangent_partials = false;
        foundation::Vector3d        m_d2pduu = foundation::Vector3d(0.0);
        foundation::Vector3d        m_d2pduv = foundation::Vector3d(0.0);
        bool                        m_has_geometric_normal_partials = false;
        foundation::Vector3d        m_dngdu = foundation::Vector3d(0.0);
        foundation::Vector3d        m_dngdv = foundation::Vector3d(0.0);
    };

    struct IntersectionResult
    {
        bool                        m_hit;
        double                      m_distance;
        foundation::Vector3d        m_geometric_normal;
        foundation::Vector3d        m_shading_normal;
        foundation::Vector2f        m_uv;
        std::uint32_t               m_material_slot;
        SurfaceDerivatives          m_derivatives;
    };

    static constexpr size_t SurfaceSampleDimensions = 5;

    struct SurfaceSample
    {
        foundation::Vector3d        m_point;
        IntersectionResult          m_attributes;
        double                      m_estimator_weight;
    };

    // Zero means this object does not provide a surface sampler. A positive,
    // finite K defines the reference area density 1/K; it is not the actual
    // surface area. Sampling must satisfy E[weight*f(point)]=integral(f*dA)/K,
    // including missed attempts as zero. Errors must throw rather than miss.
    virtual double get_surface_sampling_reference_area() const;

    // Supply five independent variates in [0,1). Return false for a null draw,
    // preserving the output. Successful attributes use the ordinary native
    // object-space mapping, unflipped normals and material slot zero.
    virtual bool sample_surface(
        const double                variates[SurfaceSampleDimensions],
        SurfaceSample&              sample) const;

    // Compute the intersection between a ray expressed in object space and
    // the surface of this object and return detailed intersection results.
    virtual void intersect(
        const ShadingRay&           ray,
        IntersectionResult&         result) const = 0;

    // Compute the intersection between a ray expressed in object space and
    // the surface of this object and simply return whether there was a hit.
    virtual bool intersect(
        const ShadingRay&           ray) const = 0;

    // Compute a front point, a back point and the geometric normal in object
    // instance space for a given ray with origin being a point on the surface
    // of the object. The supplied geometric normal is the unflipped normal
    // reported by intersect(), in object space. Return an offset normal facing
    // the incoming ray; front and back lie on its positive and negative sides.
    virtual void refine_and_offset(
        const foundation::Ray3d&    obj_inst_ray,
        const foundation::Vector3d& obj_inst_geometric_normal,
        foundation::Vector3d&       obj_inst_front_point,
        foundation::Vector3d&       obj_inst_back_point,
        foundation::Vector3d&       obj_inst_geo_normal) const = 0;

  protected:
    // Constructor.
    ProceduralObject(
        const char*                 name,
        const ParamArray&           params);
};

}   // namespace renderer
