# This source file is part of appleseed.
# Visit https://appleseedhq.net/ for additional information and resources.
#
# This software is released under the MIT license.
#
# Copyright (c) 2010-2013 Francois Beaune, Jupiter Jazz Limited
# Copyright (c) 2014-2019 Francois Beaune, The appleseedhq Organization
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/include/appleseed/foundation/core)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/appleseed/foundation/core/version.h.in
  ${PROJECT_BINARY_DIR}/include/appleseed/foundation/core/version.h
  )

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/appleseed/foundation/core/buildoptions.h.in
  ${PROJECT_BINARY_DIR}/include/appleseed/foundation/core/buildoptions.h
  )

configure_file (
  ${PROJECT_SOURCE_DIR}/include/appleseed/shaders/as_osl_extensions.h.in
  ${PROJECT_BINARY_DIR}/include/appleseed/shaders/as_osl_extensions.h
  )

#--------------------------------------------------------------------------------------------------
# Group header files.
#--------------------------------------------------------------------------------------------------

set (foundation_array_sources
    foundation/array/algorithm.h
    foundation/array/applyvisitor.h
    foundation/array/array.h
    foundation/array/arrayallocator.h
    foundation/array/arrayref.h
    foundation/array/arraytraits.h
    foundation/array/arrayview.h
    foundation/array/exception.h
    foundation/array/keyframedarray.h
)
source_group ("foundation\\array" FILES
    ${foundation_array_sources}
)

set (foundation_core_concepts_sources
    foundation/core/concepts/iunknown.h
    foundation/core/concepts/noncopyable.h
    foundation/core/concepts/singleton.h
)
source_group ("foundation\\core\\concepts" FILES
    ${foundation_core_concepts_sources}
)

set (foundation_containers_sources
    foundation/containers/alignedvector.h
    foundation/containers/dictionary.h
    foundation/containers/hashtable.h
)
source_group ("foundation\\containers" FILES
    ${foundation_containers_sources}
)

set (foundation_core_exceptions_sources
    foundation/core/exceptions/exception.h
    foundation/core/exceptions/exceptionioerror.h
    foundation/core/exceptions/exceptionnotimplemented.h
    foundation/core/exceptions/exceptionunsupportedfileformat.h
    foundation/core/exceptions/stringexception.h
)
source_group ("foundation\\core\\exceptions" FILES
    ${foundation_core_exceptions_sources}
)

set (foundation_core_sources
    foundation/core/appleseed.h
    foundation/core/buildoptions.h.in
    foundation/core/thirdparties.h
    foundation/core/version.h.in
)
source_group ("foundation\\core" FILES
    ${foundation_core_sources}
)

if (WITH_GPU)
    set (foundation_cuda_sources
        foundation/cuda/cudadevice.h
        foundation/cuda/exception.h
        foundation/cuda/memory.h
        foundation/cuda/module.h
    )
    source_group ("foundation\\cuda" FILES
        ${foundation_cuda_sources}
    )
endif ()

set (foundation_curve_sources
    foundation/curve/binarycurvefilereader.h
    foundation/curve/binarycurvefilewriter.h
    foundation/curve/curvebasis.h
    foundation/curve/genericcurvefilereader.h
    foundation/curve/genericcurvefilewriter.h
    foundation/curve/icurvebuilder.h
    foundation/curve/icurvefilereader.h
    foundation/curve/icurvefilewriter.h
    foundation/curve/icurvewalker.h
    foundation/curve/mitshairfilereader.h
)
source_group ("foundation\\curve" FILES
    ${foundation_curve_sources}
)

set (foundation_hash_sources
    foundation/hash/hash.h
    foundation/hash/murmurhash.h
    foundation/hash/siphash.h
)
source_group ("foundation\\hash" FILES
    ${foundation_hash_sources}
)

set (foundation_image_text_sources
    foundation/image/text/stb_truetype.h
    foundation/image/text/textrenderer.h
)
source_group ("foundation\\image\\text" FILES
    ${foundation_image_text_sources}
)

set (foundation_image_sources
    foundation/image/accumulatortile.h
    foundation/image/analysis.h
    foundation/image/canvasproperties.h
    foundation/image/color.h
    foundation/image/colormap.h
    foundation/image/colormapdata.h
    foundation/image/colorspace.h
    foundation/image/conversion.h
    foundation/image/drawing.h
    foundation/image/exceptionunsupportedimageformat.h
    foundation/image/genericimagefilereader.h
    foundation/image/genericimagefilewriter.h
    foundation/image/genericprogressiveimagefilereader.h
    foundation/image/icanvas.h
    foundation/image/iimagefilereader.h
    foundation/image/iimagefilewriter.h
    foundation/image/image.h
    foundation/image/imageattributes.h
    foundation/image/iprogressiveimagefilereader.h
    foundation/image/nativedrawing.h
    foundation/image/pixel.h
    foundation/image/regularspectrum.h
    foundation/image/tile.h
)
source_group ("foundation\\image" FILES
    ${foundation_image_sources}
)

set (foundation_log_sources
    foundation/log/consolelogtarget.h
    foundation/log/filelogtarget.h
    foundation/log/filelogtargetbase.h
    foundation/log/helpers.h
    foundation/log/ilogtarget.h
    foundation/log/log.h
    foundation/log/logformatter.h
    foundation/log/logger.h
    foundation/log/logmessage.h
    foundation/log/openfilelogtarget.h
    foundation/log/stringlogtarget.h
)
source_group ("foundation\\log" FILES
    ${foundation_log_sources}
)

set (foundation_math_bsp_sources
    foundation/math/bsp/bsp_builder.h
    foundation/math/bsp/bsp_intersector.h
    foundation/math/bsp/bsp_node.h
    foundation/math/bsp/bsp_statistics.h
    foundation/math/bsp/bsp_tree.h
)
source_group ("foundation\\math\\bsp" FILES
    ${foundation_math_bsp_sources}
)

set (foundation_math_bvh_sources
    foundation/math/bvh/bvh_bboxsortpredicate.h
    foundation/math/bvh/bvh_builder.h
    foundation/math/bvh/bvh_intersector.h
    foundation/math/bvh/bvh_medianpartitioner.h
    foundation/math/bvh/bvh_middlepartitioner.h
    foundation/math/bvh/bvh_node.h
    foundation/math/bvh/bvh_partitionerbase.h
    foundation/math/bvh/bvh_sahpartitioner.h
    foundation/math/bvh/bvh_sbvhpartitioner.h
    foundation/math/bvh/bvh_spatialbuilder.h
    foundation/math/bvh/bvh_statistics.h
    foundation/math/bvh/bvh_tree.h
)
source_group ("foundation\\math\\bvh" FILES
    ${foundation_math_bvh_sources}
)

set (foundation_math_intersection_sources
    foundation/math/intersection/aabbtriangle.h
    foundation/math/intersection/frustumaabb.h
    foundation/math/intersection/frustumsegment.h
    foundation/math/intersection/planesegment.h
    foundation/math/intersection/rayaabb.h
    foundation/math/intersection/raydisk.h
    foundation/math/intersection/rayparallelogram.h
    foundation/math/intersection/rayplane.h
    foundation/math/intersection/raysphere.h
    foundation/math/intersection/raytrianglehh.h
    foundation/math/intersection/raytrianglemt.h
    foundation/math/intersection/raytrianglessk.h
)
source_group ("foundation\\math\\intersection" FILES
    ${foundation_math_intersection_sources}
)

