
#
# This source file is part of appleseed.
# Visit https://appleseedhq.net/ for additional information and resources.
#
# This software is released under the MIT license.
#
# Copyright (c) 2010-2013 Francois Beaune, Jupiter Jazz Limited
# Copyright (c) 2014-2018 Francois Beaune, The appleseedhq Organization
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
#


#--------------------------------------------------------------------------------------------------
# Include directories.
#--------------------------------------------------------------------------------------------------

if (WITH_GPU)
    include_directories (
        ${CUDA_INCLUDE_DIRS}
        ${OPTIX_INCLUDE_DIR}
    )
endif ()


#--------------------------------------------------------------------------------------------------
# Source files.
#--------------------------------------------------------------------------------------------------

set (foundation_array_sources
    foundation/array/algorithm.cpp
    foundation/array/array.cpp
    foundation/array/arraytraits.cpp
    foundation/array/exception.cpp
    foundation/array/keyframedarray.cpp
)
list (APPEND appleseed_sources
    ${foundation_array_sources}
)
source_group ("foundation\\array" FILES
    ${foundation_array_sources}
)

set (foundation_core_concepts_sources
)
list (APPEND appleseed_sources
    ${foundation_core_concepts_sources}
)
source_group ("foundation\\core\\concepts" FILES
    ${foundation_core_concepts_sources}
)

set (foundation_containers_sources
    foundation/containers/dictionary.cpp
)
list (APPEND appleseed_sources
    ${foundation_containers_sources}
)
source_group ("foundation\\containers" FILES
    ${foundation_containers_sources}
)

set (foundation_core_exceptions_sources
)
list (APPEND appleseed_sources
    ${foundation_core_exceptions_sources}
)
source_group ("foundation\\core\\exceptions" FILES
    ${foundation_core_exceptions_sources}
)

set (foundation_core_sources
    foundation/core/appleseed.cpp
    foundation/core/thirdparties.cpp
)
list (APPEND appleseed_sources
    ${foundation_core_sources}
)
source_group ("foundation\\core" FILES
    ${foundation_core_sources}
)

if (WITH_GPU)
    set (foundation_cuda_sources
        foundation/cuda/cudadevice.cpp
        foundation/cuda/exception.cpp
        foundation/cuda/memory.cpp
        foundation/cuda/module.cpp
    )
    list (APPEND appleseed_sources
        ${foundation_cuda_sources}
    )
    source_group ("foundation\\cuda" FILES
        ${foundation_cuda_sources}
    )
endif ()

set (foundation_curve_sources
    foundation/curve/binarycurvefilereader.cpp
    foundation/curve/binarycurvefilewriter.cpp
    foundation/curve/curvebasis.cpp
    foundation/curve/genericcurvefilereader.cpp
    foundation/curve/genericcurvefilewriter.cpp
    foundation/curve/mitshairfilereader.cpp
)
list (APPEND appleseed_sources
    ${foundation_curve_sources}
)
source_group ("foundation\\curve" FILES
    ${foundation_curve_sources}
)

set (foundation_hash_sources
    foundation/hash/murmurhash.cpp
    foundation/hash/siphash.cpp
)
list (APPEND appleseed_sources
    ${foundation_hash_sources}
)
source_group ("foundation\\hash" FILES
    ${foundation_hash_sources}
)

set (foundation_image_text_sources
    foundation/image/text/stb_truetype.cpp
    foundation/image/text/textrenderer.cpp
)
list (APPEND appleseed_sources
    ${foundation_image_text_sources}
)
source_group ("foundation\\image\\text" FILES
    ${foundation_image_text_sources}
)

set (foundation_image_sources
    foundation/image/accumulatortile.cpp
    foundation/image/analysis.cpp
    foundation/image/colormap.cpp
    foundation/image/colormapdata.cpp
    foundation/image/colorspace.cpp
    foundation/image/conversion.cpp
    foundation/image/drawing.cpp
    foundation/image/genericimagefilereader.cpp
    foundation/image/genericimagefilewriter.cpp
    foundation/image/genericprogressiveimagefilereader.cpp
    foundation/image/image.cpp
    foundation/image/imageattributes.cpp
    foundation/image/nativedrawing.cpp
    foundation/image/pixel.cpp
    foundation/image/tile.cpp
)
list (APPEND appleseed_sources
    ${foundation_image_sources}
)
source_group ("foundation\\image" FILES
    ${foundation_image_sources}
)

set (foundation_log_sources
    foundation/log/consolelogtarget.cpp
    foundation/log/filelogtarget.cpp
    foundation/log/filelogtargetbase.cpp
    foundation/log/logformatter.cpp
    foundation/log/logger.cpp
    foundation/log/logmessage.cpp
    foundation/log/openfilelogtarget.cpp
    foundation/log/stringlogtarget.cpp
)
list (APPEND appleseed_sources
    ${foundation_log_sources}
)
source_group ("foundation\\log" FILES
    ${foundation_log_sources}
)

set (foundation_math_bsp_sources
    foundation/math/bsp/bsp_statistics.cpp
)
list (APPEND appleseed_sources
    ${foundation_math_bsp_sources}
)
source_group ("foundation\\math\\bsp" FILES
    ${foundation_math_bsp_sources}
)

set (foundation_math_bvh_sources
    foundation/math/bvh/bvh_statistics.cpp
)
list (APPEND appleseed_sources
    ${foundation_math_bvh_sources}
)
source_group ("foundation\\math\\bvh" FILES
    ${foundation_math_bvh_sources}
)

set (foundation_math_knn_sources
    foundation/math/knn/knn_statistics.cpp
)
list (APPEND appleseed_sources
    ${foundation_math_knn_sources}
)
source_group ("foundation\\math\\knn" FILES
    ${foundation_math_knn_sources}
)

set (foundation_math_rng_sources
    foundation/math/rng/serialmersennetwister.cpp
)
if (USE_SSE)
    list (APPEND foundation_math_rng_sources
        foundation/math/rng/simdmersennetwister.cpp
    )
endif ()
list (APPEND appleseed_sources
    ${foundation_math_rng_sources}
)
source_group ("foundation\\math\\rng" FILES
    ${foundation_math_rng_sources}
)

set (foundation_math_voxel_sources
    foundation/math/voxel/voxel_statistics.cpp
)
list (APPEND appleseed_sources
    ${foundation_math_voxel_sources}
)
source_group ("foundation\\math\\voxel" FILES
    ${foundation_math_voxel_sources}
)

set (foundation_math_sources
    foundation/math/filtersamplingtable.cpp
    foundation/math/half.cpp
    foundation/math/microfacet.cpp
    foundation/math/noise.cpp
    foundation/math/ordering.cpp
    foundation/math/permutation.cpp
    foundation/math/phasefunction.cpp
    foundation/math/primes.cpp
    foundation/math/qmc.cpp
    foundation/math/specialfunctions.cpp
)
list (APPEND appleseed_sources
    ${foundation_math_sources}
)
source_group ("foundation\\math" FILES
    ${foundation_math_sources}
)

set (foundation_memory_sources
    foundation/memory/memory.cpp
)
list (APPEND appleseed_sources
    ${foundation_memory_sources}
)
source_group ("foundation\\memory" FILES
    ${foundation_memory_sources}
)

set (foundation_meshio_sources
    foundation/meshio/binarymeshfilereader.cpp
    foundation/meshio/binarymeshfilewriter.cpp
    foundation/meshio/genericmeshfilereader.cpp
    foundation/meshio/genericmeshfilewriter.cpp
    foundation/meshio/objmeshfilereader.cpp
    foundation/meshio/objmeshfilewriter.cpp
)
list (APPEND appleseed_sources
    ${foundation_meshio_sources}
)
source_group ("foundation\\meshio" FILES
    ${foundation_meshio_sources}
)

