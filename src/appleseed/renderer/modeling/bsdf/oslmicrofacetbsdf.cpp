
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

#include "oslmicrofacetbsdf.h"

#include "renderer/kernel/shading/shadingpoint.h"
#include "renderer/modeling/bsdf/bsdf.h"
#include "renderer/modeling/bsdf/bsdfsample.h"
#include "renderer/modeling/bsdf/bsdfwrapper.h"
#include "renderer/modeling/bsdf/fresnel.h"
#include "renderer/modeling/bsdf/microfacethelper.h"
#include "renderer/modeling/bsdf/microfacettransmissionhelper.h"
#include "renderer/modeling/bsdf/specularhelper.h"
#include "renderer/utility/paramarray.h"
#include "foundation/math/microfacet.h"

#include <algorithm>
#include <cmath>

using namespace foundation;

namespace renderer
{
namespace
{
    template <typename Function>
    auto visit_distribution(const OSLMicrofacetDistribution distribution, const Function& function)
    {
        if (distribution == OSLMicrofacetDistribution::GGX)
            return function(GGXMDF());
        return function(BeckmannMDF());
    }

    class OSLMicrofacetBSDFImpl
      : public BSDF
    {
      public:
        OSLMicrofacetBSDFImpl(const char* name, const ParamArray& params)
          : BSDF(name, AllBSDFTypes, ScatteringMode::Glossy | ScatteringMode::Specular, params)
        {
        }

        void release() override
        {
            delete this;
        }

        const char* get_model() const override
        {
            return "osl_microfacet_bsdf";
        }

        size_t compute_input_data_size() const override
        {
            return sizeof(OSLMicrofacetBSDFInputValues);
        }

        void prepare_inputs(Arena& arena, const ShadingPoint& point, void* data) const override
        {
            auto& values = *static_cast<OSLMicrofacetBSDFInputValues*>(data);
            const float minimum_alpha = square(point.get_ray().m_min_roughness);
            const float maximum_alpha = std::max(values.m_alpha_x, values.m_alpha_y);
            if (maximum_alpha < minimum_alpha)
            {
                if (maximum_alpha > 0.0f)
                {
                    // Scale both axes together to preserve authored anisotropy.
                    // Double precision avoids overflow in a large width ratio.
                    const double scale = static_cast<double>(minimum_alpha) / maximum_alpha;
                    values.m_alpha_x = static_cast<float>(values.m_alpha_x * scale);
                    values.m_alpha_y = static_cast<float>(values.m_alpha_y * scale);
                }
                else
                {
                    values.m_alpha_x = minimum_alpha;
                    values.m_alpha_y = minimum_alpha;
                }
            }
        }

        bool has_transmission(const void* data) const override
        {
            return static_cast<const OSLMicrofacetBSDFInputValues*>(data)->m_mode != OSLMicrofacetMode::Reflection;
        }

        void sample(
            SamplingContext& sampling_context,
            const void* data,
            const bool adjoint,
            const bool cosine_mult,
            const LocalGeometry& geometry,
            const Dual3f& outgoing,
            const int modes,
            BSDFSample& sample) const override
        {
            const auto& values = *static_cast<const OSLMicrofacetBSDFInputValues*>(data);
            if (values.m_mode == OSLMicrofacetMode::Reflection)
            {
                sample_reflection(sampling_context, data, adjoint, cosine_mult, geometry, outgoing, modes, sample);
                return;
            }
            const Vector3f wo = geometry.m_shading_basis.transform_to_local(outgoing.get_value());
            if (wo.y <= 0.0f)
                return;
            if (values.m_eta == 1.0f)
            {
                // A matching interface is a straight-through delta even when
                // nonzero widths were authored. Its finite-density limit is singular.
                if (!ScatteringMode::has_specular(modes) ||
                    !matches_geometric_side(geometry, outgoing.get_value(), -outgoing.get_value(), false))
                    return;
                sample.m_incoming = Dual3f(-outgoing.get_value());
                if (outgoing.has_derivatives())
                    sample.m_incoming.set_derivatives(-outgoing.get_dx(), -outgoing.get_dy());
                sample.m_value.m_glossy = values.m_reflectance;
                if (!cosine_mult)
                    sample.m_value.m_glossy /= wo.y;
                sample.m_value.m_beauty = sample.m_value.m_glossy;
                sample.set_to_scattering(ScatteringMode::Specular, DiracDelta);
                return;
            }
            const bool smooth = is_smooth(values);
            const auto mode = smooth ? ScatteringMode::Specular : ScatteringMode::Glossy;
            if ((modes & mode) == 0)
                return;
            visit_distribution(values.m_distribution, [&](auto distribution)
            {
                using MDF = decltype(distribution);
                Matrix3f jacobian(0.0f);
                Vector3f m(0.0f, 1.0f, 0.0f);
                float selector = 0.0f;
                const auto sample_normal = [&](const Vector2f& random)
                {
                    return outgoing.has_derivatives() ?
                        MDF::sample(wo, random, values.m_alpha_x, values.m_alpha_y, jacobian) :
                        MDF::sample(wo, random, values.m_alpha_x, values.m_alpha_y);
                };
                if (!smooth)
                {
                    if (values.m_mode == OSLMicrofacetMode::Combined)
                    {
                        sampling_context.split_in_place(3, 1);
                        const Vector3f random = sampling_context.next2<Vector3f>();
                        m = sample_normal(Vector2f(random.x, random.y));
                        selector = random.z;
                    }
                    else
                    {
                        sampling_context.split_in_place(2, 1);
                        m = sample_normal(sampling_context.next2<Vector2f>());
                    }
                }
                else if (values.m_mode == OSLMicrofacetMode::Combined)
                {
                    sampling_context.split_in_place(1, 1);
                    selector = sampling_context.next2<float>();
                }
                const float cosine = clamp(dot(wo, m), 0.0f, 1.0f);
                float transmitted_cosine;
                const float F = MicrofacetTransmissionHelper<MDF>::fresnel(cosine, values.m_eta, transmitted_cosine);
                const bool reflection = values.m_mode == OSLMicrofacetMode::Combined && selector < F;
                if (!reflection && F == 1.0f)
                    return;
                const float ratio = 1.0f / values.m_eta;
                const Vector3f wi = reflection ? improve_normalization(reflect(wo, m)) :
                    improve_normalization((ratio * cosine - transmitted_cosine) * m - ratio * wo);
                if ((reflection && wi.y <= 0.0f) || (!reflection && wi.y >= 0.0f))
                    return;
                const Vector3f incoming = geometry.m_shading_basis.transform_to_parent(wi);
                if (!matches_geometric_side(geometry, outgoing.get_value(), incoming, reflection))
                    return;
                sample.m_incoming = Dual3f(incoming);
                if (smooth)
                {
                    const float weight = reflection ? 1.0f :
                        (values.m_mode == OSLMicrofacetMode::Combined ? 1.0f : 1.0f - F);
                    sample.m_value.m_glossy = values.m_reflectance;
                    sample.m_value.m_glossy *= weight * (!reflection && !adjoint ? square(ratio) : 1.0f);
                    if (!cosine_mult)
                        sample.m_value.m_glossy /= std::abs(wi.y);
                    sample.set_to_scattering(mode, DiracDelta);
                }
                else
                {
                    const float probability = evaluate(data, adjoint, cosine_mult, geometry,
                        outgoing.get_value(), incoming, modes, sample.m_value);
                    if (!(probability > 0.0f))
                        return;
                    sample.set_to_scattering(mode, probability);
                }
                sample.m_min_roughness = std::sqrt(std::max(values.m_alpha_x, values.m_alpha_y));
                sample.m_value.m_beauty = sample.m_value.m_glossy;
                if (reflection)
                    sample.compute_microfacet_reflected_differentials(geometry, outgoing, m, jacobian,
                        BSDFSample::MicrofacetReflectionMode::Renormalized);
                else
                    sample.compute_microfacet_transmitted_differentials(geometry, outgoing, m, jacobian, ratio);
            });
        }

        float evaluate(
            const void* data,
            const bool adjoint,
            const bool cosine_mult,
            const LocalGeometry& geometry,
            const Vector3f& outgoing,
            const Vector3f& incoming,
            const int modes,
            DirectShadingComponents& value) const override
        {
            const auto& values = *static_cast<const OSLMicrofacetBSDFInputValues*>(data);
            if (values.m_mode == OSLMicrofacetMode::Reflection)
                return evaluate_reflection(data, adjoint, cosine_mult, geometry, outgoing, incoming, modes, value);
            const float probability = evaluate_rough(values, adjoint, geometry, outgoing,
                incoming, modes, &value.m_glossy);
            value.m_beauty = value.m_glossy;
            return probability;
        }

        float evaluate_pdf(
            const void* data,
            const bool adjoint,
            const LocalGeometry& geometry,
            const Vector3f& outgoing,
            const Vector3f& incoming,
            const int modes) const override
        {
            const auto& values = *static_cast<const OSLMicrofacetBSDFInputValues*>(data);
            if (values.m_mode == OSLMicrofacetMode::Reflection)
                return evaluate_reflection_pdf(data, adjoint, geometry, outgoing, incoming, modes);
            return evaluate_rough(values, adjoint, geometry, outgoing, incoming, modes, nullptr);
        }

      private:
        float evaluate_rough(
            const OSLMicrofacetBSDFInputValues& values,
            const bool adjoint,
            const LocalGeometry& geometry,
            const Vector3f& outgoing,
            const Vector3f& incoming,
            const int modes,
            Spectrum* value) const
        {
            if (!ScatteringMode::has_glossy(modes) || is_smooth(values) || values.m_eta == 1.0f)
                return 0.0f;
            const Vector3f wo = geometry.m_shading_basis.transform_to_local(outgoing);
            const Vector3f wi = geometry.m_shading_basis.transform_to_local(incoming);
            if (wo.y <= 0.0f || wi.y == 0.0f)
                return 0.0f;
            const bool reflection = wi.y > 0.0f;
            if ((reflection && values.m_mode == OSLMicrofacetMode::Transmission) ||
                !matches_geometric_side(geometry, outgoing, incoming, reflection))
                return 0.0f;
            const float probability = visit_distribution(values.m_distribution, [&](auto distribution)
            {
                using MDF = decltype(distribution);
                using Transmission = MicrofacetTransmissionHelper<MDF>;
                if (reflection)
                {
                    const Vector3f half = normalize(wo + wi);
                    const float F = Transmission::fresnel(clamp(dot(wo, half), 0.0f, 1.0f), values.m_eta);
                    if (value)
                    {
                        const FresnelDielectricFun fun(values.m_reflectance, 1.0f, 1.0f / values.m_eta, 1.0f);
                        return F * MicrofacetBRDFHelper<MDF>::evaluate(values.m_alpha_x, values.m_alpha_y,
                            fun, geometry, outgoing, incoming, *value);
                    }
                    return F * MicrofacetBRDFHelper<MDF>::pdf(values.m_alpha_x, values.m_alpha_y,
                        geometry, outgoing, incoming);
                }
                const Vector3f half = Transmission::half_vector(wo, wi, values.m_eta);
                if (!Transmission::valid_facet(wo, wi, half))
                    return 0.0f;
                const float T = 1.0f - Transmission::fresnel(clamp(dot(wo, half), 0.0f, 1.0f), values.m_eta);
                if (!(T > 0.0f))
                    return 0.0f;
                if (value)
                    Transmission::evaluate(values.m_eta, values.m_reflectance, adjoint, wo, wi, half,
                        values.m_alpha_x, values.m_alpha_y, T, *value);
                return Transmission::pdf(wo, wi, half, values.m_alpha_x, values.m_alpha_y, values.m_eta) *
                    (values.m_mode == OSLMicrofacetMode::Combined ? T : 1.0f);
            });
            return probability;
        }

        void sample_reflection(
            SamplingContext& sampling_context,
            const void* data,
            const bool adjoint,
            const bool cosine_mult,
            const LocalGeometry& geometry,
            const Dual3f& outgoing,
            const int modes,
            BSDFSample& sample) const
        {
            const auto& values = *static_cast<const OSLMicrofacetBSDFInputValues*>(data);
            if (dot(outgoing.get_value(), geometry.m_shading_basis.get_normal()) <= 0.0f)
                return;
            const FresnelDielectricFun fresnel(values.m_reflectance, 1.0f, 1.0f / values.m_eta, 1.0f);
            if (is_smooth(values))
            {
                if (ScatteringMode::has_specular(modes))
                    SpecularBRDFHelper::sample(fresnel, geometry, outgoing, sample);
            }
            else if (ScatteringMode::has_glossy(modes))
            {
                // The standard closure supplies distribution widths directly.
                // Roughness here is only downstream roughness-clamp metadata.
                const float roughness = std::sqrt(std::max(values.m_alpha_x, values.m_alpha_y));
                visit_distribution(values.m_distribution, [&](auto distribution)
                {
                    using MDF = decltype(distribution);
                    MicrofacetBRDFHelper<MDF>::sample(sampling_context, roughness,
                        values.m_alpha_x, values.m_alpha_y, fresnel, geometry, outgoing, sample);
                });
                if (sample.get_mode() != ScatteringMode::None)
                    sample.m_min_roughness = roughness;
            }
            if (sample.get_mode() != ScatteringMode::None &&
                dot(sample.m_incoming.get_value(), geometry.m_shading_basis.get_normal()) <= 0.0f)
                sample.set_to_absorption();
            if (sample.get_mode() != ScatteringMode::None)
                sample.m_value.m_beauty = sample.m_value.m_glossy;
        }

        float evaluate_reflection(
            const void* data,
            const bool adjoint,
            const bool cosine_mult,
            const LocalGeometry& geometry,
            const Vector3f& outgoing,
            const Vector3f& incoming,
            const int modes,
            DirectShadingComponents& value) const
        {
            const auto& values = *static_cast<const OSLMicrofacetBSDFInputValues*>(data);
            if (!valid_glossy(values, geometry, outgoing, incoming, modes))
                return 0.0f;
            const FresnelDielectricFun fresnel(values.m_reflectance, 1.0f, 1.0f / values.m_eta, 1.0f);
            const float probability = visit_distribution(values.m_distribution, [&](auto distribution)
            {
                using MDF = decltype(distribution);
                return MicrofacetBRDFHelper<MDF>::evaluate(values.m_alpha_x, values.m_alpha_y,
                    fresnel, geometry, outgoing, incoming, value.m_glossy);
            });
            value.m_beauty = value.m_glossy;
            return probability;
        }

        float evaluate_reflection_pdf(
            const void* data,
            const bool adjoint,
            const LocalGeometry& geometry,
            const Vector3f& outgoing,
            const Vector3f& incoming,
            const int modes) const
        {
            const auto& values = *static_cast<const OSLMicrofacetBSDFInputValues*>(data);
            if (!valid_glossy(values, geometry, outgoing, incoming, modes))
                return 0.0f;
            return visit_distribution(values.m_distribution, [&](auto distribution)
            {
                using MDF = decltype(distribution);
                return MicrofacetBRDFHelper<MDF>::pdf(values.m_alpha_x, values.m_alpha_y,
                    geometry, outgoing, incoming);
            });
        }

        static bool matches_geometric_side(const LocalGeometry& geometry, const Vector3f& outgoing,
            const Vector3f& incoming, const bool reflection)
        {
            const float wo = dot(outgoing, geometry.m_geometric_normal);
            const float wi = dot(incoming, geometry.m_geometric_normal);
            return wo != 0.0f && wi != 0.0f && (((wo > 0.0f) == (wi > 0.0f)) == reflection);
        }

        static bool is_smooth(const OSLMicrofacetBSDFInputValues& values)
        {
            return values.m_alpha_x == 0.0f && values.m_alpha_y == 0.0f;
        }

        static bool valid_glossy(
            const OSLMicrofacetBSDFInputValues& values,
            const LocalGeometry& geometry,
            const Vector3f& outgoing,
            const Vector3f& incoming,
            const int modes)
        {
            const Vector3f& normal = geometry.m_shading_basis.get_normal();
            return ScatteringMode::has_glossy(modes) && !is_smooth(values) &&
                dot(outgoing, normal) > 0.0f && dot(incoming, normal) > 0.0f;
        }
    };
}

auto_release_ptr<BSDF> OSLMicrofacetBSDFFactory::create(const char* name, const ParamArray& params)
{
    return auto_release_ptr<BSDF>(new BSDFWrapper<OSLMicrofacetBSDFImpl, false>(name, params));
}
} // namespace renderer