set (foundation_math_knn_sources
    foundation/math/knn/knn_anyquery.h
    foundation/math/knn/knn_answer.h
    foundation/math/knn/knn_builder.h
    foundation/math/knn/knn_node.h
    foundation/math/knn/knn_query.h
    foundation/math/knn/knn_statistics.h
    foundation/math/knn/knn_tree.h
)
source_group ("foundation\\math\\knn" FILES
    ${foundation_math_knn_sources}
)

set (foundation_math_rng_sources
    foundation/math/rng/distribution.h
    foundation/math/rng/lcg.h
    foundation/math/rng/mersennetwister.h
    foundation/math/rng/pcg.h
    foundation/math/rng/serialmersennetwister.h
    foundation/math/rng/xoroshiro128plus.h
    foundation/math/rng/xorshift32.h
    foundation/math/rng/xorshift64.h
)
if (USE_SSE)
    list (APPEND foundation_math_rng_sources
        foundation/math/rng/simdmersennetwister.h
    )
endif ()
source_group ("foundation\\math\\rng" FILES
    ${foundation_math_rng_sources}
)

set (foundation_math_sampling_sources
    foundation/math/sampling/imageimportancesampler.h
    foundation/math/sampling/mappings.h
    foundation/math/sampling/qmcsamplingcontext.h
    foundation/math/sampling/rngsamplingcontext.h
    foundation/math/sampling/sphericalimportancesampler.h
    foundation/math/sampling/sphericalrectanglesampler.h
    foundation/math/sampling/sphericaltrianglesampler.h
)
source_group ("foundation\\math\\sampling" FILES
    ${foundation_math_sampling_sources}
)

set (foundation_math_voxel_sources
    foundation/math/voxel/voxel_builder.h
    foundation/math/voxel/voxel_intersector.h
    foundation/math/voxel/voxel_node.h
    foundation/math/voxel/voxel_statistics.h
    foundation/math/voxel/voxel_tree.h
)
source_group ("foundation\\math\\voxel" FILES
    ${foundation_math_voxel_sources}
)

set (foundation_math_sources
    foundation/math/aabb.h
    foundation/math/area.h
    foundation/math/basis.h
    foundation/math/bezier.h
    foundation/math/beziercurve.h
    foundation/math/bsp.h
    foundation/math/bvh.h
    foundation/math/cdf.h
    foundation/math/combination.h
    foundation/math/compressedunitvector.h
    foundation/math/distance.h
    foundation/math/dual.h
    foundation/math/fastmath.h
    foundation/math/filter.h
    foundation/math/filtersamplingtable.h
    foundation/math/fp.h
    foundation/math/fresnel.h
    foundation/math/frustum.h
    foundation/math/half.h
    foundation/math/knn.h
    foundation/math/matrix.h
    foundation/math/microfacet.h
    foundation/math/minmax.h
    foundation/math/mis.h
    foundation/math/noise.h
    foundation/math/ordering.h
    foundation/math/permutation.h
    foundation/math/phasefunction.h
    foundation/math/population.h
    foundation/math/primes.h
    foundation/math/qmc.h
    foundation/math/quaternion.h
    foundation/math/ray.h
    foundation/math/root.h
    foundation/math/rr.h
    foundation/math/sah.h
    foundation/math/scalar.h
    foundation/math/specialfunctions.h
    foundation/math/sphericaltriangle.h
    foundation/math/spline.h
    foundation/math/split.h
    foundation/math/transform.h
    foundation/math/treeoptimizer.h
    foundation/math/triangulator.h
    foundation/math/vector.h
    foundation/math/voxel.h
    foundation/math/voxelgrid.h
)
source_group ("foundation\\math" FILES
    ${foundation_math_sources}
)

set (foundation_memory_sources
    foundation/memory/alignedallocator.h
    foundation/memory/arena.h
    foundation/memory/autoreleaseptr.h
    foundation/memory/copyonwrite.h
    foundation/memory/memory.h
    foundation/memory/poolallocator.h
    foundation/memory/stampedptr.h
)
source_group ("foundation\\memory" FILES
    ${foundation_memory_sources}
)

set (foundation_meshio_sources
    foundation/meshio/binarymeshfilereader.h
    foundation/meshio/binarymeshfilewriter.h
    foundation/meshio/genericmeshfilereader.h
    foundation/meshio/genericmeshfilewriter.h
    foundation/meshio/imeshbuilder.h
    foundation/meshio/imeshfilereader.h
    foundation/meshio/imeshfilewriter.h
    foundation/meshio/imeshwalker.h
    foundation/meshio/meshbuilderbase.h
    foundation/meshio/objmeshfilelexer.h
    foundation/meshio/objmeshfilereader.h
    foundation/meshio/objmeshfilewriter.h
)
source_group ("foundation\\meshio" FILES
    ${foundation_meshio_sources}
)

set (foundation_meta_benchmarks_sources
)
source_group ("foundation\\meta\\benchmarks" FILES
    ${foundation_meta_benchmarks_sources}
)

set (foundation_meta_tests_sources
)
source_group ("foundation\\meta\\tests" FILES
    ${foundation_meta_tests_sources}
)

set (foundation_platform_snprintf_sources
)
source_group ("foundation\\platform\\snprintf" FILES
    ${foundation_platform_snprintf_sources}
)

set (foundation_platform_sources
    foundation/platform/_beginexrheaders.h
    foundation/platform/_beginoiioheaders.h
    foundation/platform/_beginoslheaders.h
    foundation/platform/_beginstbtruetypeheaders.h
    foundation/platform/_endexrheaders.h
    foundation/platform/_endoiioheaders.h
    foundation/platform/_endoslheaders.h
    foundation/platform/_endstbtruetypeheaders.h
    foundation/platform/arch.h
    foundation/platform/atomic.h
    foundation/platform/compiler.h
    foundation/platform/console.h
    foundation/platform/datetime.h
    foundation/platform/debugger.h
    foundation/platform/defaulttimers.h
    foundation/platform/path.h
    foundation/platform/python.h
    foundation/platform/sharedlibrary.h
    foundation/platform/snprintf.h
    foundation/platform/sse.h
    foundation/platform/system.h
    foundation/platform/thread.h
    foundation/platform/timers.h
    foundation/platform/types.h
    foundation/platform/win32stackwalker.h
    foundation/platform/windows.h
)
if ("${CPU_TARGET}" MATCHES "x86")
    list (APPEND foundation_platform_sources
        foundation/platform/x86timer.h
    )
endif ()
source_group ("foundation\\platform" FILES
    ${foundation_platform_sources}
)

set (foundation_resources_fonts_sources
    foundation/resources/fonts/Ubuntu-L.ttf.h
    foundation/resources/fonts/Ubuntu-M.ttf.h
)
source_group ("foundation\\resources\\fonts" FILES
    ${foundation_resources_fonts_sources}
)

