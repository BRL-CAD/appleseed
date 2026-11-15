
//
// This source file is part of appleseed.
// Visit https://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2014-2018 Esteban Tovagliari, The appleseedhq Organization
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
#include "osledf.h"

// appleseed.renderer headers.
#include "renderer/global/globallogger.h"
#include "renderer/global/globaltypes.h"
#include "renderer/kernel/shading/closures.h"
#include "renderer/kernel/shading/shadingcontext.h"
#include "renderer/kernel/shading/shadingpoint.h"
#include "renderer/modeling/edf/diffuseedf.h"
#include "renderer/modeling/bsdf/bsdf.h"
#include "renderer/modeling/bsdf/glossylayerbsdf.h"
#include "renderer/modeling/edf/edf.h"
#include "renderer/modeling/input/scalarsource.h"
#include "renderer/modeling/input/source.h"
#include "renderer/modeling/material/material.h"
#include "renderer/utility/paramarray.h"

// appleseed.foundation headers.
#include "foundation/math/basis.h"
#include "foundation/math/sampling/mappings.h"
#include "foundation/math/scalar.h"
#include "foundation/math/vector.h"
#include "foundation/memory/arena.h"
#include "foundation/platform/compiler.h"

// Standard headers.
#include <cstdint>
#include <limits>

using namespace foundation;

namespace renderer
{

//
// OSL EDF class.
//

namespace
{
    const char* Model = "osl_edf";

    struct DummyNonUniformSource
      : public Source
    {
        DummyNonUniformSource()
          : Source(false)
        {
        }

        std::uint64_t compute_signature() const override
        {
            return 0;
        }

        Hints get_hints() const override
        {
            Hints hints;
            hints.m_width = 1;
            hints.m_height = 1;
            return hints;
        }
    };

    enum class EmissionContext { PreservePath, LaunchLight };

    struct EmissionInputs
    {
        CompositeEmissionClosure        m_closure;
        const ShadingContext&           m_context;
        const ShaderGroup&              m_shader_group;
        ShadingPoint                    m_point;
        Basis3f                         m_basis;
        Vector3f                        m_outgoing;
        VisibilityFlags::Type           m_ray_flags;

        EmissionInputs(const ShadingContext& context, const ShadingPoint& point)
          : m_closure(Basis3f(point.get_shading_basis()),
                point.get_osl_shader_globals().Ci, context.get_arena())
          , m_context(context)
          , m_shader_group(*point.get_material()->get_render_data().m_shader_group)
          , m_point(point)
          , m_basis(Basis3f(point.get_shading_basis()))
          , m_outgoing(-point.get_osl_shader_globals().I)
          , m_ray_flags(static_cast<VisibilityFlags::Type>(point.get_osl_shader_globals().raytype))
        {
        }
    };

