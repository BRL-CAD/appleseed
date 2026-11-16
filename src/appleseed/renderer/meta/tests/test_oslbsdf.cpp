
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
#include "renderer/kernel/rendering/rendererservices.h"
#include "renderer/kernel/shading/closures.h"
#include "renderer/kernel/shading/oslshadingsystem.h"
#include "renderer/kernel/shading/shadingpoint.h"
#include "renderer/kernel/shading/shadingpointbuilder.h"
#include "renderer/kernel/texturing/oiiotexturesystem.h"
#include "renderer/modeling/bsdf/bsdf.h"
#include "renderer/modeling/bsdf/bsdfsample.h"
#include "renderer/modeling/bsdf/oslbsdf.h"
#include "renderer/modeling/bsdf/oslmicrofacetbsdf.h"
#include "renderer/modeling/bsdf/microfacettransmissionhelper.h"
#include "renderer/modeling/bsdf/metalbrdf.h"
#include "renderer/modeling/bsdf/glossybrdf.h"
#include "renderer/utility/paramarray.h"
#include "renderer/modeling/project/project.h"

// appleseed.foundation headers.
#include "foundation/math/basis.h"
#include "foundation/math/rng/xoroshiro128plus.h"
#include "foundation/math/scalar.h"
#include "foundation/math/vector.h"
#include "foundation/memory/arena.h"
#include "foundation/memory/autoreleaseptr.h"
#include "foundation/utility/iostreamop.h"
#include "foundation/utility/test.h"

// OSL headers.
#include "foundation/platform/_beginoslheaders.h"
#include "OSL/oslclosure.h"
#include "OSL/genclosure.h"
#include "foundation/platform/_endoslheaders.h"

// Standard headers.
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstring>
#include <memory>

using namespace foundation;
using namespace renderer;