set (foundation_resources_logo_sources
    foundation/resources/logo/appleseed-seeds-16.h
    foundation/resources/logo/appleseed-seeds-32.h
    foundation/resources/logo/appleseed-seeds-64.h
    foundation/resources/logo/appleseed-seeds-256.h
)
source_group ("foundation\\resources\\logo" FILES
    ${foundation_resources_logo_sources}
)

set (foundation_string_sources
    foundation/string/internedstring.h
    foundation/string/string.h
)
source_group ("foundation\\string" FILES
    ${foundation_string_sources}
)

set (foundation_utility_api_sources
    foundation/utility/api/apiarray.h
    foundation/utility/api/apistring.h
    foundation/utility/api/apistringpair.h
    foundation/utility/api/specializedapiarrays.h
)
source_group ("foundation\\utility\\api" FILES
    ${foundation_utility_api_sources}
)

set (foundation_utility_benchmark_sources
    foundation/utility/benchmark/benchmarkaggregator.h
    foundation/utility/benchmark/benchmarkdatapoint.h
    foundation/utility/benchmark/benchmarklistenerbase.h
    foundation/utility/benchmark/benchmarkresult.h
    foundation/utility/benchmark/benchmarkseries.h
    foundation/utility/benchmark/benchmarksuite.h
    foundation/utility/benchmark/benchmarksuiterepository.h
    foundation/utility/benchmark/helpers.h
    foundation/utility/benchmark/ibenchmarkcase.h
    foundation/utility/benchmark/ibenchmarkcasefactory.h
    foundation/utility/benchmark/ibenchmarklistener.h
    foundation/utility/benchmark/loggerbenchmarklistener.h
    foundation/utility/benchmark/timingresult.h
    foundation/utility/benchmark/xmlfilebenchmarklistener.h
)
source_group ("foundation\\utility\\benchmark" FILES
    ${foundation_utility_benchmark_sources}
)

set (foundation_utility_commandlineparser_sources
    foundation/utility/commandlineparser/commandlineparser.h
    foundation/utility/commandlineparser/flagoptionhandler.h
    foundation/utility/commandlineparser/messagelist.h
    foundation/utility/commandlineparser/optionhandler.h
    foundation/utility/commandlineparser/parseresults.h
    foundation/utility/commandlineparser/valueoptionhandler.h
)
source_group ("foundation\\utility\\commandlineparser" FILES
    ${foundation_utility_commandlineparser_sources}
)

set (foundation_utility_filter_sources
    foundation/utility/filter/ifilter.h
    foundation/utility/filter/passthroughfilter.h
    foundation/utility/filter/regexfilter.h
    foundation/utility/filter/rejectallfilter.h
)
source_group ("foundation\\utility\\filter" FILES
    ${foundation_utility_filter_sources}
)

set (foundation_utility_job_sources
    foundation/utility/job/abortswitch.h
    foundation/utility/job/iabortswitch.h
    foundation/utility/job/ijob.h
    foundation/utility/job/jobmanager.h
    foundation/utility/job/jobqueue.h
    foundation/utility/job/workerthread.h
)
source_group ("foundation\\utility\\job" FILES
    ${foundation_utility_job_sources}
)

set (foundation_utility_minizip_sources
    foundation/utility/minizip/crypt.h
    foundation/utility/minizip/ioapi.c
    foundation/utility/minizip/ioapi.h
    foundation/utility/minizip/unzip.c
    foundation/utility/minizip/unzip.h
    foundation/utility/minizip/zip.c
    foundation/utility/minizip/zip.h
)
source_group ("foundation\\utility\\minizip" FILES
    ${foundation_utility_minizip_sources}
)

set (foundation_utility_settings_sources
    foundation/utility/settings/settings.xsd
    foundation/utility/settings/settingsfilereader.h
    foundation/utility/settings/settingsfilewriter.h
)
source_group ("foundation\\utility\\settings" FILES
    ${foundation_utility_settings_sources}
)

set (foundation_utility_test_sources
    foundation/utility/test/assertions.h
    foundation/utility/test/exceptionassertionfailure.h
    foundation/utility/test/globaltestsuite.h
    foundation/utility/test/helpers.h
    foundation/utility/test/itestcase.h
    foundation/utility/test/itestcasefactory.h
    foundation/utility/test/itestlistener.h
    foundation/utility/test/loggertestlistener.h
    foundation/utility/test/testlistenerbase.h
    foundation/utility/test/testlistenercollection.h
    foundation/utility/test/testlistenerhelper.h
    foundation/utility/test/testmessage.h
    foundation/utility/test/testresult.h
    foundation/utility/test/testsuite.h
    foundation/utility/test/testsuiterepository.h
)
source_group ("foundation\\utility\\test" FILES
    ${foundation_utility_test_sources}
)

set (foundation_utility_ui_sources
    foundation/utility/ui/cameracontroller.h
)
source_group ("foundation\\utility\\ui" FILES
    ${foundation_utility_ui_sources}
)

set (foundation_utility_sources
    foundation/utility/attributeset.h
    foundation/utility/benchmark.h
    foundation/utility/bitmask.h
    foundation/utility/bufferedfile.h
    foundation/utility/cache.h
    foundation/utility/casts.h
    foundation/utility/cc.h
    foundation/utility/commandlineparser.h
    foundation/utility/countof.h
    foundation/utility/filter.h
    foundation/utility/foreach.h
    foundation/utility/gnuplotfile.h
    foundation/utility/iesparser.h
    foundation/utility/indenter.h
    foundation/utility/iostreamop.h
    foundation/utility/iterators.h
    foundation/utility/job.h
    foundation/utility/kvpair.h
    foundation/utility/lazy.h
    foundation/utility/makevector.h
    foundation/utility/numerictype.h
    foundation/utility/otherwise.h
    foundation/utility/poison.h
    foundation/utility/preprocessor.h
    foundation/utility/registrar.h
    foundation/utility/searchpaths.h
    foundation/utility/settings.h
    foundation/utility/statistics.h
    foundation/utility/stopwatch.h
    foundation/utility/tea.h
    foundation/utility/test.h
    foundation/utility/testutils.h
    foundation/utility/tls.h
    foundation/utility/typetraits.h
    foundation/utility/uid.h
    foundation/utility/version.h
    foundation/utility/vpythonfile.h
    foundation/utility/xercesc.h
    foundation/utility/xmlelement.h
    foundation/utility/z85.h
    foundation/utility/zip.h
)
source_group ("foundation\\utility" FILES
    ${foundation_utility_sources}
)

set (main_sources
    main/allocator.h
    main/dllsymbol.h
    main/dllvisibility.h
)
source_group ("main" FILES
    ${main_sources}
)