set (foundation_meta_benchmarks_sources
    foundation/meta/benchmarks/benchmark_basis.cpp
    foundation/meta/benchmarks/benchmark_cache.cpp
    foundation/meta/benchmarks/benchmark_cdf.cpp
    foundation/meta/benchmarks/benchmark_colorspace.cpp
    foundation/meta/benchmarks/benchmark_distance.cpp
    foundation/meta/benchmarks/benchmark_fastmath.cpp
    foundation/meta/benchmarks/benchmark_half.cpp
    foundation/meta/benchmarks/benchmark_hash.cpp
    foundation/meta/benchmarks/benchmark_imageimportancesampler.cpp
    foundation/meta/benchmarks/benchmark_integerdivision.cpp
    foundation/meta/benchmarks/benchmark_intersection.cpp
    foundation/meta/benchmarks/benchmark_job.cpp
    foundation/meta/benchmarks/benchmark_knn.cpp
    foundation/meta/benchmarks/benchmark_math_filter.cpp
    foundation/meta/benchmarks/benchmark_matrix.cpp
    foundation/meta/benchmarks/benchmark_microfacet.cpp
    foundation/meta/benchmarks/benchmark_permutation.cpp
    foundation/meta/benchmarks/benchmark_poolallocator.cpp
    foundation/meta/benchmarks/benchmark_qmc.cpp
    foundation/meta/benchmarks/benchmark_quaternion.cpp
    foundation/meta/benchmarks/benchmark_ray.cpp
    foundation/meta/benchmarks/benchmark_regularspectrum.cpp
    foundation/meta/benchmarks/benchmark_rng.cpp
    foundation/meta/benchmarks/benchmark_samesign.cpp
    foundation/meta/benchmarks/benchmark_sampling.cpp
    foundation/meta/benchmarks/benchmark_string.cpp
    foundation/meta/benchmarks/benchmark_tile.cpp
    foundation/meta/benchmarks/benchmark_transform.cpp
    foundation/meta/benchmarks/benchmark_vector.cpp
    foundation/meta/benchmarks/benchmark_voxelgrid.cpp
)
list (APPEND appleseed_sources
    ${foundation_meta_benchmarks_sources}
)
source_group ("foundation\\meta\\benchmarks" FILES
    ${foundation_meta_benchmarks_sources}
)

set (foundation_meta_tests_sources
    foundation/meta/tests/test_aabb.cpp
    foundation/meta/tests/test_analysis.cpp
    foundation/meta/tests/test_array.cpp
    foundation/meta/tests/test_arrayalgorithm.cpp
    foundation/meta/tests/test_arrayapplyvisitor.cpp
    foundation/meta/tests/test_attributeset.cpp
    foundation/meta/tests/test_autoreleaseptr.cpp
    foundation/meta/tests/test_benchmarkaggregator.cpp
    foundation/meta/tests/test_beziercurve.cpp
    foundation/meta/tests/test_bitmask.cpp
    foundation/meta/tests/test_boost_datetime.cpp
    foundation/meta/tests/test_boost_path.cpp
    foundation/meta/tests/test_boost_regex.cpp
    foundation/meta/tests/test_bsp.cpp
    foundation/meta/tests/test_bufferedfile.cpp
    foundation/meta/tests/test_bvh.cpp
    foundation/meta/tests/test_bvh_partitioner.cpp
    foundation/meta/tests/test_cache.cpp
    foundation/meta/tests/test_cameracontroller.cpp
    foundation/meta/tests/test_casts.cpp
    foundation/meta/tests/test_cdf.cpp
    foundation/meta/tests/test_color.cpp
    foundation/meta/tests/test_colorspace.cpp
    foundation/meta/tests/test_commandlineparser.cpp
    foundation/meta/tests/test_compressedunitvector.cpp
    foundation/meta/tests/test_concepts.cpp
    foundation/meta/tests/test_copyonwrite.cpp
    foundation/meta/tests/test_countof.cpp
    foundation/meta/tests/test_datetime.cpp
    foundation/meta/tests/test_dictionary.cpp
    foundation/meta/tests/test_distance.cpp
    foundation/meta/tests/test_fastmath.cpp
    foundation/meta/tests/test_filtersamplingtable.cpp
    foundation/meta/tests/test_fp.cpp
    foundation/meta/tests/test_fresnel.cpp
    foundation/meta/tests/test_genericimagefilewriter.cpp
    foundation/meta/tests/test_genericprogressiveimagefilereader.cpp
    foundation/meta/tests/test_half.cpp
    foundation/meta/tests/test_hash.cpp
    foundation/meta/tests/test_hashtable.cpp
    foundation/meta/tests/test_iesparser.cpp
    foundation/meta/tests/test_image.cpp
    foundation/meta/tests/test_imageimportancesampler.cpp
    foundation/meta/tests/test_internedstring.cpp
    foundation/meta/tests/test_intersection_frustumaabb.cpp
    foundation/meta/tests/test_intersection_frustumsegment.cpp
    foundation/meta/tests/test_intersection_planesegment.cpp
    foundation/meta/tests/test_intersection_rayaabb.cpp
    foundation/meta/tests/test_intersection_raysphere.cpp
    foundation/meta/tests/test_intersection_raytriangle.cpp
    foundation/meta/tests/test_iostreamop.cpp
    foundation/meta/tests/test_job.cpp
    foundation/meta/tests/test_keyframedarray.cpp
    foundation/meta/tests/test_knn.cpp
    foundation/meta/tests/test_kvpair.cpp
    foundation/meta/tests/test_lazy.cpp
    foundation/meta/tests/test_makevector.cpp
    foundation/meta/tests/test_math_filter.cpp
    foundation/meta/tests/test_matrix.cpp
    foundation/meta/tests/test_memory.cpp
    foundation/meta/tests/test_microfacet.cpp
    foundation/meta/tests/test_minmax.cpp
    foundation/meta/tests/test_mis.cpp
    foundation/meta/tests/test_murmurhash.cpp
    foundation/meta/tests/test_noise.cpp
    foundation/meta/tests/test_objmeshfilereader.cpp
    foundation/meta/tests/test_objmeshfilewriter.cpp
    foundation/meta/tests/test_otherwise.cpp
    foundation/meta/tests/test_path.cpp
    foundation/meta/tests/test_permutation.cpp
    foundation/meta/tests/test_pixel.cpp
    foundation/meta/tests/test_poison.cpp
    foundation/meta/tests/test_poolallocator.cpp
    foundation/meta/tests/test_population.cpp
    foundation/meta/tests/test_preprocessor.cpp
    foundation/meta/tests/test_qmc.cpp
    foundation/meta/tests/test_quaternion.cpp
    foundation/meta/tests/test_ray.cpp
    foundation/meta/tests/test_registrar.cpp
    foundation/meta/tests/test_regularspectrum.cpp
    foundation/meta/tests/test_rng.cpp
    foundation/meta/tests/test_sampling.cpp
    foundation/meta/tests/test_scalar.cpp
    foundation/meta/tests/test_searchpaths.cpp
    foundation/meta/tests/test_settings.cpp
    foundation/meta/tests/test_sharedlibrary.cpp
    foundation/meta/tests/test_siphash.cpp
    foundation/meta/tests/test_snprintf.cpp
    foundation/meta/tests/test_sphericalimportancesampler.cpp
    foundation/meta/tests/test_spline.cpp
    foundation/meta/tests/test_stampedptr.cpp
    foundation/meta/tests/test_statistics.cpp
    foundation/meta/tests/test_stlallocatortestbed.cpp
    foundation/meta/tests/test_stopwatch.cpp
    foundation/meta/tests/test_string.cpp
    foundation/meta/tests/test_tea.cpp
    foundation/meta/tests/test_test.cpp
    foundation/meta/tests/test_thread.cpp
    foundation/meta/tests/test_tile.cpp
    foundation/meta/tests/test_timers.cpp
    foundation/meta/tests/test_transform.cpp
    foundation/meta/tests/test_triangulator.cpp
    foundation/meta/tests/test_typetraits.cpp
    foundation/meta/tests/test_utility_filter.cpp
    foundation/meta/tests/test_vector.cpp
    foundation/meta/tests/test_voxelgrid.cpp
    foundation/meta/tests/test_windows.cpp
    foundation/meta/tests/test_z85.cpp
    foundation/meta/tests/test_zip.cpp
)
list (APPEND appleseed_sources
    ${foundation_meta_tests_sources}
)
source_group ("foundation\\meta\\tests" FILES
    ${foundation_meta_tests_sources}
)

