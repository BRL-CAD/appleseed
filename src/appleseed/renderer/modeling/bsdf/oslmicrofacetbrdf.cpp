
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

#include "oslmicrofacetbrdf.h"

#include "renderer/kernel/shading/shadingpoint.h"
#include "renderer/modeling/bsdf/bsdf.h"
#include "renderer/modeling/bsdf/bsdfsample.h"
#include "renderer/modeling/bsdf/bsdfwrapper.h"
#include "renderer/modeling/bsdf/fresnel.h"
#include "renderer/modeling/bsdf/microfacethelper.h"
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

    class OSLMicrofacetBRDFImpl
      : public BSDF
    {
      public:
        OSLMicrofacetBRDFImpl(const char* name, const ParamArray& params)
          : BSDF(name, Reflective, ScatteringMode::Glossy | ScatteringMode::Specular, params)
        {
        }

        void release() override
        {
            delete this;
        }

        const char* get_model() const override
        {
            return "osl_microfacet_brdf";
        }

        size_t compute_input_data_size() const override
        {
            return sizeof(OSLMicrofacetBRDFInputValues);
        }

        void prepare_inputs(Arena& arena, const ShadingPoint& point, void* data) const override
        {
            auto& values = *static_cast<OSLMicrofacetBRDFInputValues*>(data);
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
            const auto& values = *static_cast<const OSLMicrofacetBRDFInputValues*>(data);
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
            const auto& values = *static_cast<const OSLMicrofacetBRDFInputValues*>(data);
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

        float evaluate_pdf(
            const void* data,
            const bool adjoint,
            const LocalGeometry& geometry,
            const Vector3f& outgoing,
            const Vector3f& incoming,
            const int modes) const override
        {
            const auto& values = *static_cast<const OSLMicrofacetBRDFInputValues*>(data);
            if (!valid_glossy(values, geometry, outgoing, incoming, modes))
                return 0.0f;
            return visit_distribution(values.m_distribution, [&](auto distribution)
            {
                using MDF = decltype(distribution);
                return MicrofacetBRDFHelper<MDF>::pdf(values.m_alpha_x, values.m_alpha_y,
                    geometry, outgoing, incoming);
            });
        }

      private:
        static bool is_smooth(const OSLMicrofacetBRDFInputValues& values)
        {
            return values.m_alpha_x == 0.0f && values.m_alpha_y == 0.0f;
        }

        static bool valid_glossy(
            const OSLMicrofacetBRDFInputValues& values,
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

auto_release_ptr<BSDF> OSLMicrofacetBRDFFactory::create(const char* name, const ParamArray& params)
{
    return auto_release_ptr<BSDF>(new BSDFWrapper<OSLMicrofacetBRDFImpl>(name, params));
}
} // namespace renderer