set (renderer_api_sources
    renderer/api/aov.h
    renderer/api/bsdf.h
    renderer/api/bssrdf.h
    renderer/api/camera.h
    renderer/api/color.h
    renderer/api/display.h
    renderer/api/edf.h
    renderer/api/entity.h
    renderer/api/environment.h
    renderer/api/environmentedf.h
    renderer/api/environmentshader.h
    renderer/api/frame.h
    renderer/api/light.h
    renderer/api/lighting.h
    renderer/api/log.h
    renderer/api/material.h
    renderer/api/object.h
    renderer/api/postprocessing.h
    renderer/api/project.h
    renderer/api/rasterization.h
    renderer/api/rendering.h
    renderer/api/scene.h
    renderer/api/scenepicker.h
    renderer/api/shadergroup.h
    renderer/api/source.h
    renderer/api/surfaceshader.h
    renderer/api/texture.h
    renderer/api/trace.h
    renderer/api/types.h
    renderer/api/utility.h
    renderer/api/volume.h
)
source_group ("renderer\\api" FILES
    ${renderer_api_sources}
)

set (renderer_device_sources
    renderer/device/irendercontext.h
    renderer/device/irenderdevice.h
    renderer/device/renderdevicebase.h
)
source_group ("renderer\\device" FILES
    ${renderer_device_sources}
)

set (renderer_device_cpu_sources
    renderer/device/cpu/cpurendercontext.h
    renderer/device/cpu/cpurenderdevice.h
)
source_group ("renderer\\device\\cpu" FILES
    ${renderer_device_cpu_sources}
)

set (renderer_global_sources
    renderer/global/globallogger.h
    renderer/global/globaltypes.h
)
source_group ("renderer\\global" FILES
    ${renderer_global_sources}
)

set (renderer_kernel_aov_sources
    renderer/kernel/aov/aovaccumulator.h
    renderer/kernel/aov/aovcomponents.h
    renderer/kernel/aov/aovsettings.h
    renderer/kernel/aov/imagestack.h
    renderer/kernel/aov/tilestack.h
)
source_group ("renderer\\kernel\\aov" FILES
    ${renderer_kernel_aov_sources}
)

set (renderer_kernel_denoising_sources
    renderer/kernel/denoising/denoiser.h
)
source_group ("renderer\\kernel\\denoising" FILES
    ${renderer_kernel_denoising_sources}
)

set (renderer_kernel_intersection_sources
    renderer/kernel/intersection/assemblytree.h
    renderer/kernel/intersection/curvekey.h
    renderer/kernel/intersection/curvetree.h
    renderer/kernel/intersection/intersectionfilter.h
    renderer/kernel/intersection/intersectionsettings.h
    renderer/kernel/intersection/intersector.h
    renderer/kernel/intersection/probevisitorbase.h
    renderer/kernel/intersection/refining.h
    renderer/kernel/intersection/tracecontext.h
    renderer/kernel/intersection/treerepository.h
    renderer/kernel/intersection/triangleencoder.h
    renderer/kernel/intersection/triangleitemhandler.h
    renderer/kernel/intersection/trianglekey.h
    renderer/kernel/intersection/triangletree.h
    renderer/kernel/intersection/trianglevertexinfo.h
)
if (WITH_EMBREE)
    list (APPEND renderer_kernel_intersection_sources
        renderer/kernel/intersection/embreescene.h
    )
endif ()
source_group ("renderer\\kernel\\intersection" FILES
    ${renderer_kernel_intersection_sources}
)

set (renderer_kernel_lighting_bdpt_sources
    renderer/kernel/lighting/bdpt/bdptlightingengine.h
)
source_group ("renderer\\kernel\\lighting\\bdpt" FILES
    ${renderer_kernel_lighting_bdpt_sources}
)

set (renderer_kernel_lighting_lighttracing_sources
    renderer/kernel/lighting/lighttracing/lighttracingsamplegenerator.h
)
source_group ("renderer\\kernel\\lighting\\lighttracing" FILES
    ${renderer_kernel_lighting_lighttracing_sources}
)

set (renderer_kernel_lighting_null_sources
    renderer/kernel/lighting/null/nulllightingengine.h
)
source_group ("renderer\\kernel\\lighting\\null" FILES
    ${renderer_kernel_lighting_null_sources}
)

set (renderer_kernel_lighting_pt_sources
    renderer/kernel/lighting/pt/ptlightingengine.h
)
source_group ("renderer\\kernel\\lighting\\pt" FILES
    ${renderer_kernel_lighting_pt_sources}
)

set (renderer_kernel_lighting_sppm_sources
    renderer/kernel/lighting/sppm/sppmimporton.h
    renderer/kernel/lighting/sppm/sppmimportonmap.h
    renderer/kernel/lighting/sppm/sppmlightingengine.h
    renderer/kernel/lighting/sppm/sppmlightingengineworkingset.h
    renderer/kernel/lighting/sppm/sppmparameters.h
    renderer/kernel/lighting/sppm/sppmpasscallback.h
    renderer/kernel/lighting/sppm/sppmphoton.h
    renderer/kernel/lighting/sppm/sppmphotonmap.h
    renderer/kernel/lighting/sppm/sppmphotontracer.h
)
source_group ("renderer\\kernel\\lighting\\sppm" FILES
    ${renderer_kernel_lighting_sppm_sources}
)

set (renderer_kernel_lighting_sources
    renderer/kernel/lighting/backwardlightsampler.h
    renderer/kernel/lighting/directlightingintegrator.h
    renderer/kernel/lighting/forwardlightsampler.h
    renderer/kernel/lighting/ilightingengine.h
    renderer/kernel/lighting/imagebasedlighting.h
    renderer/kernel/lighting/lightpathrecorder.h
    renderer/kernel/lighting/lightpathstream.h
    renderer/kernel/lighting/lightsample.h
    renderer/kernel/lighting/lightsamplerbase.h
    renderer/kernel/lighting/lighttree.h
    renderer/kernel/lighting/lighttree_node.h
    renderer/kernel/lighting/lighttypes.h
    renderer/kernel/lighting/materialsamplers.h
    renderer/kernel/lighting/pathtracer.h
    renderer/kernel/lighting/pathvertex.h
    renderer/kernel/lighting/scatteringmode.h
    renderer/kernel/lighting/tracer.h
    renderer/kernel/lighting/volumelightingintegrator.h
)
source_group ("renderer\\kernel\\lighting" FILES
    ${renderer_kernel_lighting_sources}
)

set (renderer_kernel_npr_sources
    renderer/kernel/npr/nprclosures.h
)
source_group ("renderer\\kernel\\npr" FILES
    ${renderer_kernel_npr_sources}
)

set (renderer_kernel_rasterization_sources
    renderer/kernel/rasterization/objectrasterizer.h
    renderer/kernel/rasterization/rasterizationcamera.h
)
source_group ("renderer\\kernel\\rasterization" FILES
    ${renderer_kernel_rasterization_sources}
)

set (renderer_kernel_rendering_debug_sources
    renderer/kernel/rendering/debug/blanksamplerenderer.h
    renderer/kernel/rendering/debug/blanktilerenderer.h
    renderer/kernel/rendering/debug/debugsamplerenderer.h
    renderer/kernel/rendering/debug/debugtilerenderer.h
)
source_group ("renderer\\kernel\\rendering\\debug" FILES
    ${renderer_kernel_rendering_debug_sources}
)

set (renderer_kernel_rendering_final_sources
    renderer/kernel/rendering/final/adaptivetilerenderer.h
    renderer/kernel/rendering/final/pixelsampler.h
    renderer/kernel/rendering/final/texturecontrolledpixelrenderer.h
    renderer/kernel/rendering/final/uniformpixelrenderer.h
)
source_group ("renderer\\kernel\\rendering\\final" FILES
    ${renderer_kernel_rendering_final_sources}
)