set (foundation_platform_snprintf_sources
    foundation/platform/snprintf/snprintf.cpp
)
list (APPEND appleseed_sources
    ${foundation_platform_snprintf_sources}
)
source_group ("foundation\\platform\\snprintf" FILES
    ${foundation_platform_snprintf_sources}
)

set (foundation_platform_sources
    foundation/platform/compiler.cpp
    foundation/platform/console.cpp
    foundation/platform/debugger.cpp
    foundation/platform/defaulttimers.cpp
    foundation/platform/path.cpp
    foundation/platform/sharedlibrary.cpp
    foundation/platform/system.cpp
    foundation/platform/thread.cpp
    foundation/platform/win32stackwalker.cpp
    foundation/platform/windows.cpp
)
if ("${CPU_TARGET}" MATCHES "x86")
    list (APPEND foundation_platform_sources
        foundation/platform/x86timer.cpp
    )
endif ()
list (APPEND appleseed_sources
    ${foundation_platform_sources}
)
source_group ("foundation\\platform" FILES
    ${foundation_platform_sources}
)

set (foundation_resources_fonts_sources
    foundation/resources/fonts/Ubuntu-L.ttf.cpp
    foundation/resources/fonts/Ubuntu-M.ttf.cpp
)
list (APPEND appleseed_sources
    ${foundation_resources_fonts_sources}
)
source_group ("foundation\\resources\\fonts" FILES
    ${foundation_resources_fonts_sources}
)

set (foundation_resources_logo_sources
    foundation/resources/logo/appleseed-seeds-16.cpp
    foundation/resources/logo/appleseed-seeds-32.cpp
    foundation/resources/logo/appleseed-seeds-64.cpp
    foundation/resources/logo/appleseed-seeds-256.cpp
    foundation/resources/logo/generatelogofiles.cpp
)
list (APPEND appleseed_sources
    ${foundation_resources_logo_sources}
)
source_group ("foundation\\resources\\logo" FILES
    ${foundation_resources_logo_sources}
)

set (foundation_string_sources
    foundation/string/internedstring.cpp
    foundation/string/string.cpp
)
list (APPEND appleseed_sources
    ${foundation_string_sources}
)
source_group ("foundation\\string" FILES
    ${foundation_string_sources}
)

set (foundation_utility_api_sources
    foundation/utility/api/apistring.cpp
    foundation/utility/api/apistringpair.cpp
    foundation/utility/api/specializedapiarrays.cpp
)
list (APPEND appleseed_sources
    ${foundation_utility_api_sources}
)
source_group ("foundation\\utility\\api" FILES
    ${foundation_utility_api_sources}
)

set (foundation_utility_benchmark_sources
    foundation/utility/benchmark/benchmarkaggregator.cpp
    foundation/utility/benchmark/benchmarkresult.cpp
    foundation/utility/benchmark/benchmarkseries.cpp
    foundation/utility/benchmark/benchmarksuite.cpp
    foundation/utility/benchmark/benchmarksuiterepository.cpp
    foundation/utility/benchmark/loggerbenchmarklistener.cpp
    foundation/utility/benchmark/xmlfilebenchmarklistener.cpp
)
list (APPEND appleseed_sources
    ${foundation_utility_benchmark_sources}
)
source_group ("foundation\\utility\\benchmark" FILES
    ${foundation_utility_benchmark_sources}
)

set (foundation_utility_commandlineparser_sources
    foundation/utility/commandlineparser/messagelist.cpp
)
list (APPEND appleseed_sources
    ${foundation_utility_commandlineparser_sources}
)
source_group ("foundation\\utility\\commandlineparser" FILES
    ${foundation_utility_commandlineparser_sources}
)

set (foundation_utility_filter_sources
    foundation/utility/filter/regexfilter.cpp
)
list (APPEND appleseed_sources
    ${foundation_utility_filter_sources}
)
source_group ("foundation\\utility\\filter" FILES
    ${foundation_utility_filter_sources}
)

set (foundation_utility_job_sources
    foundation/utility/job/jobmanager.cpp
    foundation/utility/job/jobqueue.cpp
    foundation/utility/job/workerthread.cpp
)
list (APPEND appleseed_sources
    ${foundation_utility_job_sources}
)
source_group ("foundation\\utility\\job" FILES
    ${foundation_utility_job_sources}
)

set (foundation_utility_minizip_sources
    foundation/utility/minizip/ioapi.c
    foundation/utility/minizip/unzip.c
    foundation/utility/minizip/zip.c
)
list (APPEND appleseed_sources
    ${foundation_utility_minizip_sources}
)
source_group ("foundation\\utility\\minizip" FILES
    ${foundation_utility_minizip_sources}
)

set (foundation_utility_settings_sources
    foundation/utility/settings/settings.xsd
    foundation/utility/settings/settingsfilereader.cpp
    foundation/utility/settings/settingsfilewriter.cpp
)
list (APPEND appleseed_sources
    ${foundation_utility_settings_sources}
)
source_group ("foundation\\utility\\settings" FILES
    ${foundation_utility_settings_sources}
)

set (foundation_utility_test_sources
    foundation/utility/test/loggertestlistener.cpp
    foundation/utility/test/testlistenercollection.cpp
    foundation/utility/test/testlistenerhelper.cpp
    foundation/utility/test/testresult.cpp
    foundation/utility/test/testsuite.cpp
    foundation/utility/test/testsuiterepository.cpp
)
list (APPEND appleseed_sources
    ${foundation_utility_test_sources}
)
source_group ("foundation\\utility\\test" FILES
    ${foundation_utility_test_sources}
)

set (foundation_utility_sources
    foundation/utility/attributeset.cpp
    foundation/utility/bufferedfile.cpp
    foundation/utility/cache.cpp
    foundation/utility/gnuplotfile.cpp
    foundation/utility/iesparser.cpp
    foundation/utility/indenter.cpp
    foundation/utility/preprocessor.cpp
    foundation/utility/searchpaths.cpp
    foundation/utility/statistics.cpp
    foundation/utility/testutils.cpp
    foundation/utility/uid.cpp
    foundation/utility/vpythonfile.cpp
    foundation/utility/xercesc.cpp
    foundation/utility/z85.cpp
    foundation/utility/zip.cpp
)
list (APPEND appleseed_sources
    ${foundation_utility_sources}
)
source_group ("foundation\\utility" FILES
    ${foundation_utility_sources}
)

set (main_sources
    main/allocator.cpp
    main/dllmain.cpp
)
list (APPEND appleseed_sources
    ${main_sources}
)
source_group ("main" FILES
    ${main_sources}
)

set (renderer_device_sources
    renderer/device/renderdevicebase.cpp
)
list (APPEND appleseed_sources
    ${renderer_device_sources}
)
source_group ("renderer\\device" FILES
    ${renderer_device_sources}
)

set (renderer_device_cpu_sources
    renderer/device/cpu/cpurenderdevice.cpp
)
list (APPEND appleseed_sources
    ${renderer_device_cpu_sources}
)
source_group ("renderer\\device\\cpu" FILES
    ${renderer_device_cpu_sources}
)

set (renderer_global_sources
    renderer/global/globallogger.cpp
)
list (APPEND appleseed_sources
    ${renderer_global_sources}
)
source_group ("renderer\\global" FILES
    ${renderer_global_sources}
)