TEST_SUITE(Renderer_Modeling_BSDF_OSL)
{
    // A double-precision analytic reference, independent of the native MDF
    // helpers, distinguishes the two distributions away from their pole.
    struct StandardMicrofacetReference
    {
        static double lambda(const Vector3d& direction, const Vector2d& alpha, const bool beckmann)
        {
            const double projected = square(alpha.x * direction.x) + square(alpha.y * direction.z);
            if (projected == 0.0)
                return 0.0;
            if (!beckmann)
                return 0.5 * (std::sqrt(1.0 + projected / square(direction.y)) - 1.0);
            const double a = std::abs(direction.y) / std::sqrt(projected);
            // Match the model's Smith masking fit, independently of its
            // projected-roughness implementation and float arithmetic.
            return a >= 1.6 ? 0.0 :
                (1.0 - 1.259 * a + 0.396 * square(a)) / (3.535 * a + 2.181 * square(a));
        }

        static double fresnel(const double cosine, const double eta)
        {
            const double transmitted_squared = (1.0 - square(cosine)) / square(eta);
            if (transmitted_squared >= 1.0)
                return 1.0;
            const double transmitted_cosine = std::sqrt(1.0 - transmitted_squared);
            const double perpendicular = (cosine - eta * transmitted_cosine) /
                (cosine + eta * transmitted_cosine);
            const double parallel = (eta * cosine - transmitted_cosine) /
                (eta * cosine + transmitted_cosine);
            return 0.5 * (square(perpendicular) + square(parallel));
        }

        static double density(const Vector3d& half, const Vector2d& alpha, const bool beckmann)
        {
            const double slope_squared = square(half.x / alpha.x) + square(half.z / alpha.y);
            return beckmann ?
                std::exp(-slope_squared / square(half.y)) /
                    (Pi<double>() * alpha.x * alpha.y * square(square(half.y))) :
                1.0 / (Pi<double>() * alpha.x * alpha.y * square(slope_squared + square(half.y)));
        }

        static Vector2d evaluate(const Vector3f& outgoing, const Vector3f& incoming,
            const Vector2f& widths, const float eta, const bool beckmann)
        {
            const Vector3d wo(outgoing), wi(incoming);
            const Vector2d alpha(widths);
            const Vector3d half = normalize(wo + wi);
            const double D = density(half, alpha, beckmann);
            const double lambda_o = lambda(wo, alpha, beckmann);
            const double masking = 1.0 / (1.0 + lambda_o + lambda(wi, alpha, beckmann));
            const double reflectance = fresnel(std::min(1.0, dot(wo, half)), eta);
            return Vector2d(reflectance * D * masking / (4.0 * wo.y * wi.y),
                D / (4.0 * wo.y * (1.0 + lambda_o)));
        }

        static Vector2d transmission(const Vector3f& outgoing, const Vector3f& incoming,
            const Vector2f& widths, const double eta, const bool beckmann, const bool adjoint)
        {
            const Vector3d wo(outgoing), wi(incoming);
            const Vector2d alpha(widths);
            Vector3d half = normalize(wo + eta * wi);
            if (half.y < 0.0)
                half = -half;
            const double cos_o = dot(wo, half), cos_i = dot(wi, half);
            const double denom = square(cos_o + eta * cos_i);
            const double D = density(half, alpha, beckmann);
            const double lambda_o = lambda(wo, alpha, beckmann);
            const double G = 1.0 / (1.0 + lambda_o + lambda(wi, alpha, beckmann));
            const double T = 1.0 - fresnel(std::min(1.0, std::abs(cos_o)), eta);
            const double value = T * D * G * std::abs(cos_o * cos_i / (wo.y * wi.y)) / denom;
            const double pdf = D * std::abs(cos_o / wo.y) / (1.0 + lambda_o) *
                std::abs(cos_i) * square(eta) / denom;
            return Vector2d(value * (adjoint ? square(eta) : 1.0), pdf);
        }
    };

    struct Fixture
    {
        auto_release_ptr<Project> project;
        std::shared_ptr<OIIOTextureSystem> textures;
        RendererServices services;
        auto_release_ptr<OSLShadingSystem> system;
        auto_release_ptr<BSDF> bsdf;
        Arena arena;
        Basis3f basis;
        const OSL::ClosureColor* tree;
        ShadingPoint point;
        BSDF::LocalGeometry geometry;
        Xoroshiro128plus rng;
        SamplingContext sampling;

        Fixture()
          : project(ProjectFactory::create("mixture_test"))
          , textures(OIIOTextureSystemFactory::create())
          , services(*project, *textures)
          , system(OSLShadingSystemFactory::create(&services, textures.get()))
          , bsdf(OSLBSDFFactory().create())
          , basis(Vector3f(0.0f, 1.0f, 0.0f))
          , tree(nullptr)
          , sampling(rng, SamplingContext::RNGMode)
        {
            register_closures(*system);
            ShadingPointBuilder builder(point);
            builder.set_ray(ShadingRay(Vector3d(0.0, 1.0, 0.0),
                Vector3d(0.0, -1.0, 0.0), ShadingRay::Time(), VisibilityFlags::CameraRay, 0));
            builder.set_primitive_type(ShadingPoint::PrimitiveTriangle);
            builder.set_geometric_normal(Vector3d(0.0, 1.0, 0.0));
            builder.set_side(ObjectInstance::FrontSide);
            builder.set_shading_basis(Basis3d(basis));
            geometry.m_shading_point = &point;
            geometry.m_geometric_normal = basis.get_normal();
            geometry.m_shading_basis = basis;
        }

        OSL::ClosureComponent* add_closure(
            const ClosureID     id,
            const float         weight,
            const Vector3f&     normal,
            const float         ior = 0.0f)
        {
            const OSL::ClosureParam* params = nullptr;
            int closure_id = id;
            if (!system->query_closure(nullptr, &closure_id, &params))
                throw Exception("test closure is not registered");
            const OSL::ClosureParam* finish = params;
            while (finish->type != OSL::TypeDesc())
                ++finish;
            // Keyword additions can grow a registered closure's parameter
            // layout. Allocate the actual layout instead of guessing its size.
            auto* node = static_cast<OSL::ClosureComponent*>(
                arena.allocate(sizeof(OSL::ClosureComponent) + finish->offset));
            new (node) OSL::ClosureComponent();
            node->id = id;
            node->w = OSL::Vec3(weight);
            std::memset(node->data(), 0, finish->offset);
            const OSL::Vec3 n(normal.x, normal.y, normal.z);
            std::memcpy(static_cast<char*>(node->data()) + params[id == OSLMicrofacetID ? 1 : 0].offset, &n, sizeof(n));
            if (id == ReflectionID)
                std::memcpy(static_cast<char*>(node->data()) + params[1].offset, &ior, sizeof(ior));
            if (tree)
            {
                auto* sum = arena.allocate<OSL::ClosureAdd>();
                sum->id = OSL::ClosureColor::ADD;
                sum->closureA = tree;
                sum->closureB = node;
                tree = sum;
            }
            else tree = node;
            return node;
        }

        template <typename T>
        void set_keyword(OSL::ClosureComponent* node, const char* key, const T& value)
        {
            const OSL::ClosureParam* params = nullptr;
            if (!system->query_closure(nullptr, &node->id, &params))
                throw Exception("test closure is not registered");
            for (; params->type != OSL::TypeDesc(); ++params)
            {
                if (params->key && std::strcmp(params->key, key) == 0)
                {
                    if (params->field_size != sizeof(T))
                        throw Exception("test closure keyword has an unexpected size");
                    std::memcpy(static_cast<char*>(node->data()) + params->offset, &value, sizeof(T));
                    return;
                }
            }
            throw Exception("test closure keyword is not registered");
        }

        template <typename T>
        void set_formal(OSL::ClosureComponent* node, const size_t index, const T& value)
        {
            const OSL::ClosureParam* params = nullptr;
            if (!system->query_closure(nullptr, &node->id, &params))
                throw Exception("test closure is not registered");
            for (size_t field = 0; field <= index; ++field)
            {
                if (params[field].type == OSL::TypeDesc() || params[field].key)
                    throw Exception("test formal parameter is not registered");
            }
            if (params[index].field_size != sizeof(T))
                throw Exception("test formal parameter has an unexpected size");
            std::memcpy(static_cast<char*>(node->data()) + params[index].offset, &value, sizeof(T));
        }

        OSL::ClosureComponent* add_standard_microfacet(
            const char* distribution, const float alpha_x, const float alpha_y, const float eta,
            const int mode = 0)
        {
            tree = nullptr;
            auto* node = add_closure(OSLMicrofacetID, 1.0f, basis.get_normal());
            set_formal(node, 0, OSL::ustringhash(distribution));
            set_formal(node, 2, OSL::Vec3(1.0f, 0.0f, 0.0f));
            set_formal(node, 3, alpha_x);
            set_formal(node, 4, alpha_y);
            set_formal(node, 5, eta);
            set_formal(node, 6, mode);
            return node;
        }

        void add_diffuse(const float weight, const Vector3f& normal)
        {
            add_closure(DiffuseID, weight, normal);
        }

        void add_mirror(const float weight, const Vector3f& normal)
        {
            add_closure(ReflectionID, weight, normal);
        }

        BSDFSample sample(const int modes = ScatteringMode::All)
        {
            Arena inputs;
            CompositeSurfaceClosure closure(basis, tree, inputs);
            bsdf->prepare_inputs(inputs, point, &closure);
            BSDFSample result;
            bsdf->sample(sampling, &closure, false, false, geometry,
                Dual3f(basis.get_normal()), modes, result);
            return result;
        }
    };

    TEST_CASE_F(RendererServices_OutsideIorUsesTheAdjacentMediumAndRejectsMissingContext, Fixture)
    {
        const OIIO::ustringhash attribute("path:outside_ior");
        const OIIO::ustringhash object;
        OSL::ShaderGlobals globals{};
        globals.renderstate = &point;
        ShadingRay ray = point.get_ray();
        ray.m_medium_count = 2;
        ray.m_media[0] = {nullptr, nullptr, 1.6f};
        ray.m_media[1] = {nullptr, nullptr, 1.2f};
        ShadingPointBuilder builder(point);
        builder.set_ray(ray);
        const auto check = [&](const float expected)
        {
            float value[3] = {-1.0f, -1.0f, -1.0f};
            EXPECT_TRUE(services.get_attribute(&globals, true, object, OIIO::TypeFloat, attribute, value));
            EXPECT_EQ(expected, value[0]);
            EXPECT_EQ(0.0f, value[1]);
            EXPECT_EQ(0.0f, value[2]);
        };
        for (const bool entering : {false, true})
        {
            builder.set_side(entering ? ObjectInstance::FrontSide : ObjectInstance::BackSide);
            point.set_no_optical_interface();
            check(entering ? 1.6f : 1.2f);
            point.set_optical_interface(ShadingPoint::OpticalInterface::Surface, ray, 1.1f);
            check(1.1f);
        }
        int wrong_type = -1;
        EXPECT_FALSE(services.get_attribute(&globals, false, object, OIIO::TypeInt, attribute, &wrong_type));
        EXPECT_EQ(-1, wrong_type);
        float unchanged = -1.0f;
        EXPECT_FALSE(services.get_attribute(nullptr, false, object, OIIO::TypeFloat, attribute, &unchanged));
        globals.renderstate = nullptr;
        EXPECT_FALSE(services.get_attribute(&globals, false, object, OIIO::TypeFloat, attribute, &unchanged));
        EXPECT_EQ(-1.0f, unchanged);
    }

    struct IorClosure : CompositeClosure
    {
        IorClosure() = default;
        void finish() { compute_ior_cdf(); }
    };

    TEST_CASE(ClosureIor_ResolvesRelativeIndicesWithoutChangingAbsoluteEntries)
    {
        for (const float outside : {1.0f, 1.2f, 1.5f})
        {
            IorClosure closure;
            closure.add_ior(Color3f(1.0f), 1.4f);
            closure.add_ior(Color3f(3.0f), 1.5f, CompositeClosure::IorType::Relative);
            closure.add_ior(Color3f(2.0f), 1.1f);
            closure.finish();
            for (const bool entering : {false, true})
            {
                closure.set_ior_context(outside, entering);
                EXPECT_FEQ_EPS(1.4f, closure.choose_ior(0.01f), 1.0e-6f);
                EXPECT_FEQ_EPS(entering ? outside * 1.5f : outside / 1.5f,
                    closure.choose_ior(0.4f), 1.0e-6f);
                EXPECT_FEQ_EPS(1.1f, closure.choose_ior(0.9f), 1.0e-6f);
            }
        }
    }

    TEST_CASE(ClosureIor_RetainsVacuumFallbackAndChecksInvalidRelativeResults)
    {
        IorClosure fallback;
        fallback.finish();
        EXPECT_FEQ_EPS(1.0f, fallback.choose_ior(0.5f), 1.0e-6f);
        IorClosure relative;
        relative.add_ior(Color3f(1.0f), 2.0f, CompositeClosure::IorType::Relative);
        relative.finish();
        relative.set_ior_context(std::numeric_limits<float>::max(), true);
        EXPECT_EXCEPTION(ExceptionOSLRuntimeError, relative.choose_ior(0.5f));
        relative.set_ior_context(std::numeric_limits<float>::denorm_min(), false);
        EXPECT_EXCEPTION(ExceptionOSLRuntimeError, relative.choose_ior(0.5f));
        EXPECT_EXCEPTION(ExceptionOSLRuntimeError, relative.set_ior_context(0.0f, true));
        EXPECT_EXCEPTION(ExceptionOSLRuntimeError, relative.set_ior_context(std::numeric_limits<float>::quiet_NaN(), true));
    }

    TEST_CASE(SharedMicrofacetFresnel_NormalIncidenceRetainsTheTransmittedCosineAtExtremeIndices)
    {
        using Transmission = MicrofacetTransmissionHelper<GGXMDF>;
        constexpr double Tolerance = 8.0 * std::numeric_limits<float>::epsilon();
        for (const float eta : {std::numeric_limits<float>::min(), 1.0e-20f,
            0.75f, 1.0f, 1.5f, 1.0e20f, std::numeric_limits<float>::max()})
        for (const float side : {-1.0f, 1.0f})
        {
            float cosine;
            const float actual = Transmission::fresnel(side, eta, cosine);
            const double amplitude = (1.0 - double(eta)) / (1.0 + double(eta));
            EXPECT_TRUE(std::isfinite(actual));
            EXPECT_TRUE(std::abs(double(actual) - square(amplitude)) <= Tolerance);
            EXPECT_EQ(1.0f, cosine);
        }
    }

    TEST_CASE(SharedMicrofacetTransmission_MatchesIndependentValueAndDensity)
    {
        const Vector2f widths(0.19f, 0.37f);
        const auto check = [&](auto distribution, const bool beckmann)
        {
            using MDF = decltype(distribution);
            using Transmission = MicrofacetTransmissionHelper<MDF>;
            for (const float eta : {0.65f, 1.1f, 1.5f, 2.0f})
            {
                for (const float side : {-1.0f, 1.0f})
                {
                    const Vector3f wo = side * normalize(Vector3f(0.3f, 1.0f, 0.1f));
                    for (const float tilt : {-0.15f, 0.0f, 0.12f})
                    {
                        const Vector3f m = normalize(Vector3f(tilt, 1.0f, 0.08f));
                        const double cosine = dot(Vector3d(wo), Vector3d(m));
                        const double transmitted_squared = 1.0 - (1.0 - square(cosine)) / square(double(eta));
                        if (transmitted_squared <= 0.0)
                            continue;
                        const Vector3f wi(normalize((cosine / eta - std::copysign(std::sqrt(transmitted_squared), cosine)) *
                            Vector3d(m) - Vector3d(wo) / double(eta)));
                        if (wo.y * wi.y >= 0.0f)
                            continue;
                        const Vector3f half = Transmission::half_vector(wo, wi, eta);
                        EXPECT_TRUE(Transmission::valid_facet(wo, wi, half));
                        const float T = 1.0f - static_cast<float>(StandardMicrofacetReference::fresnel(
                            std::abs(dot(Vector3d(wo), Vector3d(half))), eta));
                        for (const bool adjoint : {false, true})
                        {
                            Spectrum value;
                            Transmission::evaluate(eta, Spectrum(1.0f), adjoint, wo, wi, half,
                                widths.x, widths.y, T, value);
                            const float pdf = Transmission::pdf(wo, wi, half, widths.x, widths.y, eta);
                            const Vector2d expected = StandardMicrofacetReference::transmission(wo, wi, widths, eta,
                                beckmann, adjoint);
                            EXPECT_TRUE(std::abs(expected.x - value[0]) <= 1.0e-4 * std::max(1.0, expected.x));
                            EXPECT_TRUE(std::abs(expected.y - pdf) <= 1.0e-4 * std::max(1.0, expected.y));
                        }
                    }
                }
            }
        };
        check(GGXMDF(), false);
        check(BeckmannMDF(), true);
    }

    TEST_CASE(SharedMicrofacetTransmission_RejectsFacetsThatDoNotCrossBothSides)
    {
        using Transmission = MicrofacetTransmissionHelper<GGXMDF>;
        const Vector3f wo = normalize(Vector3f(0.8f, 0.2f, 0.0f));
        const Vector3f wi = normalize(Vector3f(0.8f, -0.2f, 0.0f));
        EXPECT_TRUE(Transmission::valid_facet(wo, wi, Vector3f(0.0f, 1.0f, 0.0f)));
        EXPECT_FALSE(Transmission::valid_facet(wo, wi, normalize(Vector3f(1.0f, 0.1f, 0.0f))));
        EXPECT_FALSE(Transmission::valid_facet(wo, wi, normalize(Vector3f(-1.0f, 0.1f, 0.0f))));
    }

    TEST_CASE_F(StandardReflection_UsesFresnelForExplicitIorAndPreservesTheMirrorDefault, Fixture)
    {
        const float EnergyTolerance = 1.0e-6f;
        auto mirror = GlossyBRDFFactory().create("mirror_energy", ParamArray());
        for (const float ior : {0.0f, 0.5f, 1.0f, 1.5f, 2.0f})
        {
            tree = nullptr;
            add_closure(ReflectionID, 1.0f, basis.get_normal(), ior);
            Arena inputs;
            CompositeSurfaceClosure closure(basis, tree, inputs);
            // OSLBSDF::evaluate_inputs prepares each converted native leaf.
            mirror->prepare_inputs(inputs, point, closure.get_closure_input_values(0));
            for (const float cosine : {1.0f, 0.6f})
            {
                float expected = 1.0f;
                if (ior > 0.0f)
                {
                    const float sine_transmitted_squared = (1.0f - cosine * cosine) / (ior * ior);
                    if (sine_transmitted_squared < 1.0f)
                    {
                        const float transmitted_cosine = std::sqrt(1.0f - sine_transmitted_squared);
                        const float perpendicular = (cosine - ior * transmitted_cosine) /
                            (cosine + ior * transmitted_cosine);
                        const float parallel = (ior * cosine - transmitted_cosine) /
                            (ior * cosine + transmitted_cosine);
                        expected = 0.5f * (perpendicular * perpendicular + parallel * parallel);
                    }
                }
                const Vector3f outgoing(std::sqrt(1.0f - cosine * cosine), cosine, 0.0f);
                for (const bool adjoint : {false, true})
                {
                    BSDFSample result;
                    bsdf->sample(sampling, &closure, adjoint, false, geometry,
                        Dual3f(outgoing), ScatteringMode::All, result);
                    ASSERT_EQ(ScatteringMode::Specular, result.get_mode());
                    EXPECT_FEQ_EPS(Vector3f(-outgoing.x, outgoing.y, 0.0f),
                        result.m_incoming.get_value(), EnergyTolerance);
                    EXPECT_FEQ_EPS(expected, result.m_value.m_beauty[0] * cosine, EnergyTolerance);
                }
            }
        }
    }

    TEST_CASE(SharedMicrofacetTransmission_NearMatchedIndicesRetainTheirFiniteDensity)
    {
        const Vector3f wo(0.0f, 1.0f, 0.0f), wi(0.0f, -1.0f, 0.0f);
        const Vector2f widths(0.2f, 0.4f);
        const auto check = [&](auto distribution, const bool beckmann)
        {
            using Transmission = MicrofacetTransmissionHelper<decltype(distribution)>;
            for (const float eta : {std::nextafter(1.0f, 0.0f), std::nextafter(1.0f, 2.0f)})
            {
                for (const bool adjoint : {false, true})
                {
                    Spectrum value;
                    const Vector3f half = Transmission::half_vector(wo, wi, eta);
                    const float T = 1.0f - Transmission::fresnel(1.0f, eta);
                    Transmission::evaluate(eta, Spectrum(1.0f), adjoint, wo, wi, half,
                        widths.x, widths.y, T, value);
                    const float pdf = Transmission::pdf(wo, wi, half, widths.x, widths.y, eta);
                    const Vector2d expected = StandardMicrofacetReference::transmission(wo, wi, widths, eta,
                        beckmann, adjoint);
                    EXPECT_TRUE(std::isfinite(value[0]) && std::isfinite(pdf));
                    EXPECT_TRUE(std::abs(double(value[0]) / expected.x - 1.0) <= 1.0e-5);
                    EXPECT_TRUE(std::abs(double(pdf) / expected.y - 1.0) <= 1.0e-5);
                }
            }
        };
        check(GGXMDF(), false);
        check(BeckmannMDF(), true);
    }

    TEST_CASE_F(StandardMicrofacet_TransmissionAndCombinedEvaluationMatchIndependentReferences, Fixture)
    {
        auto leaf = OSLMicrofacetBSDFFactory::create("standard_transmission", ParamArray());
        const Vector2f widths(0.19f, 0.37f);
        const Vector3f wo = normalize(Vector3f(0.3f, 1.0f, 0.1f));
        const Vector3f half = normalize(Vector3f(0.12f, 1.0f, 0.08f));
        for (const char* distribution : {"ggx", "beckmann"})
        {
            for (const float eta : {0.65f, 1.1f, 1.5f, 2.0f})
            {
                const double cosine = dot(Vector3d(wo), Vector3d(half));
                const double transmitted_cosine = std::sqrt(1.0 - (1.0 - square(cosine)) / square(double(eta)));
                const Vector3f transmitted(normalize((cosine / eta - transmitted_cosine) * Vector3d(half) - Vector3d(wo) / double(eta)));
                const Vector3f reflected(normalize(2.0 * cosine * Vector3d(half) - Vector3d(wo)));
                for (const int mode : {1, 2})
                {
                    add_standard_microfacet(distribution, widths.x, widths.y, eta, mode);
                    Arena inputs;
                    CompositeSurfaceClosure closure(basis, tree, inputs);
                    auto* data = closure.get_closure_input_values(0);
                    leaf->prepare_inputs(inputs, point, data);
                    for (const bool adjoint : {false, true})
                    {
                        for (const bool reflection : {false, true})
                        {
                            const Vector3f wi = reflection ? reflected : transmitted;
                            const Vector3f outgoing = basis.transform_to_parent(wo);
                            const Vector3f incoming = basis.transform_to_parent(wi);
                            DirectShadingComponents value;
                            const float pdf = leaf->evaluate(data, adjoint, false, geometry, outgoing, incoming,
                                ScatteringMode::Glossy, value);
                            EXPECT_FEQ_EPS(pdf, leaf->evaluate_pdf(data, adjoint, geometry, outgoing, incoming,
                                ScatteringMode::Glossy), 1.0e-5f * std::max(1.0f, pdf));
                            if (reflection && mode == 1)
                            {
                                EXPECT_EQ(0.0f, pdf);
                                continue;
                            }
                            Vector2d expected = reflection ? StandardMicrofacetReference::evaluate(wo, wi, widths, eta,
                                std::string(distribution) == "beckmann") :
                                StandardMicrofacetReference::transmission(wo, wi, widths, eta,
                                    std::string(distribution) == "beckmann", adjoint);
                            const double F = StandardMicrofacetReference::fresnel(cosine, eta);
                            if (mode == 2)
                                expected.y *= reflection ? F : 1.0 - F;
                            EXPECT_TRUE(std::abs(expected.x - value.m_beauty[0]) <= 1.0e-4 * std::max(1.0, expected.x));
                            EXPECT_TRUE(std::abs(expected.y - pdf) <= 1.0e-4 * std::max(1.0, expected.y));
                        }
                    }
                }
            }
        }
    }

    TEST_CASE_F(StandardMicrofacet_SmoothCombinedSamplesUseFresnelProbabilitiesAndRadianceScaling, Fixture)
    {
        constexpr size_t SampleCount = 4096;
        auto leaf = OSLMicrofacetBSDFFactory::create("smooth_combined", ParamArray());
        for (const char* distribution : {"ggx", "beckmann"})
        {
            for (const float eta : {0.65f, 1.0f, 1.5f, 2.0f})
            {
                for (const int mode : {1, 2})
                {
                    add_standard_microfacet(distribution, 0.0f, 0.0f, eta, mode);
                    Arena inputs;
                    CompositeSurfaceClosure closure(basis, tree, inputs);
                    bsdf->prepare_inputs(inputs, point, &closure);
                    EXPECT_TRUE(bsdf->has_transmission(&closure));
                    EXPECT_TRUE(bsdf->has_interior(&closure));
                    EXPECT_FEQ_EPS(eta, bsdf->sample_ior(sampling, &closure), 1.0e-6f);
                    auto* data = closure.get_closure_input_values(0);
                    const double F = StandardMicrofacetReference::fresnel(1.0, eta);
                    size_t reflections = 0;
                    for (size_t i = 0; i < SampleCount; ++i)
                    {
                        BSDFSample sample;
                        leaf->sample(sampling, data, false, false, geometry, Dual3f(basis.get_normal()),
                            ScatteringMode::All, sample);
                        ASSERT_EQ(ScatteringMode::Specular, sample.get_mode());
                        EXPECT_EQ(BSDF::DiracDelta, sample.get_probability());
                        const bool reflection = sample.m_incoming.get_value().y > 0.0f;
                        reflections += reflection;
                        EXPECT_FEQ_EPS(reflection ? basis.get_normal() : -basis.get_normal(),
                            sample.m_incoming.get_value(), 1.0e-6f);
                        const double weight = reflection ? 1.0 : (mode == 2 ? 1.0 : 1.0 - F) / square(double(eta));
                        EXPECT_FEQ_EPS(weight, static_cast<double>(sample.m_value.m_beauty[0]), 1.0e-6 * std::max(1.0, weight));
                    }
                    if (mode == 1)
                        EXPECT_EQ(size_t(0), reflections);
                    else
                        EXPECT_TRUE(std::abs(F - double(reflections) / SampleCount) <= 0.015);
                }
            }
        }
    }

    TEST_CASE_F(StandardMicrofacet_MatchingRoughIndicesTransmitAsDeltaAndTirRejectsTransmissionOnly, Fixture)
    {
        auto leaf = OSLMicrofacetBSDFFactory::create("matched_and_tir", ParamArray());
        for (const int mode : {1, 2})
        {
            add_standard_microfacet("ggx", 0.2f, 0.3f, 1.0f, mode);
            Arena inputs;
            CompositeSurfaceClosure closure(basis, tree, inputs);
            auto* data = closure.get_closure_input_values(0);
            leaf->prepare_inputs(inputs, point, data);
            BSDFSample sample;
            leaf->sample(sampling, data, false, false, geometry, Dual3f(basis.get_normal()),
                ScatteringMode::Specular, sample);
            EXPECT_EQ(ScatteringMode::Specular, sample.get_mode());
            EXPECT_FEQ_EPS(-basis.get_normal(), sample.m_incoming.get_value(), 1.0e-6f);
            EXPECT_FEQ_EPS(1.0f, sample.m_value.m_beauty[0], 1.0e-6f);
            const Vector3f wo = normalize(Vector3f(0.9f, 0.3f, 0.0f));
            add_standard_microfacet("ggx", 0.0f, 0.0f, 0.65f, mode);
            CompositeSurfaceClosure tir(basis, tree, inputs);
            data = tir.get_closure_input_values(0);
            leaf->prepare_inputs(inputs, point, data);
            BSDFSample reflected;
            leaf->sample(sampling, data, false, false, geometry, Dual3f(wo), ScatteringMode::All, reflected);
            EXPECT_EQ(mode == 1 ? ScatteringMode::None : ScatteringMode::Specular, reflected.get_mode());
        }
    }

    TEST_CASE_F(StandardMicrofacet_PreservesRawWidthsAndDistribution, Fixture)
    {
        for (const char* distribution : {"beckmann", "ggx"})
        {
            for (const Vector2f alpha : {Vector2f(0.0f), Vector2f(0.0001f, 0.0002f), Vector2f(0.04f, 0.2f)})
            {
                add_standard_microfacet(distribution, alpha.x, alpha.y, 1.42f);
                Arena inputs;
                CompositeSurfaceClosure closure(basis, tree, inputs);
                ASSERT_EQ(size_t(1), closure.get_closure_count());
                EXPECT_EQ(OSLMicrofacetID, closure.get_closure_type(0));
                const auto& values = *static_cast<const OSLMicrofacetBSDFInputValues*>(closure.get_closure_input_values(0));
                EXPECT_EQ(alpha.x, values.m_alpha_x);
                EXPECT_EQ(alpha.y, values.m_alpha_y);
                EXPECT_EQ(1.42f, values.m_eta);
                const auto expected_distribution = std::strcmp(distribution, "ggx") == 0 ?
                    OSLMicrofacetDistribution::GGX : OSLMicrofacetDistribution::Beckmann;
                EXPECT_TRUE(values.m_distribution == expected_distribution);
            }
        }
    }

    TEST_CASE_F(StandardMicrofacet_RespectsRayRoughnessClamping, Fixture)
    {
        ShadingRay ray = point.get_ray();
        ray.m_min_roughness = 0.5f;
        ShadingPointBuilder(point).set_ray(ray);
        auto leaf = OSLMicrofacetBSDFFactory::create("standard_clamp", ParamArray());
        for (const Vector2f alpha : {Vector2f(0.0f), Vector2f(0.04f, 0.2f)})
        {
            add_standard_microfacet("ggx", alpha.x, alpha.y, 1.5f);
            Arena inputs;
            CompositeSurfaceClosure closure(basis, tree, inputs);
            void* data = closure.get_closure_input_values(0);
            leaf->prepare_inputs(inputs, point, data);
            const auto& values = *static_cast<const OSLMicrofacetBSDFInputValues*>(data);
            EXPECT_FEQ_EPS(0.25f, values.m_alpha_y, 1.0e-6f);
            EXPECT_FEQ_EPS(alpha.x == 0.0f ? 0.25f : 0.05f, values.m_alpha_x, 1.0e-6f);
        }
    }

    TEST_CASE_F(StandardMicrofacet_NormalIncidenceMatchesIndependentDensity, Fixture)
    {
        const float ValueTolerance = 2.0e-5f;
        const float AlphaX = 0.04f, AlphaY = 0.2f, Eta = 1.5f;
        const float normal_reflectance = square((Eta - 1.0f) / (Eta + 1.0f));
        const float density = 1.0f / (Pi<float>() * AlphaX * AlphaY);
        auto leaf = OSLMicrofacetBSDFFactory::create("standard_density", ParamArray());
        const Vector3f direction = basis.get_normal();
        for (const char* distribution : {"beckmann", "ggx"})
        {
            add_standard_microfacet(distribution, AlphaX, AlphaY, Eta);
            Arena inputs;
            CompositeSurfaceClosure closure(basis, tree, inputs);
            DirectShadingComponents value;
            const float probability = leaf->evaluate(closure.get_closure_input_values(0),
                false, false, geometry, direction, direction, ScatteringMode::All, value);
            EXPECT_FEQ_EPS(density / 4.0f, probability, ValueTolerance);
            EXPECT_FEQ_EPS(normal_reflectance * density / 4.0f, value.m_beauty[0], ValueTolerance);
            EXPECT_FEQ_EPS(probability, leaf->evaluate_pdf(closure.get_closure_input_values(0),
                false, geometry, direction, direction, ScatteringMode::All), ValueTolerance);
        }
    }

    TEST_CASE_F(StandardMicrofacet_ObliqueResponseMatchesIndependentDensityAndFresnel, Fixture)
    {
        const double AbsoluteTolerance = 1.0e-7;
        const double RelativeTolerance = 5.0e-5;
        auto leaf = OSLMicrofacetBSDFFactory::create("standard_oblique", ParamArray());
        for (const char* distribution : {"beckmann", "ggx"})
        for (const Vector2f widths : {Vector2f(0.04f, 0.2f), Vector2f(0.36f, 0.1f),
            Vector2f(0.1f, 0.36f), Vector2f(0.6f)})
        for (const float eta : {0.75f, 1.0f, 1.5f, 2.0f})
        {
            add_standard_microfacet(distribution, widths.x, widths.y, eta);
            Arena inputs;
            CompositeSurfaceClosure closure(basis, tree, inputs);
            const void* data = closure.get_closure_input_values(0);
            for (const Vector3f wo : {normalize(Vector3f(0.3f, 1.0f, 0.2f)),
                normalize(Vector3f(1.0f, 0.2f, -0.3f))})
            for (const Vector3f wi : {normalize(Vector3f(-0.25f, 1.0f, -0.18f)),
                normalize(Vector3f(-1.0f, 0.3f, 0.2f))})
            {
                const Vector2d expected = StandardMicrofacetReference::evaluate(wo, wi, widths,
                    eta, std::strcmp(distribution, "beckmann") == 0);
                const Vector3f outgoing = basis.transform_to_parent(wo);
                const Vector3f incoming = basis.transform_to_parent(wi);
                DirectShadingComponents value;
                const float pdf = leaf->evaluate(data, false, false, geometry,
                    outgoing, incoming, ScatteringMode::All, value);
                // A relative comparison alone cannot bound float Fresnel
                // cancellation when equal indices have zero reflectance.
                EXPECT_LT(AbsoluteTolerance + RelativeTolerance * std::abs(expected.x),
                    std::abs(double(value.m_beauty[0]) - expected.x));
                EXPECT_LT(AbsoluteTolerance + RelativeTolerance * std::abs(expected.y),
                    std::abs(double(pdf) - expected.y));
                const float queried_pdf = leaf->evaluate_pdf(data, false, geometry,
                    outgoing, incoming, ScatteringMode::All);
                EXPECT_LT(AbsoluteTolerance + RelativeTolerance * std::abs(expected.y),
                    std::abs(double(queried_pdf) - expected.y));
            }
        }
    }

    TEST_CASE_F(StandardMicrofacet_SamplesAgreeWithIndependentResponseAndDensity, Fixture)
    {
        const double AbsoluteTolerance = 1.0e-6;
        const double RelativeTolerance = 0.001;
        const size_t SampleCount = 64;
        auto leaf = OSLMicrofacetBSDFFactory::create("standard_samples", ParamArray());
        for (const char* distribution : {"beckmann", "ggx"})
        for (const Vector2f widths : {Vector2f(0.1f, 0.36f), Vector2f(0.6f)})
        for (const float eta : {0.75f, 1.5f})
        {
            add_standard_microfacet(distribution, widths.x, widths.y, eta);
            Arena inputs;
            CompositeSurfaceClosure closure(basis, tree, inputs);
            const void* data = closure.get_closure_input_values(0);
            for (const Vector3f wo : {normalize(Vector3f(0.3f, 1.0f, 0.2f)),
                normalize(Vector3f(1.0f, 0.2f, -0.3f))})
            {
                const Vector3f outgoing = basis.transform_to_parent(wo);
                size_t accepted = 0;
                for (size_t index = 0; index < SampleCount; ++index)
                {
                    BSDFSample result;
                    leaf->sample(sampling, data, false, false, geometry,
                        Dual3f(outgoing), ScatteringMode::All, result);
                    if (result.get_mode() == ScatteringMode::None)
                        continue;
                    ASSERT_EQ(ScatteringMode::Glossy, result.get_mode());
                    const Vector3f wi = basis.transform_to_local(result.m_incoming.get_value());
                    ASSERT_TRUE(wi.y > 0.0f);
                    const Vector2d expected = StandardMicrofacetReference::evaluate(wo, wi, widths,
                        eta, std::strcmp(distribution, "beckmann") == 0);
                    EXPECT_TRUE(std::isfinite(result.m_value.m_beauty[0]));
                    EXPECT_LT(AbsoluteTolerance + RelativeTolerance * std::abs(expected.x),
                        std::abs(double(result.m_value.m_beauty[0]) - expected.x));
                    EXPECT_LT(AbsoluteTolerance + RelativeTolerance * std::abs(expected.y),
                        std::abs(double(result.get_probability()) - expected.y));
                    ++accepted;
                }
                EXPECT_TRUE(accepted > 0);
            }
        }
    }

    TEST_CASE_F(StandardMicrofacet_DegenerateFiniteWidthAxesRemainFinite, Fixture)
    {
        const size_t SampleCount = 32;
        auto leaf = OSLMicrofacetBSDFFactory::create("standard_narrow_axes", ParamArray());
        for (const char* distribution : {"beckmann", "ggx"})
        for (const Vector2f widths : {Vector2f(0.0f, 0.2f), Vector2f(0.2f, 0.0f),
            Vector2f(1.0e-20f)})
        {
            add_standard_microfacet(distribution, widths.x, widths.y, 1.5f);
            Arena inputs;
            CompositeSurfaceClosure closure(basis, tree, inputs);
            const void* data = closure.get_closure_input_values(0);
            DirectShadingComponents peak;
            const float density = leaf->evaluate(data, false, false, geometry,
                basis.get_normal(), basis.get_normal(), ScatteringMode::All, peak);
            EXPECT_TRUE(std::isfinite(density) && density > 0.0f);
            EXPECT_TRUE(std::isfinite(peak.m_beauty[0]) && peak.m_beauty[0] > 0.0f);
            size_t accepted = 0;
            for (size_t index = 0; index < SampleCount; ++index)
            {
                BSDFSample result;
                leaf->sample(sampling, data, false, false, geometry,
                    Dual3f(basis.get_normal()), ScatteringMode::All, result);
                if (result.get_mode() == ScatteringMode::None)
                    continue;
                ASSERT_EQ(ScatteringMode::Glossy, result.get_mode());
                EXPECT_TRUE(std::isfinite(result.get_probability()) && result.get_probability() > 0.0f);
                EXPECT_TRUE(std::isfinite(result.m_value.m_beauty[0]) && result.m_value.m_beauty[0] >= 0.0f);
                EXPECT_TRUE(is_normalized(result.m_incoming.get_value()));
                ++accepted;
            }
            EXPECT_TRUE(accepted > 0);
        }
    }

    TEST_CASE_F(StandardMicrofacet_ZeroWidthsRetainIdealReflection, Fixture)
    {
        for (const char* distribution : {"beckmann", "ggx"})
        {
            add_standard_microfacet(distribution, 0.0f, 0.0f, 1.5f);
            BSDFSample result = sample();
            ASSERT_EQ(ScatteringMode::Specular, result.get_mode());
            EXPECT_FEQ_EPS(0.04f, result.m_value.m_beauty[0], 1.0e-6f);
            EXPECT_FEQ_EPS(basis.get_normal(), result.m_incoming.get_value(), 1.0e-6f);
        }
    }

    TEST_CASE_F(StandardMicrofacet_RejectsRatiosWithUnrepresentableReciprocals, Fixture)
    {
        for (const char* distribution : {"ggx", "beckmann"})
        for (const int mode : {0, 1, 2})
        {
            add_standard_microfacet(distribution, 0.2f, 0.2f,
                std::numeric_limits<float>::denorm_min(), mode);
            Arena inputs;
            EXPECT_EXCEPTION(ExceptionOSLRuntimeError, CompositeSurfaceClosure(basis, tree, inputs));
            for (const float eta : {std::numeric_limits<float>::min(), 1.0e-20f, 1.0e20f})
            {
                add_standard_microfacet(distribution, 0.2f, 0.2f, eta, mode);
                CompositeSurfaceClosure valid(basis, tree, inputs);
                EXPECT_EQ(std::size_t(1), valid.get_closure_count());
            }
        }
    }

    TEST_CASE_F(StandardMicrofacet_UnsupportedModesAndMalformedInputsFail, Fixture)
    {
        for (const int mode : {-1, 3})
        {
            auto* node = add_standard_microfacet("ggx", 0.1f, 0.2f, 1.5f);
            set_formal(node, 6, mode);
            EXPECT_EXCEPTION(ExceptionOSLRuntimeError, sample());
        }
        add_standard_microfacet("unknown", 0.1f, 0.2f, 1.5f);
        EXPECT_EXCEPTION(ExceptionOSLRuntimeError, sample());
        add_standard_microfacet("beckmann", -0.1f, 0.2f, 1.5f);
        EXPECT_EXCEPTION(ExceptionOSLRuntimeError, sample());
        add_standard_microfacet("ggx", 0.1f, 0.2f, 0.0f);
        EXPECT_EXCEPTION(ExceptionOSLRuntimeError, sample());
    }

    TEST_CASE_F(StandardReflection_PropagatesTheSelectedNormalFootprint, Fixture)
    {
        const float DirectionTolerance = 2.0e-6f;
        const Vector3f raw_normal(0.3f, 1.0f, -0.1f);
        const Vector3f dx(0.0004f, 0.0001f, -0.0002f);
        const Vector3f dy(-0.0003f, 0.0002f, 0.0005f);
        const Vector3f outgoing = normalize(Vector3f(0.1f, 1.0f, 0.2f));
        const Vector3f outgoing_dx(0.0001f, -0.00002f, -0.0001f);
        const Vector3f outgoing_dy(-0.0001f, 0.00003f, 0.0002f);
        auto mirror = GlossyBRDFFactory().create("mirror_footprint", ParamArray());
        const auto reflected = [](const Vector3f& n, const Vector3f& wo)
        {
            const Vector3f unit_normal = normalize(n);
            return 2.0f * dot(wo, unit_normal) * unit_normal - wo;
        };
        const Vector3f center = reflected(raw_normal, outgoing);
        const Vector3f expected_dx = center + 0.5f *
            (reflected(raw_normal + dx, outgoing + outgoing_dx) -
             reflected(raw_normal - dx, outgoing - outgoing_dx));
        const Vector3f expected_dy = center + 0.5f *
            (reflected(raw_normal + dy, outgoing + outgoing_dy) -
             reflected(raw_normal - dy, outgoing - outgoing_dy));
        for (const float scale : {1.0f, 3.0f})
        {
            for (const float ior : {0.0f, 1.5f})
            {
                tree = nullptr;
                auto* node = add_closure(ReflectionID, 1.0f, scale * raw_normal, ior);
                set_keyword(node, "normal_derivatives", 1);
                set_keyword(node, "dNdx", OSL::Vec3(scale * dx.x, scale * dx.y, scale * dx.z));
                set_keyword(node, "dNdy", OSL::Vec3(scale * dy.x, scale * dy.y, scale * dy.z));
                Arena inputs;
                CompositeSurfaceClosure closure(basis, tree, inputs);
                ASSERT_TRUE(closure.get_closure_normal_derivatives(0) != nullptr);
                mirror->prepare_inputs(inputs, point, closure.get_closure_input_values(0));
                for (const bool adjoint : {false, true})
                {
                    BSDFSample result;
                    bsdf->sample(sampling, &closure, adjoint, false, geometry,
                        Dual3f(outgoing, outgoing + outgoing_dx, outgoing + outgoing_dy),
                        ScatteringMode::All, result);
                    ASSERT_EQ(ScatteringMode::Specular, result.get_mode());
                    ASSERT_TRUE(result.m_incoming.has_derivatives());
                    EXPECT_FEQ_EPS(center, result.m_incoming.get_value(), DirectionTolerance);
                    EXPECT_FEQ_EPS(expected_dx, result.m_incoming.get_dx(), DirectionTolerance);
                    EXPECT_FEQ_EPS(expected_dy, result.m_incoming.get_dy(), DirectionTolerance);
                }
            }
        }
    }

    TEST_CASE_F(SelectedTangentDerivativesFollowTheFrameFallback, Fixture)
    {
        const Vector3f tangent_dx(0.2f, 0.1f, -0.3f);
        const Vector3f tangent_dy(-0.1f, 0.4f, 0.2f);
        for (const Vector3f normal : {Vector3f(0.0f), Vector3f(0.2f, 1.0f, 0.1f)})
        for (const Vector3f tangent : {Vector3f(0.0f), Vector3f(1.0f, 0.3f, 0.2f)})
        {
            tree = nullptr;
            auto* node = add_closure(GlossyID, 1.0f, normal);
            const OSL::ClosureParam* params = nullptr;
            ASSERT_TRUE(system->query_closure(nullptr, &node->id, &params));
            // The glossy closure's second formal parameter is its tangent.
            ASSERT_EQ(sizeof(OSL::Vec3), params[1].field_size);
            const OSL::Vec3 raw_tangent(tangent.x, tangent.y, tangent.z);
            std::memcpy(static_cast<char*>(node->data()) + params[1].offset,
                &raw_tangent, sizeof(raw_tangent));
            set_keyword(node, "normal_derivatives", 1);
            set_keyword(node, "dTdx", OSL::Vec3(tangent_dx.x, tangent_dx.y, tangent_dx.z));
            set_keyword(node, "dTdy", OSL::Vec3(tangent_dy.x, tangent_dy.y, tangent_dy.z));
            Arena inputs;
            CompositeSurfaceClosure closure(basis, tree, inputs);
            const auto& reference = closure.get_closure_basis_reference(0);
            const bool selected = square_norm(normal) != 0.0f && square_norm(tangent) != 0.0f;
            EXPECT_EQ(selected ? tangent : basis.get_tangent_u(), reference.m_tangent);
            EXPECT_EQ(selected ? tangent_dx : Vector3f(0.0f), reference.m_dx);
            EXPECT_EQ(selected ? tangent_dy : Vector3f(0.0f), reference.m_dy);
        }
    }

    TEST_CASE_F(SurfaceTangentDerivativesRespectTheReferenceSource, Fixture)
    {
        const Vector3f surface_dx(0.7f, 0.8f, 0.9f), surface_dy(-0.5f, 0.4f, 0.3f);
        const OSL::Vec3 explicit_dx(0.1f, 0.2f, 0.3f), explicit_dy(-0.2f, 0.3f, 0.4f);
        for (const bool valid_normal : {false, true})
        for (const bool valid_tangent : {false, true})
        {
            tree = nullptr;
            auto* node = add_closure(GlossyID, 1.0f, valid_normal ? basis.get_normal() : Vector3f(0.0f));
            const OSL::ClosureParam* params = nullptr;
            ASSERT_TRUE(system->query_closure(nullptr, &node->id, &params));
            const Vector3f tangent = valid_tangent ? basis.get_tangent_u() : Vector3f(0.0f);
            const OSL::Vec3 raw_tangent(tangent.x, tangent.y, tangent.z);
            std::memcpy(static_cast<char*>(node->data()) + params[1].offset, &raw_tangent, sizeof(raw_tangent));
            set_keyword(node, "normal_derivatives", 1);
            set_keyword(node, "dTdx", explicit_dx);
            set_keyword(node, "dTdy", explicit_dy);
            Arena inputs;
            CompositeSurfaceClosure closure(basis, tree, inputs);
            closure.set_surface_tangent_derivatives(surface_dx, surface_dy);
            const auto& reference = closure.get_closure_basis_reference(0);
            // Equal tangent values do not imply equal derivative sources.
            const bool explicit_reference = valid_normal && valid_tangent;
            EXPECT_EQ(explicit_reference ? Vector3f(explicit_dx) : surface_dx, reference.m_dx);
            EXPECT_EQ(explicit_reference ? Vector3f(explicit_dy) : surface_dy, reference.m_dy);
        }
    }

    TEST_CASE_F(StandardReflection_LeavesUnspecifiedNormalDerivativesOnTheLegacyPath, Fixture)
    {
        add_mirror(1.0f, basis.get_normal());
        Arena inputs;
        CompositeSurfaceClosure closure(basis, tree, inputs);
        EXPECT_TRUE(closure.get_closure_normal_derivatives(0) == nullptr);
    }

    TEST_CASE_F(ReflectiveAndEmptyClosuresHaveNoInterior, Fixture)
    {
        Arena inputs;
        CompositeSurfaceClosure empty(basis, nullptr, inputs);
        EXPECT_FALSE(bsdf->has_interior(&empty));
        EXPECT_FALSE(bsdf->has_transmission(&empty));
        add_diffuse(0.5f, basis.get_normal());
        add_mirror(0.5f, basis.get_normal());
        CompositeSurfaceClosure reflective(basis, tree, inputs);
        EXPECT_FALSE(bsdf->has_interior(&reflective));
        EXPECT_FALSE(bsdf->has_transmission(&reflective));
    }

    TEST_CASE_F(DiffuseTransmissionDoesNotDefineAnOpticalInterior, Fixture)
    {
        add_closure(TranslucentID, 1.0f, basis.get_normal());
        Arena inputs;
        CompositeSurfaceClosure closure(basis, tree, inputs);
        EXPECT_TRUE(bsdf->has_transmission(&closure));
        EXPECT_FALSE(bsdf->has_interior(&closure));
    }

    TEST_CASE_F(IdealReflectionHasNoContinuousDensity, Fixture)
    {
        add_mirror(0.5f, basis.get_normal());
        Arena inputs;
        CompositeSurfaceClosure closure(basis, tree, inputs);
        bsdf->prepare_inputs(inputs, point, &closure);
        DirectShadingComponents value;
        EXPECT_EQ(0.0f, bsdf->evaluate(&closure, false, false, geometry,
            basis.get_normal(), basis.get_normal(), ScatteringMode::All, value));
        EXPECT_EQ(0.0f, bsdf->evaluate_pdf(&closure, false, geometry,
            basis.get_normal(), basis.get_normal(), ScatteringMode::All));
    }

    TEST_CASE_F(SmoothMetalHasNoContinuousDensity, Fixture)
    {
        auto metal = MetalBRDFFactory().create("smooth_metal", ParamArray());
        MetalBRDFInputValues values{};
        values.m_normal_reflectance.set(0.5f);
        values.m_reflectance_multiplier = 1.0f;
        values.m_precomputed.m_a.set(0.0f);
        DirectShadingComponents value;
        EXPECT_EQ(0.0f, metal->evaluate(&values, false, false, geometry,
            basis.get_normal(), basis.get_normal(), ScatteringMode::All, value));
        EXPECT_EQ(0.0f, metal->evaluate_pdf(&values, false, geometry,
            basis.get_normal(), basis.get_normal(), ScatteringMode::All));
    }

    TEST_CASE_F(DeltaMixture_PreservesTotalReflectionForEitherClosure, Fixture)
    {
        const Vector3f tilted(0.6f, 0.8f, 0.0f);
        add_mirror(0.2f, basis.get_normal());
        add_mirror(0.3f, tilted);
        size_t straight = 0, angled = 0;
        const size_t SampleCount = 64;
        for (size_t i = 0; i < SampleCount; ++i)
        {
            const BSDFSample result = sample();
            ASSERT_EQ(ScatteringMode::Specular, result.get_mode());
            EXPECT_EQ(BSDF::DiracDelta, result.get_probability());
            const Vector3f& incoming = result.m_incoming.get_value();
            const Vector3f& normal = incoming.x > 0.5f ? tilted : basis.get_normal();
            const float cosine = std::abs(dot(incoming, normal));
            EXPECT_FEQ_EPS(0.5f, result.m_value.m_beauty[0] * cosine, 1.0e-6f);
            if (incoming.x > 0.5f) ++angled;
            else ++straight;
        }
        EXPECT_TRUE(straight > 0);
        EXPECT_TRUE(angled > 0);
    }

    TEST_CASE_F(ModeMask_SelectsEligibleClosureAfterAnExcludedOne, Fixture)
    {
        add_diffuse(0.2f, basis.get_normal());
        add_mirror(0.3f, basis.get_normal());
        const BSDFSample result = sample(ScatteringMode::Specular);
        ASSERT_EQ(ScatteringMode::Specular, result.get_mode());
        EXPECT_FEQ_EPS(0.3f, result.m_value.m_beauty[0], 1.0e-6f);
    }

    TEST_CASE_F(ModeMask_SelectsDiffuseAfterAnExcludedMirror, Fixture)
    {
        add_mirror(0.2f, basis.get_normal());
        add_diffuse(0.3f, basis.get_normal());
        const BSDFSample result = sample(ScatteringMode::Diffuse);
        ASSERT_EQ(ScatteringMode::Diffuse, result.get_mode());
        EXPECT_FEQ_EPS(0.3f * RcpPi<float>(), result.m_value.m_beauty[0], 1.0e-6f);
    }

    TEST_CASE_F(ContinuousMixture_UsesEachClosuresShadingFrame, Fixture)
    {
        const Vector3f tilted(0.6f, 0.8f, 0.0f);
        const float first_weight = 0.2f, second_weight = 0.3f;
        add_diffuse(first_weight, basis.get_normal());
        add_diffuse(second_weight, tilted);
        const size_t SampleCount = 32;
        for (size_t i = 0; i < SampleCount; ++i)
        {
            const BSDFSample result = sample(ScatteringMode::Diffuse);
            ASSERT_EQ(ScatteringMode::Diffuse, result.get_mode());
            const Vector3f& incoming = result.m_incoming.get_value();
            const float first_cos = std::max(dot(incoming, basis.get_normal()), 0.0f);
            const float second_cos = std::max(dot(incoming, tilted), 0.0f);
            const float expected_pdf = (first_weight * first_cos + second_weight * second_cos)
                * RcpPi<float>() / (first_weight + second_weight);
            const float expected_value = ((first_cos > 0.0f ? first_weight : 0.0f) +
                (second_cos > 0.0f ? second_weight : 0.0f)) * RcpPi<float>();
            EXPECT_FEQ_EPS(expected_pdf, result.get_probability(), 1.0e-6f);
            EXPECT_FEQ_EPS(expected_value, result.m_value.m_beauty[0], 1.0e-6f);
            Arena inputs;
            CompositeSurfaceClosure mixture(basis, tree, inputs);
            DirectShadingComponents evaluated;
            const float evaluated_pdf = bsdf->evaluate(&mixture, false, false,
                geometry, basis.get_normal(), incoming, ScatteringMode::Diffuse, evaluated);
            EXPECT_FEQ_EPS(expected_pdf, evaluated_pdf, 1.0e-6f);
            EXPECT_FEQ_EPS(expected_value, evaluated.m_beauty[0], 1.0e-6f);
            EXPECT_FEQ_EPS(expected_pdf, bsdf->evaluate_pdf(&mixture, false,
                geometry, basis.get_normal(), incoming, ScatteringMode::Diffuse), 1.0e-6f);
        }
    }

    TEST_CASE_F(DiffuseAndMirrorMixture_PreservesEnergyForEitherSamplingMode, Fixture)
    {
        add_diffuse(0.2f, basis.get_normal());
        add_mirror(0.3f, basis.get_normal());
        size_t diffuse = 0, specular = 0;
        const size_t SampleCount = 64;
        for (size_t i = 0; i < SampleCount; ++i)
        {
            const BSDFSample result = sample();
            ASSERT_TRUE(result.get_mode() != ScatteringMode::None);
            const float cosine = dot(result.m_incoming.get_value(), basis.get_normal());
            const float probability = result.get_probability() == BSDF::DiracDelta ?
                1.0f : result.get_probability();
            EXPECT_FEQ_EPS(0.5f, result.m_value.m_beauty[0] * cosine / probability, 1.0e-6f);
            if (result.get_mode() == ScatteringMode::Diffuse) ++diffuse;
            else ++specular;
        }
        EXPECT_TRUE(diffuse > 0);
        EXPECT_TRUE(specular > 0);
    }

    TEST_CASE_F(MaximumClosureCount_CanBeSampledAndRejectsAnExtraClosure, Fixture)
    {
        const float weight = 1.0f / CompositeSurfaceClosure::MaxClosureEntries;
        for (size_t i = 0; i < CompositeSurfaceClosure::MaxClosureEntries; ++i)
            add_mirror(weight, basis.get_normal());
        const BSDFSample result = sample();
        ASSERT_EQ(ScatteringMode::Specular, result.get_mode());
        EXPECT_FEQ_EPS(1.0f, result.m_value.m_beauty[0], 1.0e-6f);
        add_mirror(weight, basis.get_normal());
        EXPECT_EXCEPTION(ExceptionOSLRuntimeError, sample());
    }

    TEST_CASE_F(FilteredMixture_RoundedUpperEndpointStaysInAnEligibleClosure, Fixture)
    {
        // Normalizing these weights rounds the sum below the largest float
        // less than one.  The excluded trailing mirror must remain excluded.
        add_diffuse(1.0f, basis.get_normal());
        add_diffuse(17.0f, basis.get_normal());
        add_diffuse(13.0f, basis.get_normal());
        add_mirror(1.0f, basis.get_normal());
        Arena inputs;
        CompositeSurfaceClosure mixture(basis, tree, inputs);
        float pdfs[CompositeSurfaceClosure::MaxClosureEntries];
        EXPECT_EQ(3, mixture.compute_pdfs(ScatteringMode::Diffuse, pdfs));
        EXPECT_EQ(2, mixture.choose_closure(std::nextafter(1.0f, 0.0f),
            mixture.get_closure_count(), pdfs));
    }

    TEST_CASE_F(ZeroProposalWeightHasNoSamplingDistribution, Fixture)
    {
        add_mirror(0.5f, basis.get_normal());
        Arena inputs;
        CompositeSurfaceClosure mixture(basis, tree, inputs);
        mixture.override_closure_scalar_weight(0.0f);
        float pdfs[CompositeSurfaceClosure::MaxClosureEntries];
        EXPECT_EQ(0, mixture.compute_pdfs(ScatteringMode::All, pdfs));
        BSDFSample result;
        bsdf->sample(sampling, &mixture, false, false, geometry,
            Dual3f(basis.get_normal()), ScatteringMode::All, result);
        EXPECT_EQ(ScatteringMode::None, result.get_mode());
    }

    TEST_CASE_F(EmptyOrExcludedMixturesHaveNoScattering, Fixture)
    {
        EXPECT_EQ(ScatteringMode::None, sample().get_mode());
        add_diffuse(0.5f, basis.get_normal());
        EXPECT_EQ(ScatteringMode::None, sample(ScatteringMode::Specular).get_mode());
        add_mirror(0.5f, basis.get_normal());
        EXPECT_EQ(ScatteringMode::None, sample(ScatteringMode::Glossy).get_mode());
    }
}