set (renderer_kernel_rendering_generic_sources
    renderer/kernel/rendering/generic/genericframerenderer.h
    renderer/kernel/rendering/generic/genericsamplegenerator.h
    renderer/kernel/rendering/generic/genericsamplerenderer.h
    renderer/kernel/rendering/generic/generictilerenderer.h
    renderer/kernel/rendering/generic/tilejob.h
    renderer/kernel/rendering/generic/tilejobfactory.h
)
source_group ("renderer\\kernel\\rendering\\generic" FILES
    ${renderer_kernel_rendering_generic_sources}
)

set (renderer_kernel_rendering_progressive_sources
    renderer/kernel/rendering/progressive/progressiveframerenderer.h
    renderer/kernel/rendering/progressive/samplecounter.h
    renderer/kernel/rendering/progressive/samplecounthistory.h
    renderer/kernel/rendering/progressive/samplegeneratorjob.h
)
source_group ("renderer\\kernel\\rendering\\progressive" FILES
    ${renderer_kernel_rendering_progressive_sources}
)

set (renderer_kernel_rendering_sources
    renderer/kernel/rendering/defaultrenderercontroller.h
    renderer/kernel/rendering/ephemeralshadingresultframebufferfactory.h
    renderer/kernel/rendering/globalsampleaccumulationbuffer.h
    renderer/kernel/rendering/iframerenderer.h
    renderer/kernel/rendering/ipasscallback.h
    renderer/kernel/rendering/ipixelrenderer.h
    renderer/kernel/rendering/irenderercontroller.h
    renderer/kernel/rendering/isamplegenerator.h
    renderer/kernel/rendering/isamplerenderer.h
    renderer/kernel/rendering/ishadingresultframebufferfactory.h
    renderer/kernel/rendering/itilecallback.h
    renderer/kernel/rendering/itilerenderer.h
    renderer/kernel/rendering/localsampleaccumulationbuffer.h
    renderer/kernel/rendering/masterrenderer.h
    renderer/kernel/rendering/nulltilecallback.h
    renderer/kernel/rendering/oiioerrorhandler.h
    renderer/kernel/rendering/permanentshadingresultframebufferfactory.h
    renderer/kernel/rendering/pixelcontext.h
    renderer/kernel/rendering/pixelrendererbase.h
    renderer/kernel/rendering/renderercomponents.h
    renderer/kernel/rendering/renderercontrollercollection.h
    renderer/kernel/rendering/rendererservices.h
    renderer/kernel/rendering/sample.h
    renderer/kernel/rendering/sampleaccumulationbuffer.h
    renderer/kernel/rendering/samplegeneratorbase.h
    renderer/kernel/rendering/scenepicker.h
    renderer/kernel/rendering/serialrenderercontroller.h
    renderer/kernel/rendering/serialtilecallback.h
    renderer/kernel/rendering/shadingresultframebuffer.h
    renderer/kernel/rendering/tilecallbackbase.h
    renderer/kernel/rendering/tilecallbackcollection.h
    renderer/kernel/rendering/timedrenderercontroller.h
)
source_group ("renderer\\kernel\\rendering" FILES
    ${renderer_kernel_rendering_sources}
)

set (renderer_kernel_shading_sources
    renderer/kernel/shading/ambientocclusion.h
    renderer/kernel/shading/closures.h
    renderer/kernel/shading/directshadingcomponents.h
    renderer/kernel/shading/fastambientocclusion.h
    renderer/kernel/shading/oslshadergroupexec.h
    renderer/kernel/shading/oslshadingsystem.h
    renderer/kernel/shading/shadingcomponents.h
    renderer/kernel/shading/shadingcontext.h
    renderer/kernel/shading/shadingengine.h
    renderer/kernel/shading/shadingpoint.h
    renderer/kernel/shading/shadingpointbuilder.h
    renderer/kernel/shading/shadingray.h
    renderer/kernel/shading/shadingresult.h
)
source_group ("renderer\\kernel\\shading" FILES
    ${renderer_kernel_shading_sources}
)

set (renderer_kernel_tessellation_sources
    renderer/kernel/tessellation/statictessellation.h
)
source_group ("renderer\\kernel\\tessellation" FILES
    ${renderer_kernel_tessellation_sources}
)

set (renderer_kernel_texturing_sources
    renderer/kernel/texturing/oiiotexturesystem.h
    renderer/kernel/texturing/texturecache.h
    renderer/kernel/texturing/texturestore.h
)
source_group ("renderer\\kernel\\texturing" FILES
    ${renderer_kernel_texturing_sources}
)

set (renderer_kernel_volume_sources
    renderer/kernel/volume/occupancygrid.h
    renderer/kernel/volume/volume.h
)
source_group ("renderer\\kernel\\volume" FILES
    ${renderer_kernel_volume_sources}
)

set (renderer_meta_benchmarks_sources
)
source_group ("renderer\\meta\\benchmarks" FILES
    ${renderer_meta_benchmarks_sources}
)

set (renderer_meta_tests_sources
)
source_group ("renderer\\meta\\tests" FILES
    ${renderer_meta_tests_sources}
)

set (renderer_modeling_aov_sources
    renderer/modeling/aov/albedoaov.h
    renderer/modeling/aov/aov.h
    renderer/modeling/aov/aovcontainer.h
    renderer/modeling/aov/aovfactoryregistrar.h
    renderer/modeling/aov/aovtraits.h
    renderer/modeling/aov/cryptomatteaov.h
    renderer/modeling/aov/denoiseraov.h
    renderer/modeling/aov/depthaov.h
    renderer/modeling/aov/diffuseaov.h
    renderer/modeling/aov/emissionaov.h
    renderer/modeling/aov/glossyaov.h
    renderer/modeling/aov/iaovfactory.h
    renderer/modeling/aov/invalidsamplesaov.h
    renderer/modeling/aov/normalaov.h
    renderer/modeling/aov/npraovs.h
    renderer/modeling/aov/pixelerroraov.h
    renderer/modeling/aov/pixelsamplecountaov.h
    renderer/modeling/aov/pixeltimeaov.h
    renderer/modeling/aov/pixelvariationaov.h
    renderer/modeling/aov/positionaov.h
    renderer/modeling/aov/screenspacevelocityaov.h
    renderer/modeling/aov/uvaov.h
)
source_group ("renderer\\modeling\\aov" FILES
    ${renderer_modeling_aov_sources}
)