set (renderer_kernel_aov_sources
    renderer/kernel/aov/aovaccumulator.cpp
    renderer/kernel/aov/aovcomponents.cpp
    renderer/kernel/aov/imagestack.cpp
)
list (APPEND appleseed_sources
    ${renderer_kernel_aov_sources}
)
source_group ("renderer\\kernel\\aov" FILES
    ${renderer_kernel_aov_sources}
)

set (renderer_kernel_denoising_sources
    renderer/kernel/denoising/denoiser.cpp
)
list (APPEND appleseed_sources
    ${renderer_kernel_denoising_sources}
)
source_group ("renderer\\kernel\\denoising" FILES
    ${renderer_kernel_denoising_sources}
)

set (renderer_kernel_intersection_sources
    renderer/kernel/intersection/assemblytree.cpp
    renderer/kernel/intersection/curvetree.cpp
    renderer/kernel/intersection/intersectionfilter.cpp
    renderer/kernel/intersection/intersector.cpp
    renderer/kernel/intersection/tracecontext.cpp
    renderer/kernel/intersection/triangleencoder.cpp
    renderer/kernel/intersection/triangleitemhandler.cpp
    renderer/kernel/intersection/triangletree.cpp
)
if (WITH_EMBREE)
    list (APPEND renderer_kernel_intersection_sources
        renderer/kernel/intersection/embreescene.cpp
    )
endif ()
list (APPEND appleseed_sources
    ${renderer_kernel_intersection_sources}
)
source_group ("renderer\\kernel\\intersection" FILES
    ${renderer_kernel_intersection_sources}
)

set (renderer_kernel_lighting_bdpt_sources
    renderer/kernel/lighting/bdpt/bdptlightingengine.cpp
)
list (APPEND appleseed_sources
    ${renderer_kernel_lighting_bdpt_sources}
)
source_group ("renderer\\kernel\\lighting\\bdpt" FILES
    ${renderer_kernel_lighting_bdpt_sources}
)

set (renderer_kernel_lighting_lighttracing_sources
    renderer/kernel/lighting/lighttracing/lighttracingsamplegenerator.cpp
)
list (APPEND appleseed_sources
    ${renderer_kernel_lighting_lighttracing_sources}
)
source_group ("renderer\\kernel\\lighting\\lighttracing" FILES
    ${renderer_kernel_lighting_lighttracing_sources}
)

set (renderer_kernel_lighting_pt_sources
    renderer/kernel/lighting/pt/ptlightingengine.cpp
)
list (APPEND appleseed_sources
    ${renderer_kernel_lighting_pt_sources}
)
source_group ("renderer\\kernel\\lighting\\pt" FILES
    ${renderer_kernel_lighting_pt_sources}
)

set (renderer_kernel_lighting_sppm_sources
    renderer/kernel/lighting/sppm/sppmimportonmap.cpp
    renderer/kernel/lighting/sppm/sppmlightingengine.cpp
    renderer/kernel/lighting/sppm/sppmparameters.cpp
    renderer/kernel/lighting/sppm/sppmpasscallback.cpp
    renderer/kernel/lighting/sppm/sppmphoton.cpp
    renderer/kernel/lighting/sppm/sppmphotonmap.cpp
    renderer/kernel/lighting/sppm/sppmphotontracer.cpp
)
list (APPEND appleseed_sources
    ${renderer_kernel_lighting_sppm_sources}
)
source_group ("renderer\\kernel\\lighting\\sppm" FILES
    ${renderer_kernel_lighting_sppm_sources}
)

set (renderer_kernel_lighting_sources
    renderer/kernel/lighting/backwardlightsampler.cpp
    renderer/kernel/lighting/directlightingintegrator.cpp
    renderer/kernel/lighting/forwardlightsampler.cpp
    renderer/kernel/lighting/imagebasedlighting.cpp
    renderer/kernel/lighting/lightpathrecorder.cpp
    renderer/kernel/lighting/lightpathstream.cpp
    renderer/kernel/lighting/lightsamplerbase.cpp
    renderer/kernel/lighting/lighttree.cpp
    renderer/kernel/lighting/lighttypes.cpp
    renderer/kernel/lighting/materialsamplers.cpp
    renderer/kernel/lighting/pathvertex.cpp
    renderer/kernel/lighting/tracer.cpp
    renderer/kernel/lighting/volumelightingintegrator.cpp
)
list (APPEND appleseed_sources
    ${renderer_kernel_lighting_sources}
)
source_group ("renderer\\kernel\\lighting" FILES
    ${renderer_kernel_lighting_sources}
)

set (renderer_kernel_rendering_debug_sources
    renderer/kernel/rendering/debug/blanksamplerenderer.cpp
    renderer/kernel/rendering/debug/blanktilerenderer.cpp
    renderer/kernel/rendering/debug/debugsamplerenderer.cpp
    renderer/kernel/rendering/debug/debugtilerenderer.cpp
)
list (APPEND appleseed_sources
    ${renderer_kernel_rendering_debug_sources}
)
source_group ("renderer\\kernel\\rendering\\debug" FILES
    ${renderer_kernel_rendering_debug_sources}
)

set (renderer_kernel_rendering_final_sources
    renderer/kernel/rendering/final/adaptivetilerenderer.cpp
    renderer/kernel/rendering/final/pixelsampler.cpp
    renderer/kernel/rendering/final/texturecontrolledpixelrenderer.cpp
    renderer/kernel/rendering/final/uniformpixelrenderer.cpp
)
list (APPEND appleseed_sources
    ${renderer_kernel_rendering_final_sources}
)
source_group ("renderer\\kernel\\rendering\\final" FILES
    ${renderer_kernel_rendering_final_sources}
)

set (renderer_kernel_rendering_generic_sources
    renderer/kernel/rendering/generic/genericframerenderer.cpp
    renderer/kernel/rendering/generic/genericsamplegenerator.cpp
    renderer/kernel/rendering/generic/genericsamplerenderer.cpp
    renderer/kernel/rendering/generic/generictilerenderer.cpp
    renderer/kernel/rendering/generic/tilejob.cpp
    renderer/kernel/rendering/generic/tilejobfactory.cpp
)
list (APPEND appleseed_sources
    ${renderer_kernel_rendering_generic_sources}
)
source_group ("renderer\\kernel\\rendering\\generic" FILES
    ${renderer_kernel_rendering_generic_sources}
)

set (renderer_kernel_rendering_progressive_sources
    renderer/kernel/rendering/progressive/progressiveframerenderer.cpp
    renderer/kernel/rendering/progressive/samplecounter.cpp
    renderer/kernel/rendering/progressive/samplegeneratorjob.cpp
)
list (APPEND appleseed_sources
    ${renderer_kernel_rendering_progressive_sources}
)
source_group ("renderer\\kernel\\rendering\\progressive" FILES
    ${renderer_kernel_rendering_progressive_sources}
)

set (renderer_kernel_rendering_sources
    renderer/kernel/rendering/defaultrenderercontroller.cpp
    renderer/kernel/rendering/ephemeralshadingresultframebufferfactory.cpp
    renderer/kernel/rendering/globalsampleaccumulationbuffer.cpp
    renderer/kernel/rendering/localsampleaccumulationbuffer.cpp
    renderer/kernel/rendering/masterrenderer.cpp
    renderer/kernel/rendering/nulltilecallback.cpp
    renderer/kernel/rendering/oiioerrorhandler.cpp
    renderer/kernel/rendering/permanentshadingresultframebufferfactory.cpp
    renderer/kernel/rendering/pixelrendererbase.cpp
    renderer/kernel/rendering/renderercomponents.cpp
    renderer/kernel/rendering/renderercontrollercollection.cpp
    renderer/kernel/rendering/rendererservices.cpp
    renderer/kernel/rendering/samplegeneratorbase.cpp
    renderer/kernel/rendering/scenepicker.cpp
    renderer/kernel/rendering/serialrenderercontroller.cpp
    renderer/kernel/rendering/serialtilecallback.cpp
    renderer/kernel/rendering/shadingresultframebuffer.cpp
    renderer/kernel/rendering/tilecallbackcollection.cpp
    renderer/kernel/rendering/timedrenderercontroller.cpp
)
list (APPEND appleseed_sources
    ${renderer_kernel_rendering_sources}
)
source_group ("renderer\\kernel\\rendering" FILES
    ${renderer_kernel_rendering_sources}
)

