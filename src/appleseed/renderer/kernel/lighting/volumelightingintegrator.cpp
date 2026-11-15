
//
// This source file is part of appleseed.
// Visit https://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2017-2018 Artem Bishev, The appleseedhq Organization
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
#include "volumelightingintegrator.h"

// appleseed.renderer headers.
#include "renderer/kernel/lighting/backwardlightsampler.h"
#include "renderer/kernel/lighting/directlightingintegrator.h"
#include "renderer/kernel/lighting/imagebasedlighting.h"
#include "renderer/kernel/lighting/scatteringmode.h"
#include "renderer/kernel/shading/directshadingcomponents.h"
#include "renderer/kernel/shading/shadingcontext.h"
#include "renderer/modeling/light/light.h"
#include "renderer/modeling/scene/visibilityflags.h"
#include "renderer/modeling/volume/volume.h"

// appleseed.foundation headers.
#include "foundation/math/sampling/mappings.h"
#include "foundation/math/scalar.h"

// Standard headers.
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>

using namespace foundation;

namespace renderer
{

namespace
{
    struct EquiangularSampler
    {
      public:
        EquiangularSampler(
            const LightSample&          light_sample,
            const ShadingRay&           volume_ray,
            const ShadingContext&       shading_context,
            SamplingContext&            sampling_context)
          : m_shading_context(shading_context)
          , m_sampling_context(sampling_context)
        {
            Vector3d emission_position;
            if (light_sample.m_light != nullptr)
            {
                m_sampling_context.split_in_place(2, 1);
                Vector3d emission_direction;  // not used
                Spectrum light_value(Spectrum::Illuminance);
                float probability;
                light_sample.m_light->sample(
                    m_shading_context,
                    light_sample.m_light_transform,
                    m_sampling_context.next2<Vector2d>(),
                    emission_position,
                    emission_direction,
                    light_value,
                    probability);
            }
            else
                // The position guides a normalized distance proposal; a null
                // surface draw must not remove that proposal's contribution.
                // Its bounds supply a positive guide height below, including
                // when the centroid lies directly on the volume ray.
                emission_position = light_sample.m_valid
                    ? light_sample.m_point : light_sample.m_shape->get_centroid();

            const Vector3d origin_to_light(emission_position - volume_ray.m_org);
            m_origin_to_projection = dot(origin_to_light, volume_ray.m_dir);

            // A cross product avoids cancellation when the guide is nearly
            // on the ray. A normalized proposal may use a positive surrogate
            // height for a null draw or a source whose guide angles would
            // collapse at float precision, including collinear roundoff.
            const double guide_distance = norm(origin_to_light);
            const double minimum_height = std::numeric_limits<float>::epsilon() * guide_distance;
            double height = norm(cross(origin_to_light, volume_ray.m_dir));
            if (!light_sample.m_valid || !(height > minimum_height))
            {
                height = light_sample.m_shape
                    ? 0.5 * norm(light_sample.m_shape->get_bbox().extent())
                    : guide_distance;
            }
            m_projection_to_light = height;
            if (!(m_projection_to_light > 0.0) || !std::isfinite(m_projection_to_light)
                || !std::isfinite(m_origin_to_projection))
                throw std::runtime_error("Degenerate equiangular light guide");

            m_ray_length = volume_ray.get_length();

            m_near_angle = std::atan2(-m_origin_to_projection, m_projection_to_light);

            m_far_angle =
                volume_ray.is_finite() ?
                std::atan2(
                    m_ray_length - m_origin_to_projection,
                    m_projection_to_light) :
                HalfPi<double>();
        }

        float sample() const
        {
            m_sampling_context.split_in_place(1, 1);

            const double distance = m_origin_to_projection +
                sample_equiangular_distribution(
                    static_cast<double>(m_sampling_context.next2<float>()),
                    m_near_angle,
                    m_far_angle,
                    m_projection_to_light);

            return static_cast<float>(clamp(distance, 0.0, m_ray_length));
        }

        float evaluate(const float distance_sample) const
        {
            return static_cast<float>(equiangular_distribution_pdf(
                static_cast<double>(distance_sample) - m_origin_to_projection,
                m_near_angle,
                m_far_angle,
                m_projection_to_light));
        }

