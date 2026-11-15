
//
// This source file is part of appleseed.
// Visit https://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2015-2018 Esteban Tovagliari, The appleseedhq Organization
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

// Interface header.
#include "bsdfsample.h"

// appleseed.foundation headers.
#include "foundation/math/differentials.h"
#include "foundation/math/basis.h"

// Standard headers.
#include <cmath>

using namespace foundation;

namespace renderer
{

namespace
{
    void get_normal_derivatives(
        const BSDF::LocalGeometry& geometry, Vector3f& dx, Vector3f& dy)
    {
        if (geometry.m_normal_derivatives)
        {
            dx = geometry.m_normal_derivatives->m_dx;
            dy = geometry.m_normal_derivatives->m_dy;
        }
        else geometry.m_shading_point->compute_screen_space_normal_derivatives(dx, dy);
    }

    Matrix3f basis_derivative(
        const BSDF::LocalGeometry& geometry, const Vector3f& dn, const Vector3f& dt)
    {
        const Basis3f& basis = geometry.m_shading_basis;
        const Vector3f& tangent = geometry.m_basis_reference ?
            geometry.m_basis_reference->m_tangent : basis.get_tangent_u();
        Vector3f du, dv;
        differentiate_basis(basis, tangent, dn, dt, du, dv);
        Matrix3f derivative;
        for (std::size_t row = 0; row < 3; ++row)
        {
            derivative(row, 0) = du[row];
            derivative(row, 1) = dn[row];
            derivative(row, 2) = dv[row];
        }
        return derivative;
    }

    Vector3f local_direction_derivative(
        const Basis3f& basis, const Matrix3f& derivative,
        const Vector3f& world_direction, const Vector3f& world_derivative)
    {
        return basis.transform_to_local(world_derivative) + transpose(derivative) * world_direction;
    }

    struct DirectionDerivative
    {
        Vector3f value;
        Vector3f derivative;
    };

    template <typename DirectionFunction>
    void propagate_microfacet_differentials(
        const BSDF::LocalGeometry& geometry,
        const Dual3f& outgoing,
        const Matrix3f& normal_jacobian,
        Dual3f& incoming,
        const DirectionFunction& direction)
    {
        const Basis3f& basis = geometry.m_shading_basis;
        Vector3f dn[2];
        get_normal_derivatives(geometry, dn[0], dn[1]);
        Vector3f surface_dx(0.0f), surface_dy(0.0f);
        if (!geometry.m_basis_reference && geometry.m_shading_point)
        {
            Vector3f vx, vy;
            geometry.m_shading_point->compute_screen_space_basis_derivatives(surface_dx, surface_dy, vx, vy);
        }
        const Vector3f outgoing_neighbors[] = {outgoing.get_dx(), outgoing.get_dy()};
        Vector3f incoming_neighbors[2];
        for (std::size_t axis = 0; axis < 2; ++axis)
        {
            const Vector3f dt = geometry.m_basis_reference ?
                (axis == 0 ? geometry.m_basis_reference->m_dx : geometry.m_basis_reference->m_dy) :
                (axis == 0 ? surface_dx : surface_dy);
            const Matrix3f db = basis_derivative(geometry, dn[axis], dt);
            const Vector3f dwo = local_direction_derivative(basis, db, outgoing.get_value(),
                outgoing_neighbors[axis] - outgoing.get_value());
            const Vector3f dm = normal_jacobian * dwo;
            const DirectionDerivative local = direction(db, dwo, dm, axis);
            incoming_neighbors[axis] = incoming.get_value()
                + basis.transform_to_parent(local.derivative) + db * local.value;
        }
        incoming.set_derivatives(incoming_neighbors[0], incoming_neighbors[1]);
    }