set (renderer_modeling_bsdf_sources
    renderer/modeling/bsdf/ashikhminbrdf.h
    renderer/modeling/bsdf/blinnbrdf.h
    renderer/modeling/bsdf/bsdf.h
    renderer/modeling/bsdf/bsdfblend.h
    renderer/modeling/bsdf/bsdffactoryregistrar.h
    renderer/modeling/bsdf/bsdfmix.h
    renderer/modeling/bsdf/bsdfsample.h
    renderer/modeling/bsdf/bsdftraits.h
    renderer/modeling/bsdf/bsdfwrapper.h
    renderer/modeling/bsdf/diffusebtdf.h
    renderer/modeling/bsdf/disneybrdf.h
    renderer/modeling/bsdf/energycompensation.h
    renderer/modeling/bsdf/energycompensationtables.h
    renderer/modeling/bsdf/fresnel.h
    renderer/modeling/bsdf/glassbsdf.h
    renderer/modeling/bsdf/glossybrdf.h
    renderer/modeling/bsdf/glossylayerbsdf.h
    renderer/modeling/bsdf/hairbsdf.h
    renderer/modeling/bsdf/ibsdffactory.h
    renderer/modeling/bsdf/kelemenbrdf.h
    renderer/modeling/bsdf/lambertianbrdf.h
    renderer/modeling/bsdf/metalbrdf.h
    renderer/modeling/bsdf/microfacetbrdfwrapper.h
    renderer/modeling/bsdf/microfacethelper.h
    renderer/modeling/bsdf/nullbsdf.h
    renderer/modeling/bsdf/orennayarbrdf.h
    renderer/modeling/bsdf/oslbsdf.h
    renderer/modeling/bsdf/plasticbrdf.h
    renderer/modeling/bsdf/sheenbrdf.h
    renderer/modeling/bsdf/specularbrdf.h
    renderer/modeling/bsdf/specularbtdf.h
    renderer/modeling/bsdf/specularhelper.h
)
source_group ("renderer\\modeling\\bsdf" FILES
    ${renderer_modeling_bsdf_sources}
)

set (renderer_modeling_bssrdf_sources
    renderer/modeling/bssrdf/betterdipolebssrdf.h
    renderer/modeling/bssrdf/bssrdf.h
    renderer/modeling/bssrdf/bssrdffactoryregistrar.h
    renderer/modeling/bssrdf/bssrdfsample.h
    renderer/modeling/bssrdf/bssrdftraits.h
    renderer/modeling/bssrdf/dipolebssrdf.h
    renderer/modeling/bssrdf/dipolebssrdffactory.h
    renderer/modeling/bssrdf/directionaldipolebssrdf.h
    renderer/modeling/bssrdf/gaussianbssrdf.h
    renderer/modeling/bssrdf/ibssrdffactory.h
    renderer/modeling/bssrdf/normalizeddiffusionbssrdf.h
    renderer/modeling/bssrdf/oslbssrdf.h
    renderer/modeling/bssrdf/randomwalkbssrdf.h
    renderer/modeling/bssrdf/separablebssrdf.h
    renderer/modeling/bssrdf/sss.h
    renderer/modeling/bssrdf/standarddipolebssrdf.h
)
source_group ("renderer\\modeling\\bssrdf" FILES
    ${renderer_modeling_bssrdf_sources}
)

set (renderer_modeling_camera_sources
    renderer/modeling/camera/camera.h
    renderer/modeling/camera/camerafactoryregistrar.h
    renderer/modeling/camera/cameratraits.h
    renderer/modeling/camera/fisheyelenscamera.h
    renderer/modeling/camera/icamerafactory.h
    renderer/modeling/camera/lenscamera.h
    renderer/modeling/camera/multilenscamera.h
    renderer/modeling/camera/orthographiccamera.h
    renderer/modeling/camera/perspectivecamera.h
    renderer/modeling/camera/pinholecamera.h
    renderer/modeling/camera/sphericalcamera.h
    renderer/modeling/camera/thinlenscamera.h
)
source_group ("renderer\\modeling\\camera" FILES
    ${renderer_modeling_camera_sources}
)

set (renderer_modeling_color_sources
    renderer/modeling/color/colorentity.h
    renderer/modeling/color/colorspace.h
    renderer/modeling/color/colortraits.h
    renderer/modeling/color/wavelengths.h
)
source_group ("renderer\\modeling\\color" FILES
    ${renderer_modeling_color_sources}
)

set (renderer_modeling_display_sources
    renderer/modeling/display/display.h
)
source_group ("renderer\\modeling\\display" FILES
    ${renderer_modeling_display_sources}
)

set (renderer_modeling_edf_sources
    renderer/modeling/edf/coneedf.h
    renderer/modeling/edf/diffuseedf.h
    renderer/modeling/edf/edf.h
    renderer/modeling/edf/edffactoryregistrar.h
    renderer/modeling/edf/edftraits.h
    renderer/modeling/edf/iedffactory.h
    renderer/modeling/edf/osledf.h
)
source_group ("renderer\\modeling\\edf" FILES
    ${renderer_modeling_edf_sources}
)

set (renderer_modeling_entity_sources
    renderer/modeling/entity/connectableentity.h
    renderer/modeling/entity/entity.h
    renderer/modeling/entity/entityfactoryregistrar.h
    renderer/modeling/entity/entitymap.h
    renderer/modeling/entity/entitytraits.h
    renderer/modeling/entity/entityvector.h
    renderer/modeling/entity/onframebeginrecorder.h
    renderer/modeling/entity/onrenderbeginrecorder.h
)
source_group ("renderer\\modeling\\entity" FILES
    ${renderer_modeling_entity_sources}
)

set (renderer_modeling_environment_sources
    renderer/modeling/environment/environment.h
    renderer/modeling/environment/environmenttraits.h
)
source_group ("renderer\\modeling\\environment" FILES
    ${renderer_modeling_environment_sources}
)

set (renderer_modeling_environmentedf_sources
    renderer/modeling/environmentedf/ArHosekSkyModelData_CIEXYZ.h
    renderer/modeling/environmentedf/constantenvironmentedf.h
    renderer/modeling/environmentedf/constanthemisphereenvironmentedf.h
    renderer/modeling/environmentedf/environmentedf.h
    renderer/modeling/environmentedf/environmentedffactoryregistrar.h
    renderer/modeling/environmentedf/environmentedftraits.h
    renderer/modeling/environmentedf/gradientenvironmentedf.h
    renderer/modeling/environmentedf/hosekenvironmentedf.h
    renderer/modeling/environmentedf/ienvironmentedffactory.h
    renderer/modeling/environmentedf/latlongmapenvironmentedf.h
    renderer/modeling/environmentedf/mirrorballmapenvironmentedf.h
    renderer/modeling/environmentedf/oslenvironmentedf.h
    renderer/modeling/environmentedf/preethamenvironmentedf.h
    renderer/modeling/environmentedf/sphericalcoordinates.h
)
source_group ("renderer\\modeling\\environmentedf" FILES
    ${renderer_modeling_environmentedf_sources}
)

set (renderer_modeling_environmentshader_sources
    renderer/modeling/environmentshader/backgroundenvironmentshader.h
    renderer/modeling/environmentshader/edfenvironmentshader.h
    renderer/modeling/environmentshader/environmentshader.h
    renderer/modeling/environmentshader/environmentshaderfactoryregistrar.h
    renderer/modeling/environmentshader/environmentshadertraits.h
    renderer/modeling/environmentshader/ienvironmentshaderfactory.h
)
source_group ("renderer\\modeling\\environmentshader" FILES
    ${renderer_modeling_environmentshader_sources}
)

set (renderer_modeling_frame_sources
    renderer/modeling/frame/frame.h
)
source_group ("renderer\\modeling\\frame" FILES
    ${renderer_modeling_frame_sources}
)