      private:
        const ShadingContext&   m_shading_context;
        SamplingContext&        m_sampling_context;

        // Nearly collinear finite segments have narrow angular intervals;
        // float angles quantize their samples and misnormalize their PDFs.
        double                  m_origin_to_projection;
        double                  m_projection_to_light;
        double                  m_ray_length;
        double                  m_near_angle;
        double                  m_far_angle;
    };
}


//
// VolumeLightingIntegrator class implementation.
//
// Call graph:
//
//   compute_radiance_combined_sampling
//       add_single_distance_sample_contribution
//
//   compute_radiance_exponential_sampling
//       add_single_distance_sample_contribution_exponential_only
//
//   add_single_distance_sample_contribution
//       take_single_direction_sample
//
//   add_single_distance_sample_contribution_exponential_only
//       take_single_direction_sample
//
//   take_single_direction_sample
//       DirectLightingIntegrator::take_single_material_sample
//       DirectLightingIntegrator::add_emitting_shape_sample_contribution
//       DirectLightingIntegrator::add_non_physical_light_sample_contribution

VolumeLightingIntegrator::VolumeLightingIntegrator(
    const ShadingContext&           shading_context,
    const BackwardLightSampler&     light_sampler,
    const Volume&                   volume,
    const ShadingRay&               volume_ray,
    const void*                     volume_data,
    const ShadingPoint&             shading_point,
    const int                       scattering_modes,
    const size_t                    distance_sample_count,
    const size_t                    light_sample_count,
    const float                     low_light_threshold,
    const bool                      indirect)
    : m_shading_context(shading_context)
    , m_light_sampler(light_sampler)
    , m_volume(volume)
    , m_volume_ray(volume_ray)
    , m_volume_data(volume_data)
    , m_shading_point(shading_point)
    , m_time(volume_ray.m_time)
    , m_scattering_modes(scattering_modes)
    , m_light_sample_count(light_sample_count)
    , m_distance_sample_count(distance_sample_count)
    , m_rcp_distance_sample_count(
        m_distance_sample_count == 0 ? 0.0f : 1.0f / m_distance_sample_count)
    , m_low_light_threshold(low_light_threshold)
    , m_indirect(indirect)
{
}

void VolumeLightingIntegrator::add_single_distance_sample_contribution(
    const LightSample*          light_sample,
    const Spectrum&             extinction_coef,
    SamplingContext&            sampling_context,
    const MISHeuristic          mis_heuristic,
    DirectShadingComponents&    radiance,
    const bool                  sample_phase_function) const
{
    assert(light_sample != nullptr);

    // Sample channel uniformly at random.
    sampling_context.split_in_place(1, 1);
    const float s = sampling_context.next2<float>();
    const size_t channel = truncate<size_t>(s * Spectrum::size());

    // Prepare equiangular sampling.
    const EquiangularSampler equiangular_distance_sampler(
        *light_sample,
        m_volume_ray,
        m_shading_context,
        sampling_context);

    //
    // Exponential sampling.
    //

    if (extinction_coef[channel] > 0.0f)
    {
        const float exponential_sample =
            draw_exponential_sample(sampling_context, m_volume_ray, extinction_coef[channel]);
        const float exponential_prob =
            evaluate_exponential_sample(exponential_sample, m_volume_ray, extinction_coef[channel]);
        const float equiangular_prob =
            equiangular_distance_sampler.evaluate(exponential_sample);

        // Calculate MIS weight for spectral channel sampling (power heuristic).
        // One-sample estimator is used (Veach: 9.2.4 eq. 9.15).
        float mis_weights_sum = 0.0f;
        for (size_t i = 0, e = Spectrum::size(); i < e; ++i)
        {
            if (extinction_coef[i] > 0.0f)
            {
                const float probability =
                    evaluate_exponential_sample(
                        exponential_sample,
                        m_volume_ray,
                        extinction_coef[i]);
                mis_weights_sum += square(probability);
            }
        }
        const float mis_weight_channel =
            Spectrum::size() *
            square(exponential_prob) /
            mis_weights_sum;

        // Calculate MIS weight for distance sampling.
        const float mis_weight_distance = mis(
            mis_heuristic, exponential_prob, equiangular_prob);

        DirectShadingComponents inscattered;
        take_single_direction_sample(
            sample_phase_function,
            sampling_context,
            light_sample,
            exponential_sample,
            mis_heuristic,
            inscattered);

        Spectrum transmission;
        m_volume.evaluate_transmission(
            m_volume_data,
            m_volume_ray,
            exponential_sample,
            transmission);
        inscattered *= transmission;
        inscattered *=
            m_rcp_distance_sample_count *
            mis_weight_channel *
            mis_weight_distance /
            exponential_prob;
        radiance += inscattered;
    }

    //
    // Equiangular sampling.
    //

    {
        const float equiangular_sample =
            equiangular_distance_sampler.sample();
        const float equiangular_prob =
            equiangular_distance_sampler.evaluate(equiangular_sample);
        const float exponential_prob = evaluate_exponential_sample(
            equiangular_sample, m_volume_ray, extinction_coef[channel]);

        DirectShadingComponents inscattered;
        take_single_direction_sample(
            sample_phase_function,
            sampling_context,
            light_sample,
            equiangular_sample,
            mis_heuristic,
            inscattered);

        // Calculate MIS weight for distance sampling.
        const float mis_weight = mis(
            mis_heuristic, equiangular_prob, exponential_prob);

        Spectrum transmission;
        m_volume.evaluate_transmission(
            m_volume_data, m_volume_ray, equiangular_sample, transmission);
        inscattered *= transmission;
        inscattered *=
            m_rcp_distance_sample_count *
            mis_weight /
            equiangular_prob;
        radiance += inscattered;
    }
}

void VolumeLightingIntegrator::add_single_distance_sample_contribution_exponential_only(
    const LightSample*          light_sample,
    const Spectrum&             extinction_coef,
    SamplingContext&            sampling_context,
    const MISHeuristic          mis_heuristic,
    DirectShadingComponents&    radiance,
    const bool                  sample_phase_function) const
{
    // Sample channel uniformly at random.
    sampling_context.split_in_place(1, 1);
    const float s = sampling_context.next2<float>();
    const size_t channel = truncate<size_t>(s * Spectrum::size());
    if (extinction_coef[channel] == 0.0f)
        return;

    const float exponential_sample = draw_exponential_sample(
        sampling_context, m_volume_ray, extinction_coef[channel]);
    const float exponential_prob = evaluate_exponential_sample(
        exponential_sample, m_volume_ray, extinction_coef[channel]);

    // Calculate MIS weight for spectral channel sampling (balance heuristic).
    // One-sample estimator is used (Veach: 9.2.4 eq. 9.15).
    float mis_weights_sum = 0.0f;
    for (size_t i = 0, e = Spectrum::size(); i < e; ++i)
    {
        const float probability =
            evaluate_exponential_sample(
                exponential_sample, m_volume_ray, extinction_coef[i]);
        mis_weights_sum += square(probability);
    }
    const float mis_weight_channel =
        Spectrum::size() *
        square(exponential_prob) /
        mis_weights_sum;

    DirectShadingComponents inscattered;
    take_single_direction_sample(
        sample_phase_function,
        sampling_context,
        light_sample,
        exponential_sample,
        mis_heuristic,
        inscattered);

    Spectrum transmission;
    m_volume.evaluate_transmission(
        m_volume_data,
        m_volume_ray,
        exponential_sample,
        transmission);
    inscattered *= transmission;
    inscattered *=
        0.5f *
        m_rcp_distance_sample_count *
        mis_weight_channel /
        exponential_prob;
    radiance += inscattered;
}

void VolumeLightingIntegrator::compute_radiance_combined_sampling(
    SamplingContext&            sampling_context,
    const MISHeuristic          mis_heuristic,
    DirectShadingComponents&    radiance) const
{
    radiance.set(0.0f);

    // No light source in the scene.
    if (!m_light_sampler.has_lights())
        return;

    const Spectrum& extinction_coef = m_volume.extinction_coefficient(
        m_volume_data, m_volume_ray);

    if (m_distance_sample_count > 0)
    {
        const size_t light_count = m_light_sampler.get_non_physical_light_count();

        // Add contributions from non-physical light sources that don't belong to the lightset.
        for (size_t light_idx = 0; light_idx < light_count; ++light_idx)
        {
            LightSample light_sample;
            m_light_sampler.sample_non_physical_light(m_time, light_idx, light_sample);

            for (size_t i = 0; i < m_distance_sample_count; ++i)
            {
                add_single_distance_sample_contribution(
                    &light_sample,
                    extinction_coef,
                    sampling_context,
                    mis_heuristic,
                    radiance,
                    false);
            }
        }
    }

    // Add contributions from the light set.
    if (m_light_sampler.has_lightset())
    {
        SamplingContext child_sampling_context =
            sampling_context.split(3, m_distance_sample_count);

        for (size_t i = 0; i < m_distance_sample_count; ++i)
        {
            const Vector3f s = child_sampling_context.next2<Vector3f>();
            SamplingContext light_sampling_context = child_sampling_context.split(0, 0);
            LightSample light_sample;
            m_light_sampler.sample_lightset(
                m_time,
                s,
                m_shading_point,
                light_sample,
                &light_sampling_context);

            for (size_t j = 0; j < m_light_sample_count; ++j)
            {
                add_single_distance_sample_contribution(
                    &light_sample,
                    extinction_coef,
                    light_sampling_context,
                    mis_heuristic,
                    radiance,
                    false);
            }

            if (!ScatteringMode::has_volume(m_scattering_modes))
            {
                add_single_distance_sample_contribution(
                    &light_sample,
                    extinction_coef,
                    light_sampling_context,
                    mis_heuristic,
                    radiance,
                    true);
            }
        }
    }
}

void VolumeLightingIntegrator::compute_radiance_exponential_sampling(
    SamplingContext&            sampling_context,
    const MISHeuristic          mis_heuristic,
    DirectShadingComponents&    radiance) const
{
    radiance.set(0.0f);

    // No light source in the scene.
    if (!m_light_sampler.has_lights())
        return;

    const Spectrum& extinction_coef = m_volume.extinction_coefficient(
        m_volume_data, m_volume_ray);

    if (m_distance_sample_count > 0)
    {
        const size_t light_count = m_light_sampler.get_non_physical_light_count();

        // Add contributions from non-physical light sources that don't belong to the lightset.
        for (size_t light_idx = 0; light_idx < light_count; ++light_idx)
        {
            LightSample light_sample;
            m_light_sampler.sample_non_physical_light(m_time, light_idx, light_sample);

            for (size_t i = 0; i < 2 * m_distance_sample_count; ++i)
            {
                add_single_distance_sample_contribution_exponential_only(
                    &light_sample,
                    extinction_coef,
                    sampling_context,
                    mis_heuristic,
                    radiance,
                    false);
            }
        }
    }

    // Add contributions from the light set.
    if (m_light_sampler.has_lightset())
    {
        for (size_t i = 0; i < 2 * m_distance_sample_count; ++i)
        {
            for (size_t j = 0; j < m_light_sample_count; ++j)
            {
                add_single_distance_sample_contribution_exponential_only(
                    nullptr,
                    extinction_coef,
                    sampling_context,
                    mis_heuristic,
                    radiance,
                    false);
            }

            if (!ScatteringMode::has_volume(m_scattering_modes))
            {
                add_single_distance_sample_contribution_exponential_only(
                    nullptr,
                    extinction_coef,
                    sampling_context,
                    mis_heuristic,
                    radiance,
                    true);
            }
        }
    }
}

void VolumeLightingIntegrator::compute_environment_radiance(
    SamplingContext&            sampling_context,
    const EnvironmentEDF&       environment_edf,
    const size_t                env_sample_count,
    DirectShadingComponents&    radiance) const
{
    radiance.set(0.0f);
    if (env_sample_count == 0)
        return;

    const Spectrum& extinction = m_volume.extinction_coefficient(m_volume_data, m_volume_ray);
    for (size_t sample = 0; sample < m_distance_sample_count; ++sample)
    {
        sampling_context.split_in_place(1, 1);
        const size_t channel = truncate<size_t>(sampling_context.next2<float>() * Spectrum::size());
        if (extinction[channel] == 0.0f)
            continue;

        const float distance = draw_exponential_sample(sampling_context, m_volume_ray, extinction[channel]);
        // The spectral mixture includes a null draw for zero-extinction channels.
        double probability = 0.0;
        for (size_t c = 0; c < Spectrum::size(); ++c)
        {
            if (extinction[c] > 0.0f)
                probability += evaluate_exponential_sample(distance, m_volume_ray, extinction[c]);
        }
        probability /= Spectrum::size();
        if (!(probability > 0.0) || !std::isfinite(probability))
            throw std::runtime_error("Invalid volume environment distance density");

        const VolumeSampler sampler(m_volume_ray, m_volume, m_volume_data, distance, m_shading_point);
        const Dual3d outgoing(-m_volume_ray.m_dir);
        DirectShadingComponents inscattered;
        compute_ibl_environment_sampling(
            sampling_context, m_shading_context, environment_edf, outgoing,
            sampler, ScatteringMode::Volume, 1, env_sample_count, inscattered, nullptr);
        // Continued paths supply the competing phase-function proposal. At the
        // bounce limit that proposal must instead be evaluated on this segment.
        if (!ScatteringMode::has_volume(m_scattering_modes))
        {
            DirectShadingComponents phase_radiance;
            compute_ibl_material_sampling(
                sampling_context, m_shading_context, environment_edf, outgoing,
                sampler, 1, env_sample_count, phase_radiance);
            inscattered += phase_radiance;
        }

        Spectrum transmission;
        m_volume.evaluate_transmission(m_volume_data, m_volume_ray, distance, transmission);
        inscattered *= transmission;
        inscattered *= static_cast<float>(m_rcp_distance_sample_count / probability);
        radiance += inscattered;
    }
}

void VolumeLightingIntegrator::take_single_direction_sample(
    const bool                  sample_phase_function,
    SamplingContext&            sampling_context,
    const LightSample*          light_sample,
    const float                 distance_sample,
    const MISHeuristic          mis_heuristic,
    DirectShadingComponents&    radiance) const
{
    radiance.set(0.0f);

    VolumeSampler volume_sampler(
        m_volume_ray,
        m_volume,
        m_volume_data,
        distance_sample,
        m_shading_point);

    DirectLightingIntegrator integrator(
        m_shading_context,
        m_light_sampler,
        volume_sampler,
        m_time,
        m_scattering_modes,
        1,
        m_light_sample_count,
        m_low_light_threshold,
        m_indirect);

    if (sample_phase_function)
    {
        integrator.take_single_material_sample(
            sampling_context,
            mis_heuristic,
            Dual3d(m_volume_ray.m_dir),
            radiance);
    }
    else if (light_sample == nullptr || light_sample->m_shape != nullptr)
    {
        sampling_context.split_in_place(3, 1);
        LightSample light_sample;
        m_light_sampler.sample_lightset(
            m_time,
            sampling_context.next2<Vector3f>(),
            m_shading_point,
            light_sample,
            &sampling_context);
        if (!light_sample.m_valid)
            return;

        integrator.add_emitting_shape_sample_contribution(
            sampling_context,
            light_sample,
            mis_heuristic,
            Dual3d(m_volume_ray.m_dir),
            radiance,
            nullptr);
    }
    else
    {
        integrator.add_non_physical_light_sample_contribution(
            sampling_context,
            *light_sample,
            Dual3d(m_volume_ray.m_dir),
            radiance,
            nullptr);
    }
}

float VolumeLightingIntegrator::draw_exponential_sample(
    SamplingContext&    sampling_context,
    const ShadingRay&   volume_ray,
    const float         extinction) const
{
    sampling_context.split_in_place(1, 1);

    if (!volume_ray.is_finite())
    {
        return sample_exponential_distribution(
            sampling_context.next2<float>(), extinction);
    }
    else
    {
        const float ray_length = static_cast<float>(volume_ray.get_length());
        return sample_exponential_distribution_on_segment(
            sampling_context.next2<float>(), extinction, 0.0f, ray_length);
    }
}

float VolumeLightingIntegrator::evaluate_exponential_sample(
    const float         distance,
    const ShadingRay&   volume_ray,
    const float         extinction) const
{
    if (extinction == 0.0f)
        return static_cast<float>(1.0 / m_volume_ray.get_length());
    if (!volume_ray.is_finite())
        return exponential_distribution_pdf(distance, extinction);
    else
    {
        const float ray_length = static_cast<float>(volume_ray.get_length());
        return exponential_distribution_on_segment_pdf(
            distance, extinction, 0.0f, ray_length);
    }
}

}   // namespace renderer