set (renderer_kernel_shading_sources
    renderer/kernel/shading/closures.cpp
    renderer/kernel/shading/directshadingcomponents.cpp
    renderer/kernel/shading/fastambientocclusion.cpp
    renderer/kernel/shading/oslshadergroupexec.cpp
    renderer/kernel/shading/oslshadingsystem.cpp
    renderer/kernel/shading/shadingcomponents.cpp
    renderer/kernel/shading/shadingcontext.cpp
    renderer/kernel/shading/shadingengine.cpp
    renderer/kernel/shading/shadingpoint.cpp
    renderer/kernel/shading/shadingpointbuilder.cpp
    renderer/kernel/shading/shadingray.cpp
    renderer/kernel/shading/shadingresult.cpp
)
list (APPEND appleseed_sources
    ${renderer_kernel_shading_sources}
)
source_group ("renderer\\kernel\\shading" FILES
    ${renderer_kernel_shading_sources}
)

set (renderer_kernel_texturing_sources
    renderer/kernel/texturing/oiiotexturesystem.cpp
    renderer/kernel/texturing/texturestore.cpp
)
list (APPEND appleseed_sources
    ${renderer_kernel_texturing_sources}
)
source_group ("renderer\\kernel\\texturing" FILES
    ${renderer_kernel_texturing_sources}
)

set (renderer_kernel_volume_sources
    renderer/kernel/volume/occupancygrid.cpp
    renderer/kernel/volume/volume.cpp
)
list (APPEND appleseed_sources
    ${renderer_kernel_volume_sources}
)
source_group ("renderer\\kernel\\volume" FILES
    ${renderer_kernel_volume_sources}
)

set (renderer_meta_benchmarks_sources
    renderer/meta/benchmarks/benchmark_dynamicspectrum.cpp
    renderer/meta/benchmarks/benchmark_frame.cpp
    renderer/meta/benchmarks/benchmark_localsampleaccumulationbuffer.cpp
    renderer/meta/benchmarks/benchmark_shadowterminator.cpp
    renderer/meta/benchmarks/benchmark_transformsequence.cpp
)
list (APPEND appleseed_sources
    ${renderer_meta_benchmarks_sources}
)
source_group ("renderer\\meta\\benchmarks" FILES
    ${renderer_meta_benchmarks_sources}
)

set (renderer_meta_tests_sources
    renderer/meta/tests/test_assembly.cpp
    renderer/meta/tests/test_backwardlightsampler.cpp
    renderer/meta/tests/test_containers.cpp
    renderer/meta/tests/test_dynamicspectrum.cpp
    renderer/meta/tests/test_energycompensation.cpp
    renderer/meta/tests/test_entitymap.cpp
    renderer/meta/tests/test_entityvector.cpp
    renderer/meta/tests/test_environmentedf.cpp
    renderer/meta/tests/test_forwardlightsampler.cpp
    renderer/meta/tests/test_frame.cpp
    renderer/meta/tests/test_imagetools.cpp
    renderer/meta/tests/test_inputarray.cpp
    renderer/meta/tests/test_intersector.cpp
    renderer/meta/tests/test_localsampleaccumulationbuffer.cpp
    renderer/meta/tests/test_paramarray.cpp
    renderer/meta/tests/test_pinholecamera.cpp
    renderer/meta/tests/test_pixelsampler.cpp
    renderer/meta/tests/test_projectfilereader.cpp
    renderer/meta/tests/test_projectfilewriter.cpp
    renderer/meta/tests/test_rgbspectrum.cpp
    renderer/meta/tests/test_samplecounter.cpp
    renderer/meta/tests/test_samplecounthistory.cpp
    renderer/meta/tests/test_samplegeneratorjob.cpp
    renderer/meta/tests/test_scene.cpp
    renderer/meta/tests/test_shaderparamparser.cpp
    renderer/meta/tests/test_shadingresult.cpp
    renderer/meta/tests/test_sphericalcamera.cpp
    renderer/meta/tests/test_sss.cpp
    renderer/meta/tests/test_texturestore.cpp
    renderer/meta/tests/test_tracer.cpp
    renderer/meta/tests/test_transformsequence.cpp
    renderer/meta/tests/test_volume.cpp
)
list (APPEND appleseed_sources
    ${renderer_meta_tests_sources}
)
source_group ("renderer\\meta\\tests" FILES
    ${renderer_meta_tests_sources}
)

set (renderer_modeling_aov_sources
    renderer/modeling/aov/albedoaov.cpp
    renderer/modeling/aov/aov.cpp
    renderer/modeling/aov/aovfactoryregistrar.cpp
    renderer/modeling/aov/cryptomatteaov.cpp
    renderer/modeling/aov/denoiseraov.cpp
    renderer/modeling/aov/depthaov.cpp
    renderer/modeling/aov/diffuseaov.cpp
    renderer/modeling/aov/emissionaov.cpp
    renderer/modeling/aov/glossyaov.cpp
    renderer/modeling/aov/invalidsamplesaov.cpp
    renderer/modeling/aov/normalaov.cpp
    renderer/modeling/aov/npraovs.cpp
    renderer/modeling/aov/pixelerroraov.cpp
    renderer/modeling/aov/pixelsamplecountaov.cpp
    renderer/modeling/aov/pixeltimeaov.cpp
    renderer/modeling/aov/pixelvariationaov.cpp
    renderer/modeling/aov/positionaov.cpp
    renderer/modeling/aov/screenspacevelocityaov.cpp
    renderer/modeling/aov/uvaov.cpp
)
list (APPEND appleseed_sources
    ${renderer_modeling_aov_sources}
)
source_group ("renderer\\modeling\\aov" FILES
    ${renderer_modeling_aov_sources}
)

set (renderer_modeling_bsdf_sources
    renderer/modeling/bsdf/ashikhminbrdf.cpp
    renderer/modeling/bsdf/blinnbrdf.cpp
    renderer/modeling/bsdf/bsdf.cpp
    renderer/modeling/bsdf/bsdfblend.cpp
    renderer/modeling/bsdf/bsdffactoryregistrar.cpp
    renderer/modeling/bsdf/bsdfmix.cpp
    renderer/modeling/bsdf/bsdfsample.cpp
    renderer/modeling/bsdf/diffusebtdf.cpp
    renderer/modeling/bsdf/disneybrdf.cpp
    renderer/modeling/bsdf/energycompensation.cpp
    renderer/modeling/bsdf/energycompensationtables.cpp
    renderer/modeling/bsdf/glassbsdf.cpp
    renderer/modeling/bsdf/glossybrdf.cpp
    renderer/modeling/bsdf/glossylayerbsdf.cpp
    renderer/modeling/bsdf/hairbsdf.cpp
    renderer/modeling/bsdf/kelemenbrdf.cpp
    renderer/modeling/bsdf/lambertianbrdf.cpp
    renderer/modeling/bsdf/metalbrdf.cpp
    renderer/modeling/bsdf/microfacethelper.cpp
    renderer/modeling/bsdf/orennayarbrdf.cpp
    renderer/modeling/bsdf/oslbsdf.cpp
    renderer/modeling/bsdf/plasticbrdf.cpp
    renderer/modeling/bsdf/sheenbrdf.cpp
    renderer/modeling/bsdf/specularbrdf.cpp
    renderer/modeling/bsdf/specularbtdf.cpp
)
list (APPEND appleseed_sources
    ${renderer_modeling_bsdf_sources}
)
source_group ("renderer\\modeling\\bsdf" FILES
    ${renderer_modeling_bsdf_sources}
)