set (renderer_modeling_input_sources
    renderer/modeling/input/colorsource.h
    renderer/modeling/input/inputarray.h
    renderer/modeling/input/inputbinder.h
    renderer/modeling/input/scalarsource.h
    renderer/modeling/input/sourceinputs.h
    renderer/modeling/input/source.h
    renderer/modeling/input/symbol.h
    renderer/modeling/input/texturesource.h
)
source_group ("renderer\\modeling\\input" FILES
    ${renderer_modeling_input_sources}
)

set (renderer_modeling_light_sources
    renderer/modeling/light/directionallight.h
    renderer/modeling/light/ilightfactory.h
    renderer/modeling/light/light.h
    renderer/modeling/light/lightfactoryregistrar.h
    renderer/modeling/light/lighttarget.h
    renderer/modeling/light/lighttraits.h
    renderer/modeling/light/maxomnilight.h
    renderer/modeling/light/maxspotlight.h
    renderer/modeling/light/pointlight.h
    renderer/modeling/light/spotlight.h
    renderer/modeling/light/sunlight.h
)
source_group ("renderer\\modeling\\light" FILES
    ${renderer_modeling_light_sources}
)

set (renderer_modeling_material_sources
    renderer/modeling/material/bumpmappingmodifier.h
    renderer/modeling/material/genericmaterial.h
    renderer/modeling/material/ibasismodifier.h
    renderer/modeling/material/imaterialfactory.h
    renderer/modeling/material/material.h
    renderer/modeling/material/materialfactoryregistrar.h
    renderer/modeling/material/materialtraits.h
    renderer/modeling/material/normalmappingmodifier.h
    renderer/modeling/material/oslmaterial.h
)
source_group ("renderer\\modeling\\material" FILES
    ${renderer_modeling_material_sources}
)

set (renderer_modeling_object_sources
    renderer/modeling/object/curveobject.h
    renderer/modeling/object/curveobjectreader.h
    renderer/modeling/object/curveobjectwriter.h
    renderer/modeling/object/diskobject.h
    renderer/modeling/object/iobjectfactory.h
    renderer/modeling/object/meshobject.h
    renderer/modeling/object/meshobjectoperations.h
    renderer/modeling/object/meshobjectprimitives.h
    renderer/modeling/object/meshobjectreader.h
    renderer/modeling/object/meshobjectwriter.h
    renderer/modeling/object/object.h
    renderer/modeling/object/objectfactoryregistrar.h
    renderer/modeling/object/objecttraits.h
    renderer/modeling/object/proceduralobject.h
    renderer/modeling/object/rectangleobject.h
    renderer/modeling/object/sphereobject.h
    renderer/modeling/object/triangle.h
)
source_group ("renderer\\modeling\\object" FILES
    ${renderer_modeling_object_sources}
)

set (renderer_modeling_postprocessingstage_sources
    renderer/modeling/postprocessingstage/bloompostprocessingstage.h
    renderer/modeling/postprocessingstage/chromaticaberrationpostprocessingstage.h
    renderer/modeling/postprocessingstage/colormappostprocessingstage.h
    renderer/modeling/postprocessingstage/ipostprocessingstagefactory.h
    renderer/modeling/postprocessingstage/postprocessingstage.h
    renderer/modeling/postprocessingstage/postprocessingstagecontainer.h
    renderer/modeling/postprocessingstage/postprocessingstagefactoryregistrar.h
    renderer/modeling/postprocessingstage/postprocessingstagetraits.h
    renderer/modeling/postprocessingstage/renderstamppostprocessingstage.h
    renderer/modeling/postprocessingstage/tonemappostprocessingstage.h
    renderer/modeling/postprocessingstage/vignettepostprocessingstage.h
)
source_group ("renderer\\modeling\\postprocessingstage" FILES
    ${renderer_modeling_postprocessingstage_sources}
)

set (renderer_modeling_postprocessingstage_effect_sources
    renderer/modeling/postprocessingstage/effect/additiveblendapplier.h
    renderer/modeling/postprocessingstage/effect/brightpassapplier.h
    renderer/modeling/postprocessingstage/effect/chromaticaberrationapplier.h
    renderer/modeling/postprocessingstage/effect/clampcolorsapplier.h
    renderer/modeling/postprocessingstage/effect/imageeffectapplier.h
    renderer/modeling/postprocessingstage/effect/imageeffectjob.h
    renderer/modeling/postprocessingstage/effect/resampleapplier.h
    renderer/modeling/postprocessingstage/effect/resamplex2applier.h
    renderer/modeling/postprocessingstage/effect/tonemapapplier.h
    renderer/modeling/postprocessingstage/effect/vignetteapplier.h
)
source_group ("renderer\\modeling\\postprocessingstage\\effect" FILES
    ${renderer_modeling_postprocessingstage_effect_sources}
)

set (renderer_modeling_project_sources
    renderer/modeling/project/assethandler.h
    renderer/modeling/project/configuration.h
    renderer/modeling/project/configurationcontainer.h
    renderer/modeling/project/eventcounters.h
    renderer/modeling/project/project.h
    renderer/modeling/project/project.xsd
    renderer/modeling/project/projectfilereader.h
    renderer/modeling/project/projectfileupdater.h
    renderer/modeling/project/projectfilewriter.h
    renderer/modeling/project/projectformatrevision.h
    renderer/modeling/project/projecttracker.h
    renderer/modeling/project/renderingtimer.h
    renderer/modeling/project/xmlprojectfilereader.h
    renderer/modeling/project/xmlprojectfilewriter.h
)
source_group ("renderer\\modeling\\project" FILES
    ${renderer_modeling_project_sources}
)

set (renderer_modeling_project-builtin_sources
    renderer/modeling/project-builtin/cornellboxproject.h
    renderer/modeling/project-builtin/defaultproject.h
)
source_group ("renderer\\modeling\\project-builtin" FILES
    ${renderer_modeling_project-builtin_sources}
)

set (renderer_modeling_scene_sources
    renderer/modeling/scene/archiveassembly.h
    renderer/modeling/scene/assembly.h
    renderer/modeling/scene/assemblyfactoryregistrar.h
    renderer/modeling/scene/assemblyinstance.h
    renderer/modeling/scene/assemblyinstancetraits.h
    renderer/modeling/scene/assemblytraits.h
    renderer/modeling/scene/basegroup.h
    renderer/modeling/scene/containers.h
    renderer/modeling/scene/iassemblyfactory.h
    renderer/modeling/scene/objectinstance.h
    renderer/modeling/scene/objectinstancetraits.h
    renderer/modeling/scene/proceduralassembly.h
    renderer/modeling/scene/scene.h
    renderer/modeling/scene/textureinstance.h
    renderer/modeling/scene/textureinstancetraits.h
    renderer/modeling/scene/visibilityflags.h
)
source_group ("renderer\\modeling\\scene" FILES
    ${renderer_modeling_scene_sources}
)