    class OSLEDF
      : public EDF
    {
      public:
        OSLEDF()
          : EDF("osl_edf", ParamArray())
        {
            m_diffuse_edf = DiffuseEDFFactory().create("osl_diff_edf", ParamArray());
            m_diffuse_edf->get_inputs().find("radiance").bind(new DummyNonUniformSource());
            m_diffuse_edf->get_inputs().find("radiance_multiplier").bind(new DummyNonUniformSource());
            m_diffuse_edf->get_inputs().find("exposure").bind(new ScalarSource(0.0f));

            m_glossy_layer_bsdf = GlossyLayerBSDFFactory::create("glossy_layer_bsdf", ParamArray());
        }

        void release() override
        {
            delete this;
        }

        const char* get_model() const override
        {
            return Model;
        }

        void* evaluate_inputs(
            const ShadingContext&   shading_context,
            const ShadingPoint&     shading_point) const override
        {
            auto* inputs = shading_context.get_arena().allocate_noinit<EmissionInputs>();
            new (inputs) EmissionInputs(shading_context, shading_point);
            prepare_closure(shading_context, shading_point, inputs->m_closure);
            return inputs;
        }

        bool on_render_begin(
            const Project&          project,
            const BaseGroup*        parent,
            OnRenderBeginRecorder&  recorder,
            IAbortSwitch*           abort_switch) override
        {
            if (!EDF::on_render_begin(project, parent, recorder, abort_switch))
                return false;

            if (!m_diffuse_edf->on_render_begin(project, parent, recorder, abort_switch))
                return false;

            if (!m_glossy_layer_bsdf->on_render_begin(project, parent, recorder, abort_switch))
                return false;

            return true;
        }

        bool on_frame_begin(
            const Project&          project,
            const BaseGroup*        parent,
            OnFrameBeginRecorder&   recorder,
            IAbortSwitch*           abort_switch) override
        {
            if (!EDF::on_frame_begin(project, parent, recorder, abort_switch))
                return false;

            if (!m_diffuse_edf->on_frame_begin(project, parent, recorder, abort_switch))
                return false;

            if (!m_glossy_layer_bsdf->on_frame_begin(project, parent, recorder, abort_switch))
                return false;

            return true;
        }

        void sample(
            SamplingContext&        sampling_context,
            const void*             data,
            const Vector3f&         geometric_normal,
            const Basis3f&          shading_basis,
            const Vector2f&         s,
            Vector3f&               outgoing,
            Spectrum&               value,
            float&                  probability) const override
        {
            // All OSL emission leaves are diffuse EDFs. Use one proposal
            // independent of their current weights: emission may be zero
            // along the provisional normal but positive in other directions.
            outgoing = sample_direction(shading_basis, s, probability);
            evaluate_direction(*static_cast<const EmissionInputs*>(data),
                geometric_normal, shading_basis, outgoing, EmissionContext::LaunchLight, value);
        }

        void sample_light_ray(
            SamplingContext&        sampling_context,
            const ShadingContext&   shading_context,
            const ShadingPoint&     shading_point,
            const Vector3f&         geometric_normal,
            const Basis3f&          shading_basis,
            const Vector2f&         s,
            ShadingRay&             ray,
            Spectrum&               value,
            float&                  probability) const override
        {
            const Vector3f outgoing = sample_direction(shading_basis, s, probability);
            initialize_light_ray(sampling_context, shading_context, shading_point, outgoing, ray);

            // Shader queries and subsequent transport must use the same IOR
            // choices. Keep the geometry ray's direction for surface frames;
            // OSL's I is supplied separately for the actual launch direction.
            ShadingPoint point(shading_point);
            ShadingRay shader_ray = ray;
            shader_ray.m_dir = shading_point.get_ray().m_dir;
            shader_ray.m_tmin = shader_ray.m_tmax = 0.0;
            point.set_ray(shader_ray);
            point.set_shading_basis(Basis3d(shading_basis));
            const ShaderGroup& group = *point.get_material()->get_render_data().m_shader_group;
            shading_context.execute_osl_shading(group, point, -outgoing, VisibilityFlags::LightRay);
            CompositeEmissionClosure closure(Basis3f(point.get_shading_basis()),
                point.get_osl_shader_globals().Ci, shading_context.get_arena());
            prepare_closure(shading_context, point, closure);
            evaluate_closure(closure, geometric_normal, shading_basis, outgoing, value);
        }

        void evaluate(
            const void*             data,
            const Vector3f&         geometric_normal,
            const Basis3f&          shading_basis,
            const Vector3f&         outgoing,
            Spectrum&               value) const override
        {
            const auto& inputs = *static_cast<const EmissionInputs*>(data);
            evaluate_direction(inputs, geometric_normal, shading_basis,
                outgoing, EmissionContext::PreservePath, value);
        }

        void evaluate(
            const void*             data,
            const Vector3f&         geometric_normal,
            const Basis3f&          shading_basis,
            const Vector3f&         outgoing,
            Spectrum&               value,
            float&                  probability) const override
        {
            evaluate(data, geometric_normal, shading_basis, outgoing, value);
            probability = evaluate_pdf(data, geometric_normal, shading_basis, outgoing);
        }

        float evaluate_pdf(
            const void*             data,
            const Vector3f&         geometric_normal,
            const Basis3f&          shading_basis,
            const Vector3f&         outgoing) const override
        {
            return m_diffuse_edf->evaluate_pdf(nullptr, geometric_normal, shading_basis, outgoing);
        }

        float get_uncached_max_contribution() const override
        {
            // We can't know the max contribution of OSL EDFs.
            return std::numeric_limits<float>::max();
        }

      private:
        static Vector3f sample_direction(
            const Basis3f&          shading_basis,
            const Vector2f&         s,
            float&                  probability)
        {
            const Vector3f local_outgoing = sample_hemisphere_cosine(s);
            probability = local_outgoing.y * RcpPi<float>();
            assert(probability > 0.0f);
            return shading_basis.transform_to_parent(local_outgoing);
        }

        void prepare_closure(
            const ShadingContext&       context,
            const ShadingPoint&         point,
            CompositeEmissionClosure&   closure) const
        {
            for (size_t i = 0; i < closure.get_closure_count(); ++i)
            {
                if (closure.get_closure_type(i) >= FirstLayeredClosure)
                {
                    bsdf_from_closure_id(closure.get_closure_type(i)).prepare_inputs(
                        context.get_arena(), point, closure.get_closure_input_values(i));
                }
            }
        }

        void evaluate_direction(
            const EmissionInputs&       inputs,
            const Vector3f&             geometric_normal,
            const Basis3f&              shading_basis,
            const Vector3f&             outgoing,
            const EmissionContext       context,
            Spectrum&                   value) const
        {
            if (dot(outgoing, shading_basis.get_normal()) <= 0.0f)
            {
                value.set(0.0f);
                return;
            }
            const bool launch = context == EmissionContext::LaunchLight;
            const VisibilityFlags::Type ray_flags = launch ? VisibilityFlags::LightRay : inputs.m_ray_flags;
            const ShadingRay& source_ray = inputs.m_point.get_ray();
            const bool same_path = !launch || (source_ray.m_tmax == 0.0 &&
                source_ray.m_depth == 0 && !source_ray.m_has_differentials);
            const bool same_direction = outgoing == inputs.m_outgoing;
            // Exact reuse preserves discontinuous direction and ray-type
            // predicates; an angular tolerance would change shader results.
            const bool same_basis =
                shading_basis.get_normal() == inputs.m_basis.get_normal() &&
                shading_basis.get_tangent_u() == inputs.m_basis.get_tangent_u() &&
                shading_basis.get_tangent_v() == inputs.m_basis.get_tangent_v();
            if (ray_flags == inputs.m_ray_flags && same_path && same_direction && same_basis)
            {
                evaluate_closure(inputs.m_closure, geometric_normal, shading_basis, outgoing, value);
                return;
            }

            ShadingPoint point(inputs.m_point);
            if (launch && !same_path)
            {
                ShadingRay ray = source_ray;
                ray.m_org = inputs.m_point.get_point();
                ray.m_tmin = ray.m_tmax = 0.0;
                ray.m_depth = 0;
                ray.m_has_differentials = false;
                point.set_ray(ray);
            }
            point.set_shading_basis(Basis3d(shading_basis));
            // Retain the hit's geometry, path metadata and surface footprint.
            // New light launches have no camera footprint.
            inputs.m_context.execute_osl_shading(inputs.m_shader_group, point, -outgoing, ray_flags);
            CompositeEmissionClosure closure(Basis3f(point.get_shading_basis()),
                point.get_osl_shader_globals().Ci, inputs.m_context.get_arena());
            prepare_closure(inputs.m_context, point, closure);
            evaluate_closure(closure, geometric_normal, shading_basis, outgoing, value);
        }

        void evaluate_closure(
            const CompositeEmissionClosure& closure,
            const Vector3f&                 geometric_normal,
            const Basis3f&                  shading_basis,
            const Vector3f&                 outgoing,
            Spectrum&                       value) const
        {
            value.set(0.0f);
            for (size_t i = 0; i < closure.get_closure_count(); ++i)
            {
                if (closure.get_closure_type(i) >= FirstLayeredClosure)
                    continue;
                Spectrum component(Spectrum::Illuminance);
                edf_from_closure_id(closure.get_closure_type(i)).evaluate(
                    closure.get_closure_input_values(i), geometric_normal,
                    shading_basis, outgoing, component);
                attenuate_emission(closure, i, outgoing, component);
                value += component;
            }
        }

        void attenuate_emission(
            const CompositeClosure& c,
            const size_t            closure_index,
            const Vector3f&         outgoing,
            Spectrum&               value) const
        {
            const std::int8_t* layers = c.get_closure_layers(closure_index);

            for (size_t i = 0; i < CompositeClosure::MaxClosureLayers; ++i)
            {
                if (layers[i] < 0)
                    break;

                const size_t layer = static_cast<size_t>(layers[i]);
                bsdf_from_closure_id(c.get_closure_type(layer)).attenuate_emission(
                    c.get_closure_input_values(layer),
                    c.get_closure_shading_basis(layer),
                    outgoing,
                    value);
            }
        }

        const BSDF& bsdf_from_closure_id(const ClosureID cid) const
        {
            assert(cid >= FirstLayeredClosure);
            return *m_glossy_layer_bsdf;
        }

        const EDF& edf_from_closure_id(const ClosureID cid) const
        {
            assert(cid == EmissionID);
            return *m_diffuse_edf;
        }

        auto_release_ptr<EDF>   m_diffuse_edf;
        auto_release_ptr<BSDF>  m_glossy_layer_bsdf;
    };
}


//
// OSLEDFFactory class implementation.
//

auto_release_ptr<EDF> OSLEDFFactory::create() const
{
    return auto_release_ptr<EDF>(new OSLEDF());
}

}   // namespace renderer