set (renderer_modeling_bssrdf_sources
    renderer/modeling/bssrdf/betterdipolebssrdf.cpp
    renderer/modeling/bssrdf/bssrdf.cpp
    renderer/modeling/bssrdf/bssrdffactoryregistrar.cpp
    renderer/modeling/bssrdf/dipolebssrdf.cpp
    renderer/modeling/bssrdf/directionaldipolebssrdf.cpp
    renderer/modeling/bssrdf/gaussianbssrdf.cpp
    renderer/modeling/bssrdf/normalizeddiffusionbssrdf.cpp
    renderer/modeling/bssrdf/oslbssrdf.cpp
    renderer/modeling/bssrdf/randomwalkbssrdf.cpp
    renderer/modeling/bssrdf/separablebssrdf.cpp
    renderer/modeling/bssrdf/sss.cpp
    renderer/modeling/bssrdf/standarddipolebssrdf.cpp
)
list (APPEND appleseed_sources
    ${renderer_modeling_bssrdf_sources}
)
source_group ("renderer\\modeling\\bssrdf" FILES
    ${renderer_modeling_bssrdf_sources}
)

set (renderer_modeling_camera_sources
    renderer/modeling/camera/camera.cpp
    renderer/modeling/camera/camerafactoryregistrar.cpp
    renderer/modeling/camera/fisheyelenscamera.cpp
    renderer/modeling/camera/lenscamera.cpp
    renderer/modeling/camera/multilenscamera.cpp
    renderer/modeling/camera/orthographiccamera.cpp
    renderer/modeling/camera/perspectivecamera.cpp
    renderer/modeling/camera/pinholecamera.cpp
    renderer/modeling/camera/sphericalcamera.cpp
    renderer/modeling/camera/thinlenscamera.cpp
)
list (APPEND appleseed_sources
    ${renderer_modeling_camera_sources}
)
source_group ("renderer\\modeling\\camera" FILES
    ${renderer_modeling_camera_sources}
)

set (renderer_modeling_color_sources
    renderer/modeling/color/colorentity.cpp
    renderer/modeling/color/colorspace.cpp
    renderer/modeling/color/wavelengths.cpp
)
list (APPEND appleseed_sources
    ${renderer_modeling_color_sources}
)
source_group ("renderer\\modeling\\color" FILES
    ${renderer_modeling_color_sources}
)

set (renderer_modeling_display_sources
    renderer/modeling/display/display.cpp
)
list (APPEND appleseed_sources
    ${renderer_modeling_display_sources}
)
source_group ("renderer\\modeling\\display" FILES
    ${renderer_modeling_display_sources}
)

set (renderer_modeling_edf_sources
    renderer/modeling/edf/coneedf.cpp
    renderer/modeling/edf/diffuseedf.cpp
    renderer/modeling/edf/edf.cpp
    renderer/modeling/edf/edffactoryregistrar.cpp
    renderer/modeling/edf/iedffactory.cpp
    renderer/modeling/edf/osledf.cpp
)
list (APPEND appleseed_sources
    ${renderer_modeling_edf_sources}
)
source_group ("renderer\\modeling\\edf" FILES
    ${renderer_modeling_edf_sources}
)

set (renderer_modeling_entity_sources
    renderer/modeling/entity/connectableentity.cpp
    renderer/modeling/entity/entity.cpp
    renderer/modeling/entity/entitymap.cpp
    renderer/modeling/entity/entityvector.cpp
    renderer/modeling/entity/onframebeginrecorder.cpp
    renderer/modeling/entity/onrenderbeginrecorder.cpp
)
list (APPEND appleseed_sources
    ${renderer_modeling_entity_sources}
)
source_group ("renderer\\modeling\\entity" FILES
    ${renderer_modeling_entity_sources}
)

set (renderer_modeling_environment_sources
    renderer/modeling/environment/environment.cpp
)
list (APPEND appleseed_sources
    ${renderer_modeling_environment_sources}
)
source_group ("renderer\\modeling\\environment" FILES
    ${renderer_modeling_environment_sources}
)

set (renderer_modeling_environmentedf_sources
    renderer/modeling/environmentedf/constantenvironmentedf.cpp
    renderer/modeling/environmentedf/constanthemisphereenvironmentedf.cpp
    renderer/modeling/environmentedf/environmentedf.cpp
    renderer/modeling/environmentedf/environmentedffactoryregistrar.cpp
    renderer/modeling/environmentedf/gradientenvironmentedf.cpp
    renderer/modeling/environmentedf/hosekenvironmentedf.cpp
    renderer/modeling/environmentedf/ienvironmentedffactory.cpp
    renderer/modeling/environmentedf/latlongmapenvironmentedf.cpp
    renderer/modeling/environmentedf/mirrorballmapenvironmentedf.cpp
    renderer/modeling/environmentedf/oslenvironmentedf.cpp
    renderer/modeling/environmentedf/preethamenvironmentedf.cpp
)
list (APPEND appleseed_sources
    ${renderer_modeling_environmentedf_sources}
)
source_group ("renderer\\modeling\\environmentedf" FILES
    ${renderer_modeling_environmentedf_sources}
)

set (renderer_modeling_environmentshader_sources
    renderer/modeling/environmentshader/backgroundenvironmentshader.cpp
    renderer/modeling/environmentshader/edfenvironmentshader.cpp
    renderer/modeling/environmentshader/environmentshader.cpp
    renderer/modeling/environmentshader/environmentshaderfactoryregistrar.cpp
)
list (APPEND appleseed_sources
    ${renderer_modeling_environmentshader_sources}
)
source_group ("renderer\\modeling\\environmentshader" FILES
    ${renderer_modeling_environmentshader_sources}
)

set (renderer_modeling_frame_sources
    renderer/modeling/frame/frame.cpp
)
list (APPEND appleseed_sources
    ${renderer_modeling_frame_sources}
)
source_group ("renderer\\modeling\\frame" FILES
    ${renderer_modeling_frame_sources}
)

set (renderer_modeling_input_sources
    renderer/modeling/input/colorsource.cpp
    renderer/modeling/input/inputarray.cpp
    renderer/modeling/input/inputbinder.cpp
    renderer/modeling/input/texturesource.cpp
)
list (APPEND appleseed_sources
    ${renderer_modeling_input_sources}
)
source_group ("renderer\\modeling\\input" FILES
    ${renderer_modeling_input_sources}
)

set (renderer_modeling_light_sources
    renderer/modeling/light/directionallight.cpp
    renderer/modeling/light/ilightfactory.cpp
    renderer/modeling/light/light.cpp
    renderer/modeling/light/lightfactoryregistrar.cpp
    renderer/modeling/light/lighttarget.cpp
    renderer/modeling/light/maxomnilight.cpp
    renderer/modeling/light/maxspotlight.cpp
    renderer/modeling/light/pointlight.cpp
    renderer/modeling/light/spotlight.cpp
    renderer/modeling/light/sunlight.cpp
)
list (APPEND appleseed_sources
    ${renderer_modeling_light_sources}
)
source_group ("renderer\\modeling\\light" FILES
    ${renderer_modeling_light_sources}
)

set (renderer_modeling_material_sources
    renderer/modeling/material/bumpmappingmodifier.cpp
    renderer/modeling/material/genericmaterial.cpp
    renderer/modeling/material/imaterialfactory.cpp
    renderer/modeling/material/material.cpp
    renderer/modeling/material/materialfactoryregistrar.cpp
    renderer/modeling/material/normalmappingmodifier.cpp
    renderer/modeling/material/oslmaterial.cpp
)
list (APPEND appleseed_sources
    ${renderer_modeling_material_sources}
)
source_group ("renderer\\modeling\\material" FILES
    ${renderer_modeling_material_sources}
)