    void compute_normal_derivatives(
        const BSDF::LocalGeometry&  local_geometry,
        const Vector3f&             o,
        const Vector3f&             dodx,
        const Vector3f&             dody,
        foundation::Vector3f&       dndx,
        foundation::Vector3f&       dndy,
        float&                      ddndx,
        float&                      ddndy)
    {
        //
        // Reference:
        //
        //   Physically Based Rendering, first edition, page 513.
        //

        get_normal_derivatives(local_geometry, dndx, dndy);

        const Vector3f& n = local_geometry.m_shading_basis.get_normal();

        ddndx = dot(dodx, n) + dot(o, dndx);
        ddndy = dot(dody, n) + dot(o, dndy);
    }
}


//
// BSDFSample class implementation.
//

void BSDFSample::compute_specular_reflected_differentials(
    const BSDF::LocalGeometry&  local_geometry,
    const Dual3f&               outgoing)
{
    if (outgoing.has_derivatives())
    {
        //
        // Reference:
        //
        //   Physically Based Rendering, first edition, page 513.
        //

        const Vector3f dodx = outgoing.get_dx() - outgoing.get_value();
        const Vector3f dody = outgoing.get_dy() - outgoing.get_value();

        Vector3f dndx, dndy;
        float ddndx, ddndy;
        compute_normal_derivatives(
            local_geometry,
            outgoing.get_value(),
            dodx,
            dody,
            dndx,
            dndy,
            ddndx,
            ddndy);

        const Vector3f& n = local_geometry.m_shading_basis.get_normal();
        const float dot_on = dot(outgoing.get_value(), n);

        const Vector3f& i = m_incoming.get_value();
        m_incoming.set_derivatives(
            i - dodx + 2.0f * Vector3f(dot_on * dndx + ddndx * n),
            i - dody + 2.0f * Vector3f(dot_on * dndy + ddndy * n));
    }
}

void BSDFSample::compute_specular_transmitted_differentials(
    const BSDF::LocalGeometry&  local_geometry,
    const float                 eta,
    const bool                  is_entering,
    const Dual3f&               outgoing)
{
    if (outgoing.has_derivatives())
    {
        const Vector3f dodx = outgoing.get_dx() - outgoing.get_value();
        const Vector3f dody = outgoing.get_dy() - outgoing.get_value();

        Vector3f dndx, dndy;
        float ddndx, ddndy;
        compute_normal_derivatives(
            local_geometry,
            outgoing.get_value(),
            dodx,
            dody,
            dndx,
            dndy,
            ddndx,
            ddndy);

        const Vector3f& n = local_geometry.m_shading_basis.get_normal();
        const float dot_on = dot(outgoing.get_value(), n);
        const float dot_in = std::abs(dot(m_incoming.get_value(), n));
        const float mu = eta * dot_on - dot_in;

        const float a = eta - (square(eta) * dot_on) / dot_in;
        const float dmudx = a * ddndx;
        const float dmudy = a * ddndy;

        const Vector3f& i = m_incoming.get_value();
        m_incoming.set_derivatives(
            i - eta * dodx + Vector3f(mu * dndx + dmudx * n),
            i - eta * dody + Vector3f(mu * dndy + dmudy * n));
    }
}

void BSDFSample::compute_glossy_reflected_differentials(
    const BSDF::LocalGeometry&  local_geometry,
    const float                 roughness,
    const Dual3f&               outgoing)
{
    // TODO: scale differentials based on roughness.
    compute_specular_reflected_differentials(local_geometry, outgoing);
}

void BSDFSample::compute_glossy_transmitted_differentials(
    const BSDF::LocalGeometry&  local_geometry,
    const float                 eta,
    const float                 roughness,
    const bool                  is_entering,
    const Dual3f&               outgoing)
{
    // TODO: scale differentials based on roughness.
    compute_specular_transmitted_differentials(
        local_geometry,
        eta,
        is_entering,
        outgoing);
}

void BSDFSample::compute_microfacet_reflected_differentials(
    const BSDF::LocalGeometry& geometry,
    const Dual3f& outgoing,
    const Vector3f& m,
    const Matrix3f& normal_jacobian,
    const MicrofacetReflectionMode mode)
{
    if (!outgoing.has_derivatives())
        return;

    const Basis3f& basis = geometry.m_shading_basis;
    const Vector3f wo = basis.transform_to_local(outgoing.get_value());
    const Vector3f raw_wi = reflect(wo, m);
    const Vector3f ng = basis.transform_to_local(geometry.m_geometric_normal);
    const float correction = BSDF::MinGeometricCosine - dot(raw_wi, ng);
    Vector3f geometric_dx(0.0f), geometric_dy(0.0f);
    if (geometry.m_geometric_normal_derivatives)
    {
        geometric_dx = geometry.m_geometric_normal_derivatives->m_dx;
        geometric_dy = geometry.m_geometric_normal_derivatives->m_dy;
    }
    else if (mode == MicrofacetReflectionMode::GeometricCorrection && correction > 0.0f &&
             geometry.m_shading_point)
        geometry.m_shading_point->compute_screen_space_geometric_normal_derivatives(geometric_dx, geometric_dy);
    propagate_microfacet_differentials(geometry, outgoing, normal_jacobian, m_incoming,
        [&](const Matrix3f& db, const Vector3f& dwo, const Vector3f& dm, const std::size_t axis)
        {
            const float cosine = dot(wo, m);
            const float dc = dot(dwo, m) + dot(wo, dm);
            Vector3f wi = raw_wi;
            Vector3f dwi = 2.0f * dc * m + 2.0f * cosine * dm - dwo;
            if (mode == MicrofacetReflectionMode::Renormalized)
            {
                const float factor = 1.5f - 0.5f * square_norm(raw_wi);
                dwi = factor * dwi - raw_wi * dot(raw_wi, dwi);
                wi = improve_normalization(raw_wi);
            }
            else if (mode == MicrofacetReflectionMode::GeometricCorrection && correction > 0.0f)
            {
                const Vector3f& dng_world = axis == 0 ? geometric_dx : geometric_dy;
                const Vector3f dng = local_direction_derivative(basis, db, geometry.m_geometric_normal, dng_world);
                const float dcorr = -dot(dwi, ng) - dot(raw_wi, dng);
                const Vector3f corrected = raw_wi + correction * ng;
                wi = normalize(corrected);
                dwi = differentiate_normalize(corrected, wi, dwi + dcorr * ng + correction * dng);
            }
            return DirectionDerivative{wi, dwi};
        });
}

void BSDFSample::compute_microfacet_transmitted_differentials(
    const BSDF::LocalGeometry& geometry,
    const Dual3f& outgoing,
    const Vector3f& m,
    const Matrix3f& normal_jacobian,
    const float eta)
{
    if (!outgoing.has_derivatives())
        return;

    const Basis3f& basis = geometry.m_shading_basis;
    const Vector3f wo = basis.transform_to_local(outgoing.get_value());
    const float cosine = dot(wo, m);
    const float transmitted_squared = 1.0f - eta * eta * (1.0f - cosine * cosine);
    // At the critical angle the direction derivative is singular. Keep
    // the sampled direction without manufacturing a finite footprint.
    if (transmitted_squared <= 0.0f)
        return;
    const float transmitted_cosine = std::sqrt(transmitted_squared);
    const float mu = eta * cosine - std::copysign(transmitted_cosine, cosine);
    const Vector3f raw_wi = mu * m - eta * wo;
    const Vector3f wi = improve_normalization(raw_wi);
    propagate_microfacet_differentials(geometry, outgoing, normal_jacobian, m_incoming,
        [&](const Matrix3f&, const Vector3f& dwo, const Vector3f& dm, std::size_t)
        {
            const float dc = dot(dwo, m) + dot(wo, dm);
            const float dmu = (eta - eta * eta * std::abs(cosine) / transmitted_cosine) * dc;
            const Vector3f raw_derivative = dmu * m + mu * dm - eta * dwo;
            const Vector3f dwi = (1.5f - 0.5f * square_norm(raw_wi)) * raw_derivative
                - raw_wi * dot(raw_wi, raw_derivative);
            return DirectionDerivative{wi, dwi};
        });
}

void BSDFSample::compute_diffuse_differentials(const Dual3f& outgoing)
{
    if (outgoing.has_derivatives())
    {
        // 1/25 of the hemisphere.
        // Reference: https://www.pbrt.org/texcache.pdf
        const auto& i = m_incoming.get_value();
        const Basis3f basis(m_incoming.get_value());
        m_incoming.set_derivatives(
            normalize(i + 0.2f * basis.get_tangent_u()),
            normalize(i + 0.2f * basis.get_tangent_v()));
    }
}

}   // namespace renderer