set (renderer_modeling_shadergroup
    renderer/modeling/shadergroup/shader.h
    renderer/modeling/shadergroup/shadercompiler.h
    renderer/modeling/shadergroup/shaderconnection.h
    renderer/modeling/shadergroup/shadergroup.h
    renderer/modeling/shadergroup/shadergrouptraits.h
    renderer/modeling/shadergroup/shaderparam.h
    renderer/modeling/shadergroup/shaderquery.h
    renderer/modeling/shadergroup/shaderparamparser.h
)
source_group ("renderer\\modeling\\shadergroup" FILES
    ${renderer_modeling_shadergroup}
)

set (renderer_modeling_surfaceshader_sources
    renderer/modeling/surfaceshader/aosurfaceshader.h
    renderer/modeling/surfaceshader/constantsurfaceshader.h
    renderer/modeling/surfaceshader/diagnosticsurfaceshader.h
    renderer/modeling/surfaceshader/isurfaceshaderfactory.h
    renderer/modeling/surfaceshader/nprsurfaceshaderhelper.h
    renderer/modeling/surfaceshader/physicalsurfaceshader.h
    renderer/modeling/surfaceshader/surfaceshader.h
    renderer/modeling/surfaceshader/surfaceshaderfactoryregistrar.h
    renderer/modeling/surfaceshader/surfaceshadertraits.h
)
source_group ("renderer\\modeling\\surfaceshader" FILES
    ${renderer_modeling_surfaceshader_sources}
)

set (renderer_modeling_texture_sources
    renderer/modeling/texture/disktexture2d.h
    renderer/modeling/texture/itexturefactory.h
    renderer/modeling/texture/memorytexture2d.h
    renderer/modeling/texture/texture.h
    renderer/modeling/texture/texturefactoryregistrar.h
    renderer/modeling/texture/texturetraits.h
    renderer/modeling/texture/tileptr.h
)
source_group ("renderer\\modeling\\texture" FILES
    ${renderer_modeling_texture_sources}
)

set (renderer_utility_sources
    renderer/utility/autodeskmax.h
    renderer/utility/bbox.h
    renderer/utility/dynamicspectrum.h
    renderer/utility/filesystem.h
    renderer/utility/iostreamop.h
    renderer/utility/messagecontext.h
    renderer/utility/oiiomaketexture.h
    renderer/utility/paramarray.h
    renderer/utility/plugin.h
    renderer/utility/pluginstore.h
    renderer/utility/projectpoints.h
    renderer/utility/rgbspectrum.h
    renderer/utility/settingsparsing.h
    renderer/utility/shadowterminator.h
    renderer/utility/spectrumclamp.h
    renderer/utility/stochasticcast.h
    renderer/utility/testutils.h
    renderer/utility/transformsequence.h
    renderer/utility/transformsequencestack.h
    renderer/utility/triangle.h
)
source_group ("renderer\\utility" FILES
    ${renderer_utility_sources}
)

set (renderer_modeling_volume_sources
    renderer/modeling/volume/genericvolume.h
    renderer/modeling/volume/ivolumefactory.h
    renderer/modeling/volume/volume.h
    renderer/modeling/volume/volumefactoryregistrar.h
    renderer/modeling/volume/volumetraits.h
)
source_group ("renderer\\modeling\\volume" FILES
    ${renderer_modeling_volume_sources}
)


set (standard_osl_headers
    shaders/appleseed/oslutil.h
    shaders/appleseed/stdosl.h
)
source_group ("include\\appleseed\\shaders" FILES
    ${standard_osl_headers}
)

set (include_appleseed_color_sources
    shaders/appleseed/color/as_chromatic_adaptation.h
    shaders/appleseed/color/as_color_blend_modes.h
    shaders/appleseed/color/as_color_helpers.h
    shaders/appleseed/color/as_color_transforms.h
    shaders/appleseed/color/as_colorimetry.h
    shaders/appleseed/color/as_transfer_functions.h
)
list (APPEND osl_headers
    ${include_appleseed_color_sources}
)
source_group ("include\\appleseed\\shaders\\color" FILES
    ${include_appleseed_color_sources}
)

set (include_appleseed_fractal_sources
    shaders/appleseed/fractal/as_fractal_helpers.h
    shaders/appleseed/fractal/as_noise_helpers.h
)
list (APPEND osl_headers
    ${include_appleseed_fractal_sources}
)
source_group ("include\\appleseed\\shaders\\fractal" FILES
    ${include_appleseed_fractal_sources}
)

set (include_appleseed_material_sources
    shaders/appleseed/material/as_material_helpers.h
)
list (APPEND osl_headers
    ${include_appleseed_material_sources}
)
source_group ("include\\appleseed\\shaders\\material" FILES
    ${include_appleseed_material_sources}
)

set (include_appleseed_math_sources
    shaders/appleseed/math/as_math_complex.h
    shaders/appleseed/math/as_math_helpers.h
)
list (APPEND osl_headers
    ${include_appleseed_math_sources}
)
source_group ("include\\appleseed\\shaders\\math" FILES
    ${include_appleseed_math_sources}
)

set (include_appleseed_max_sources
)
list (APPEND osl_headers
    ${include_appleseed_max_sources}
)
source_group ("include\\appleseed\\shaders\\max" FILES
    ${include_appleseed_max_sources}
)

set (include_appleseed_maya_sources
    shaders/appleseed/maya/as_maya_cms_syncolor_idt.h
    shaders/appleseed/maya/as_maya_fractal_helpers.h
    shaders/appleseed/maya/as_maya_helpers.h
    shaders/appleseed/maya/as_maya_layer_helpers.h
    shaders/appleseed/maya/as_maya_pattern_helpers.h
    shaders/appleseed/maya/as_maya_ramp_helpers.h
    shaders/appleseed/maya/as_maya_recurrence_helpers.h
    shaders/appleseed/maya/as_maya_recurrence_tables.h
    shaders/appleseed/maya/as_maya_transform_helpers.h
)
list (APPEND osl_headers
    ${include_appleseed_maya_sources}
)
source_group ("include\\appleseed\\shaders\\maya" FILES
    ${include_appleseed_maya_sources}
)

set (include_appleseed_pattern_sources
    shaders/appleseed/pattern/as_pattern_helpers.h
)
list (APPEND osl_headers
    ${include_appleseed_pattern_sources}
)
source_group ("include\\appleseed\\shaders\\pattern" FILES
    ${include_appleseed_pattern_sources}
)

set (include_appleseed_texture_sources
    shaders/appleseed/texture/as_texture_helpers.h
)
list (APPEND osl_headers
    ${include_appleseed_texture_sources}
)
source_group ("include\\appleseed\\shaders\\texture" FILES
    ${include_appleseed_texture_sources}
)

set (include_appleseed_transform_sources
    shaders/appleseed/transform/as_transform_helpers.h
)
list (APPEND osl_headers
    ${include_appleseed_transform_sources}
)
source_group ("include\\appleseed\\shaders\\transform" FILES
    ${include_appleseed_transform_sources}
)

#--------------------------------------------------------------------------------------------------
# Install header files.
#--------------------------------------------------------------------------------------------------

if (INSTALL_HEADERS)
  install(
    DIRECTORY appleseed
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
    )
  install(
    DIRECTORY ${PROJECT_BINARY_DIR}/include/appleseed
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
    )
endif ()


# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8