set (renderer_modeling_object_sources
    renderer/modeling/object/curveobject.cpp
    renderer/modeling/object/curveobjectreader.cpp
    renderer/modeling/object/curveobjectwriter.cpp
    renderer/modeling/object/diskobject.cpp
    renderer/modeling/object/iobjectfactory.cpp
    renderer/modeling/object/meshobject.cpp
    renderer/modeling/object/meshobjectoperations.cpp
    renderer/modeling/object/meshobjectprimitives.cpp
    renderer/modeling/object/meshobjectreader.cpp
    renderer/modeling/object/meshobjectwriter.cpp
    renderer/modeling/object/object.cpp
    renderer/modeling/object/objectfactoryregistrar.cpp
    renderer/modeling/object/proceduralobject.cpp
    renderer/modeling/object/rectangleobject.cpp
    renderer/modeling/object/sphereobject.cpp
)
list (APPEND appleseed_sources
    ${renderer_modeling_object_sources}
)
source_group ("renderer\\modeling\\object" FILES
    ${renderer_modeling_object_sources}
)

set (renderer_modeling_postprocessingstage_sources
    renderer/modeling/postprocessingstage/bloompostprocessingstage.cpp
    renderer/modeling/postprocessingstage/chromaticaberrationpostprocessingstage.cpp
    renderer/modeling/postprocessingstage/colormappostprocessingstage.cpp
    renderer/modeling/postprocessingstage/ipostprocessingstagefactory.cpp
    renderer/modeling/postprocessingstage/postprocessingstage.cpp
    renderer/modeling/postprocessingstage/postprocessingstagefactoryregistrar.cpp
    renderer/modeling/postprocessingstage/renderstamppostprocessingstage.cpp
    renderer/modeling/postprocessingstage/tonemappostprocessingstage.cpp
    renderer/modeling/postprocessingstage/vignettepostprocessingstage.cpp
)
list (APPEND appleseed_sources
    ${renderer_modeling_postprocessingstage_sources}
)
source_group ("renderer\\modeling\\postprocessingstage" FILES
    ${renderer_modeling_postprocessingstage_sources}
)

set (renderer_modeling_postprocessingstage_effect_sources
    renderer/modeling/postprocessingstage/effect/additiveblendapplier.cpp
    renderer/modeling/postprocessingstage/effect/brightpassapplier.cpp
    renderer/modeling/postprocessingstage/effect/chromaticaberrationapplier.cpp
    renderer/modeling/postprocessingstage/effect/clampcolorsapplier.cpp
    renderer/modeling/postprocessingstage/effect/imageeffectapplier.cpp
    renderer/modeling/postprocessingstage/effect/imageeffectjob.cpp
    renderer/modeling/postprocessingstage/effect/resampleapplier.cpp
    renderer/modeling/postprocessingstage/effect/resamplex2applier.cpp
    renderer/modeling/postprocessingstage/effect/tonemapapplier.cpp
    renderer/modeling/postprocessingstage/effect/vignetteapplier.cpp
)
list (APPEND appleseed_sources
    ${renderer_modeling_postprocessingstage_effect_sources}
)
source_group ("renderer\\modeling\\postprocessingstage\\effect" FILES
    ${renderer_modeling_postprocessingstage_effect_sources}
)

set (renderer_modeling_project_sources
    renderer/modeling/project/assethandler.cpp
    renderer/modeling/project/configuration.cpp
    renderer/modeling/project/project.cpp
    renderer/modeling/project/project.xsd
    renderer/modeling/project/projectfilereader.cpp
    renderer/modeling/project/projectfileupdater.cpp
    renderer/modeling/project/projectfilewriter.cpp
    renderer/modeling/project/projecttracker.cpp
    renderer/modeling/project/xmlprojectfilereader.cpp
    renderer/modeling/project/xmlprojectfilewriter.cpp
)
list (APPEND appleseed_sources
    ${renderer_modeling_project_sources}
)
source_group ("renderer\\modeling\\project" FILES
    ${renderer_modeling_project_sources}
)

set (renderer_modeling_project-builtin_sources
    renderer/modeling/project-builtin/cornellboxproject.cpp
    renderer/modeling/project-builtin/defaultproject.cpp
)
list (APPEND appleseed_sources
    ${renderer_modeling_project-builtin_sources}
)
source_group ("renderer\\modeling\\project-builtin" FILES
    ${renderer_modeling_project-builtin_sources}
)

set (renderer_modeling_scene_sources
    renderer/modeling/scene/archiveassembly.cpp
    renderer/modeling/scene/assembly.cpp
    renderer/modeling/scene/assemblyfactoryregistrar.cpp
    renderer/modeling/scene/assemblyinstance.cpp
    renderer/modeling/scene/basegroup.cpp
    renderer/modeling/scene/containers.cpp
    renderer/modeling/scene/objectinstance.cpp
    renderer/modeling/scene/proceduralassembly.cpp
    renderer/modeling/scene/scene.cpp
    renderer/modeling/scene/textureinstance.cpp
    renderer/modeling/scene/visibilityflags.cpp
)
list (APPEND appleseed_sources
    ${renderer_modeling_scene_sources}
)
source_group ("renderer\\modeling\\scene" FILES
    ${renderer_modeling_scene_sources}
)

set (renderer_modeling_shadergroup
    renderer/modeling/shadergroup/shader.cpp
    renderer/modeling/shadergroup/shadercompiler.cpp
    renderer/modeling/shadergroup/shaderconnection.cpp
    renderer/modeling/shadergroup/shadergroup.cpp
    renderer/modeling/shadergroup/shaderparam.cpp
    renderer/modeling/shadergroup/shaderquery.cpp
    renderer/modeling/shadergroup/shaderparamparser.cpp
)
list (APPEND appleseed_sources
    ${renderer_modeling_shadergroup}
)
source_group ("renderer\\modeling\\shadergroup" FILES
    ${renderer_modeling_shadergroup}
)

set (renderer_modeling_surfaceshader_sources
    renderer/modeling/surfaceshader/aosurfaceshader.cpp
    renderer/modeling/surfaceshader/constantsurfaceshader.cpp
    renderer/modeling/surfaceshader/diagnosticsurfaceshader.cpp
    renderer/modeling/surfaceshader/nprsurfaceshaderhelper.cpp
    renderer/modeling/surfaceshader/physicalsurfaceshader.cpp
    renderer/modeling/surfaceshader/surfaceshader.cpp
    renderer/modeling/surfaceshader/surfaceshaderfactoryregistrar.cpp
)
list (APPEND appleseed_sources
    ${renderer_modeling_surfaceshader_sources}
)
source_group ("renderer\\modeling\\surfaceshader" FILES
    ${renderer_modeling_surfaceshader_sources}
)

set (renderer_modeling_texture_sources
    renderer/modeling/texture/disktexture2d.cpp
    renderer/modeling/texture/memorytexture2d.cpp
    renderer/modeling/texture/texture.cpp
    renderer/modeling/texture/texturefactoryregistrar.cpp
)
list (APPEND appleseed_sources
    ${renderer_modeling_texture_sources}
)
source_group ("renderer\\modeling\\texture" FILES
    ${renderer_modeling_texture_sources}
)

set (renderer_utility_sources
    renderer/utility/filesystem.cpp
    renderer/utility/messagecontext.cpp
    renderer/utility/oiiomaketexture.cpp
    renderer/utility/paramarray.cpp
    renderer/utility/plugin.cpp
    renderer/utility/pluginstore.cpp
    renderer/utility/projectpoints.cpp
    renderer/utility/settingsparsing.cpp
    renderer/utility/testutils.cpp
    renderer/utility/transformsequence.cpp
)
list (APPEND appleseed_sources
    ${renderer_utility_sources}
)
source_group ("renderer\\utility" FILES
    ${renderer_utility_sources}
)

set (renderer_modeling_volume_sources
    renderer/modeling/volume/genericvolume.cpp
    renderer/modeling/volume/volume.cpp
    renderer/modeling/volume/volumefactoryregistrar.cpp
)
list (APPEND appleseed_sources
    ${renderer_modeling_volume_sources}
)
source_group ("renderer\\modeling\\volume" FILES
    ${renderer_modeling_volume_sources}
)

# Collect all XML Schema files amongst the source files.
filter_list (
    appleseed_schema_files
    "${appleseed_sources}"
    ".*\\\\.xsd"
)

# Exclude all XML Schema files from the build.
set_source_files_properties (${appleseed_schema_files} PROPERTIES
    HEADER_FILE_ONLY TRUE
)


#--------------------------------------------------------------------------------------------------
# CUDA compilation.
#--------------------------------------------------------------------------------------------------

if (WITH_GPU)
    # Build lists of absolute paths to kernel header files.
    foreach (cuda_header ${appleseed_cuda_headers})
        list (APPEND abs_cuda_headers_sources ${PROJECT_SOURCE_DIR}/src/appleseed/${cuda_header})
    endforeach ()

    # Build the sandbox destination directory path.
    set (kernels_dir "${PROJECT_SOURCE_DIR}/sandbox/kernels")

    # Compile PTX into the sandbox.
    foreach (ptx_source ${appleseed_ptx_sources})
        # Build the .ptx destination file name.
        get_filename_component (ptx_source_we ${ptx_source} NAME_WE)
        set (ptx_filename ${ptx_source_we}.ptx)

        add_custom_command (
            OUTPUT ${kernels_dir}/${ptx_filename}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${kernels_dir}
            COMMAND ${CUDA_NVCC_EXECUTABLE}
                "-I${CUDA_INCLUDE_DIR} "
                "-I${OPTIX_INCLUDE_DIR} "
                "-I${PROJECT_SOURCE_DIR}/src/appleseed "
                -m64 --std=c++11 -O3 --use_fast_math --expt-relaxed-constexpr
                -ptx
                -arch sm_30
                ${PROJECT_SOURCE_DIR}/src/appleseed/${ptx_source}
                -o ${kernels_dir}/${ptx_filename}
            MAIN_DEPENDENCY ${PROJECT_SOURCE_DIR}/src/appleseed/${ptx_source}
            DEPENDS ${abs_cuda_headers_sources}
        )

        list (APPEND appleseed_ptx_files ${kernels_dir}/${ptx_filename})
    endforeach ()

    # Compile CUDA kernels into the sandbox.
    foreach (cuda_source ${appleseed_cuda_sources})
        # Build the .cu destination file name.
        get_filename_component (cuda_source_we ${cuda_source} NAME_WE)
        set (fatbin_filename ${cuda_source_we}.fatbin)

        add_custom_command (
            OUTPUT ${kernels_dir}/${fatbin_filename}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${kernels_dir}
            COMMAND ${CUDA_NVCC_EXECUTABLE}
                "-I${CUDA_INCLUDE_DIR} "
                "-I${PROJECT_SOURCE_DIR}/src/appleseed "
                -m64 --std=c++11 -O3 --use_fast_math --expt-relaxed-constexpr
                -fatbin
                -gencode=arch=compute_30,code=sm_30
                -gencode=arch=compute_50,code=sm_50
                -gencode=arch=compute_60,code=sm_60
                ${PROJECT_SOURCE_DIR}/src/appleseed/${cuda_source}
                -o ${kernels_dir}/${fatbin_filename}
            MAIN_DEPENDENCY ${PROJECT_SOURCE_DIR}/src/appleseed/${cuda_source}
            DEPENDS ${abs_cuda_headers_sources}
        )

        list (APPEND appleseed_fatbin_files ${kernels_dir}/${fatbin_filename})
    endforeach ()
endif ()


#--------------------------------------------------------------------------------------------------
# Target.
#--------------------------------------------------------------------------------------------------

add_library (appleseed SHARED
    ${appleseed_sources}
    ${appleseed_ptx_files}
    ${appleseed_fatbin_files}
)

set_target_properties (appleseed PROPERTIES FOLDER "Core")

if (USE_VISIBILITY_MAP)
    set_target_properties (appleseed PROPERTIES
        LINK_FLAGS "-Wl,--version-script=${PROJECT_SOURCE_DIR}/src/appleseed/libappleseed.map"
    )
endif ()


#--------------------------------------------------------------------------------------------------
# Preprocessor definitions.
#--------------------------------------------------------------------------------------------------

apply_preprocessor_definitions (appleseed)

append_custom_preprocessor_definitions (appleseed
    APPLESEED_ENABLE_IMATH_INTEROP
)


#--------------------------------------------------------------------------------------------------
# Static libraries.
# Static libraries must be specified in order of reverse-dependency.
#--------------------------------------------------------------------------------------------------

set(APPLESEED_LIBS
	bcd
	${Boost_LIBRARIES}
	murmurhash3
	${CMAKE_DL_LIBS}
	Threads::Threads
	XercesC::XercesC
	Imath::Imath OpenEXR::OpenEXR OpenEXR::Iex OpenEXR::IlmThread
	${ZLIB_LIBRARIES}
	${LZ4_LIBRARY}
	#OpenColorIO::OpenColorIO
	OpenImageIO::OpenImageIO_Util
	OpenImageIO::OpenImageIO
	OSL::oslcomp
	OSL::oslexec
	OSL::oslquery
	)
if (WITH_EMBREE)
	set(APPLESEED_LIBS ${APPLESEED_LIBS} embree)
endif ()

if (WITH_GPU)
	set(APPLESEED_LIBS ${APPLESEED_LIBS}
        ${OPTIX_LIBRARIES}
        ${CUDA_CUDA_LIBRARY}
    )
endif ()

target_link_libraries(appleseed ${APPLESEED_LIBS})

#--------------------------------------------------------------------------------------------------
# Pre-build commands.
#--------------------------------------------------------------------------------------------------

# Copy OSL and OIIO tools to the sandbox.
add_custom_command (TARGET appleseed
	PRE_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy ${OSL_QUERY_INFO} ${PROJECT_SOURCE_DIR}/sandbox/bin/
	)
add_custom_command (TARGET appleseed
	PRE_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:OpenImageIO::maketx> ${PROJECT_SOURCE_DIR}/sandbox/bin/
	)
add_custom_command (TARGET appleseed
	PRE_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:OpenImageIO::oiiotool> ${PROJECT_SOURCE_DIR}/sandbox/bin/
	)
add_custom_command (TARGET appleseed
	PRE_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:OpenImageIO::idiff> ${PROJECT_SOURCE_DIR}/sandbox/bin/
	)

#--------------------------------------------------------------------------------------------------
# Post-build commands.
#--------------------------------------------------------------------------------------------------

add_copy_target_lib_to_sandbox_command (appleseed)

# Copy XML schema files to the sandbox.
foreach (schema_file ${appleseed_schema_files})
    add_custom_command (TARGET appleseed POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/${schema_file} ${PROJECT_SOURCE_DIR}/sandbox/schemas/
    )
endforeach ()


#--------------------------------------------------------------------------------------------------
# Installation.
#--------------------------------------------------------------------------------------------------

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    install (TARGETS appleseed
        RUNTIME
        DESTINATION bin
    )

    install (TARGETS appleseed
        ARCHIVE
        DESTINATION lib
    )
else ()
    install (TARGETS appleseed
        DESTINATION lib
    )
endif ()

if (WITH_GPU)
    install (FILES ${appleseed_ptx_files} DESTINATION kernels)
    install (FILES ${appleseed_fatbin_files} DESTINATION kernels)
endif ()
