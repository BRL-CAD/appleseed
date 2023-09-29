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

cmake_minimum_required(VERSION 3.19 FATAL_ERROR)

project(appleseed)

# Must come after project().
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)


# Relative paths for standard components
if (NOT DEFINED BIN_DIR)
  set(BIN_DIR bin)
endif (NOT DEFINED BIN_DIR)

if (NOT DEFINED LIB_DIR)
  set(LIB_DIR lib)
endif (NOT DEFINED LIB_DIR)

if (NOT DEFINED INCLUDE_DIR)
  set(INCLUDE_DIR include)
endif (NOT DEFINED INCLUDE_DIR)

include(Util)

include(TargetArch)
target_architecture(CPU_TARGET)

#--------------------------------------------------------------------------------------------------
# Language and compiler features.
#--------------------------------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# TODO - this is a problem, CMake 3.20 and later deprecate this module:
# https://cmake.org/cmake/help/latest/module/WriteCompilerDetectionHeader.html
#
# Need to look at what it was doing and find another solution

#include (WriteCompilerDetectionHeader)
#
#write_compiler_detection_header (
#    FILE ${PROJECT_SOURCE_DIR}/src/appleseed/foundation/platform/compilerfeatures.h
#    PREFIX APPLESEED
#    COMPILERS GNU Clang MSVC AppleClang
#    FEATURES
#        cxx_final
#        cxx_noexcept
#        cxx_constexpr
#        cxx_defaulted_functions
#        cxx_deleted_functions
#        cxx_generalized_initializers
#        cxx_attribute_deprecated
#)

#--------------------------------------------------------------------------------------------------
# Build options.
#--------------------------------------------------------------------------------------------------

option(WITH_CLI                "Build appleseed.cli"                                       ON)
option(WITH_STUDIO             "Build appleseed.studio"                                    ON)
option(WITH_BENCH              "Build appleseed.bench"                                     ON)
option(WITH_TOOLS              "Build appleseed tools"                                     ON)
option(WITH_PYTHON2_BINDINGS   "Build Python bindings for Python 2"                        OFF)
option(WITH_PYTHON3_BINDINGS   "Build Python bindings for Python 3"                        OFF)
option(WITH_EMBREE             "Include support for Embree intersection backend"           OFF)
option(WITH_GPU                "Build GPU support"                                         OFF)
option(WITH_SPECTRAL_SUPPORT   "Include support for spectral colors"                       ON)
option(WITH_DOXYGEN            "Generate API reference with Doxygen"                       OFF)
option(INSTALL_HEADERS         "Install header files"                                      ON)
option(INSTALL_TESTS           "Install unit tests and benchmarks"                         ON)
option(INSTALL_API_EXAMPLES    "Install API examples(requires header files)"              ON)

option(USE_STATIC_BOOST        "Use static Boost libraries"                                ON)
option(USE_STATIC_EMBREE       "Use static Embree libraries"                               ON)
option(USE_STATIC_EXR          "Use static OpenEXR libraries"                              ON)
option(USE_STATIC_OCIO         "Use static OpenColorIO libraries"                          ON)
option(USE_STATIC_OIIO         "Use static OpenImageIO libraries"                          ON)
option(USE_STATIC_OSL          "Use static OpenShadingLanguage libraries"                  ON)

option(WARNINGS_AS_ERRORS      "Treat compiler warnings as errors"                         ON)
option(HIDE_SYMBOLS            "When using gcc, hide symbols not on the public API"        ON)

# SIMD options.
option(USE_SSE                 "Use instruction sets up to and including SSE2"             ON)
option(USE_SSE42               "Use instruction sets up to and including SSE4.2"           ${USE_SSE})
option(USE_AVX                 "Use instruction sets up to and including AVX"              OFF)
option(USE_AVX2                "Use instruction sets up to and including AVX2"             OFF)
option(USE_F16C                "Use F16C instruction set"                                  OFF)

if (WITH_SPECTRAL_SUPPORT)
  add_definitions(-DAPPLESEED_WITH_SPECTRAL_SUPPORT)
endif ()


#--------------------------------------------------------------------------------------------------
# RPath Settings
#--------------------------------------------------------------------------------------------------
if (NOT DEFINED CMAKE_BUILD_RPATH)
  if (CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_RPATH "${CMAKE_BINARY_DIR}/$<CONFIG>/${LIB_DIR}")
  else(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_RPATH "${CMAKE_BINARY_DIR}/${LIB_DIR}")
  endif (CMAKE_CONFIGURATION_TYPES)
endif ()
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${LIB_DIR}")

#--------------------------------------------------------------------------------------------------
# Compiler Flags
#--------------------------------------------------------------------------------------------------
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(CompilerFlags)

check_c_flag(Wall)
check_c_flag(Wno-switch)
check_c_flag(Wno-unknown-pragmas)
check_c_flag(Wno-sign-compare)
check_c_flag(Wno-unused-function)
check_c_flag(Wno-strict-aliasing)
check_c_flag(Wno-math-errno)
check_cxx_flag(Wno-reorder)

if (WARNINGS_AS_ERRORS)
  check_c_flag(Werror)
  check_cxx_flag(Werror)
endif (WARNINGS_AS_ERRORS)

if (HIDE_SYMBOLS)
  check_c_flag(fvisibility=hidden)
  check_cxx_flag(fvisibility=hidden)
  check_cxx_flag(fvisibility-inlines-hidden)
endif (HIDE_SYMBOLS)

if ("${CMAKE_BUILD_TYPE}" MATCHES "Release")
  check_c_flag(O3)
  check_c_flag(fgcse-las) # eliminates stores followed by a load to the same positions
  check_c_flag(fgcse-sm)  # moves load/save instructions out of loops, if possible
  check_cxx_flag(O3)

endif ("${CMAKE_BUILD_TYPE}" MATCHES "Release")

if (USE_SSE)
  check_c_flag(msse2 GROUPS ACCEL_C_FLAGS)
  check_cxx_flag(msse2 GROUPS ACCEL_CXX_FLAGS)
endif (USE_SSE)
if (USE_F16C)
  check_c_flag(mf16c GROUPS ACCEL_C_FLAGS)
  check_cxx_flag(mf16c GROUPS ACCEL_CXX_FLAGS)
endif (USE_F16C)
if (USE_SSE42)
  check_c_flag(msse4.2 GROUPS ACCEL_C_FLAGS)
  check_cxx_flag(msse4.2 GROUPS ACCEL_CXX_FLAGS)
endif (USE_SSE42)
if (USE_MAVX)
  check_c_flag(mavx GROUPS ACCEL_C_FLAGS)
  check_cxx_flag(mavx GROUPS ACCEL_CXX_FLAGS)
endif (USE_MAVX)
if (USE_MAVX2)
  check_c_flag(mavx2 GROUPS ACCEL_C_FLAGS)
  check_c_flag(mfma GROUPS ACCEL_C_FLAGS)
  check_c_flag(ffp-contract=off GROUPS ACCEL_C_FLAGS)
  check_cxx_flag(mavx2 GROUPS ACCEL_CXX_FLAGS)
  check_cxx_flag(mfma GROUPS ACCEL_CXX_FLAGS)
  check_cxx_flag(ffp-contract=off GROUPS ACCEL_CXX_FLAGS)
endif (USE_MAVX2)

#--------------------------------------------------------------------------------------------------
# Set version number.
#--------------------------------------------------------------------------------------------------

set(appleseed_version_major    2)
set(appleseed_version_minor    1)
set(appleseed_version_patch    0)
set(appleseed_version_maturity beta)


#--------------------------------------------------------------------------------------------------
# Find packages.
#--------------------------------------------------------------------------------------------------

find_package(Threads REQUIRED)

find_package(Eigen3 REQUIRED)
find_package(lz4 MODULE REQUIRED)
find_package(PNG REQUIRED)
find_package(ZLIB REQUIRED)

set(Boost_MULTITHREADED TRUE)
if (USE_STATIC_BOOST)
  set(Boost_USE_STATIC_LIBS TRUE)
endif ()
find_package(Boost 1.70 REQUIRED COMPONENTS atomic chrono date_time filesystem regex system thread wave)
add_definitions(-DBOOST_FILESYSTEM_VERSION=3 -DBOOST_FILESYSTEM_NO_DEPRECATED)
include_directories(SYSTEM ${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})
find_package(OSL REQUIRED)
find_package(OpenEXR REQUIRED)
find_package(OpenImageIO REQUIRED)

if (WITH_EMBREE)
  set(APPLESEED_WITH_EMBREE ON)
  add_definitions(-DAPPLESEED_WITH_EMBREE)
  if (USE_STATIC_EMBREE)
    add_definitions(-DEMBREE_STATIC_LIB)
  endif ()
  find_package(Embree REQUIRED)
endif ()

if (WITH_GPU)
  set(APPLESEED_WITH_GPU ON)
  add_definitions(-DAPPLESEED_WITH_GPU)
  find_package(CUDA 9.0 REQUIRED)
  find_package(OptiX REQUIRED)
endif ()

if (WITH_STUDIO)
  add_definitions(-DAPPLESEED_WITH_OCIO)
  find_package(OpenColorIO REQUIRED)
endif ()

option(USE_EMBEDDED_XERCES "Don't use external Xerces" OFF)
if (NOT USE_EMBEDDED_XERCES)
  add_definitions(-DAPPLESEED_WITH_EXTERNAL_XERCES)
  find_package(XercesC REQUIRED)
endif (NOT USE_EMBEDDED_XERCES)

#--------------------------------------------------------------------------------------------------
# Preprocessor definitions.
#--------------------------------------------------------------------------------------------------

#
# Definitions common to all configurations.
#

set(predef_common APPLESEED_ENABLE_IMATH_INTEROP)

# $ORIGIN support in rpath.
if (UNIX AND NOT APPLE)
  set(USE_RPATH_ORIGIN TRUE)
endif ()

if ("${CPU_TARGET}" MATCHES "x86")
  set(predef_common ${predef_common} APPLESEED_X86)
endif ()

# SIMD.
if ("${CPU_TARGET}" MATCHES "x86")
  if (USE_AVX2)
    set(APPLESEED_USE_AVX2 ON)
    set(predef_common ${predef_common} APPLESEED_USE_AVX2)
    set(USE_AVX TRUE)
  endif ()
  if (USE_AVX)
    set(APPLESEED_USE_AVX ON)
    set(predef_common ${predef_common} APPLESEED_USE_AVX)
    set(USE_SSE42 TRUE)
  endif ()
  if (USE_SSE42)
    set(APPLESEED_USE_SSE42 ON)
    set(predef_common ${predef_common} APPLESEED_USE_SSE42)
    set(USE_SSE TRUE)
  endif ()
  if (USE_SSE)
    set(APPLESEED_USE_SSE ON)
    set(predef_common ${predef_common} APPLESEED_USE_SSE)
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P MATCHES 4)
      message(WARNING "Building appleseed with SSE/SSE2 instruction sets on 32-bit Linux is not supported.")
    endif ()
  endif ()
  if (USE_F16C)
    set(APPLESEED_USE_F16C ON)
    set(predef_common ${predef_common} APPLESEED_USE_F16C)
  endif ()
endif ()

# Debug configuration.
set(predef_debug ${predef_debug} _DEBUG APPLESEED_DEBUG)

# Release configuration.
set(predef_release ${predef_release} NDEBUG APPLESEED_RELEASE)

# Ship configuration.
set(predef_ship
  ${predef_ship}
  ${predef_release}     # the Ship configuration inherits from the Release configuration
  APPLESEED_PROFILE
  )

# Profile configuration.
set(predef_profile
  ${predef_profile}
  ${predef_ship}        # the Profile configuration inherits from the Ship configuration
  APPLESEED_SHIP
  )

# A convenience function to apply the preprocessor definitions to all configurations of a given target.
function(apply_preprocessor_definitions target)
  set_property(TARGET ${target} APPEND PROPERTY
    COMPILE_DEFINITIONS ${predef_common}
    )
  set_property(TARGET ${target} APPEND PROPERTY
    COMPILE_DEFINITIONS_DEBUG ${predef_debug}
    )
  set_property(TARGET ${target} APPEND PROPERTY
    COMPILE_DEFINITIONS_RELEASE ${predef_release}
    )
  set_property(TARGET ${target} APPEND PROPERTY
    COMPILE_DEFINITIONS_SHIP ${predef_ship}
    )
  set_property(TARGET ${target} APPEND PROPERTY
    COMPILE_DEFINITIONS_PROFILE ${predef_profile}
    )
endfunction()

# A convenience macro to append custom preprocessor definitions to all configurations of a given target.
function(append_custom_preprocessor_definitions target first_definition)
  set(definitions ${first_definition} ${ARGN})
  set_property(TARGET ${target} APPEND PROPERTY
    COMPILE_DEFINITIONS_DEBUG ${definitions}
    )
  set_property(TARGET ${target} APPEND PROPERTY
    COMPILE_DEFINITIONS_RELEASE ${definitions}
    )
  set_property(TARGET ${target} APPEND PROPERTY
    COMPILE_DEFINITIONS_SHIP ${definitions}
    )
  set_property(TARGET ${target} APPEND PROPERTY
    COMPILE_DEFINITIONS_PROFILE ${definitions}
    )
endfunction()

#--------------------------------------------------------------------------------------------------
# Compilation/linking flags.
#--------------------------------------------------------------------------------------------------

# Debug configuration.
set(c_compiler_flags_debug             ${c_compiler_flags_common}          ${c_compiler_flags_debug})
set(cxx_compiler_flags_debug           ${cxx_compiler_flags_common}        ${cxx_compiler_flags_debug})
set(exe_linker_flags_debug             ${exe_linker_flags_common}          ${exe_linker_flags_debug})
set(shared_lib_linker_flags_debug      ${shared_lib_linker_flags_common}   ${shared_lib_linker_flags_debug})
set(static_lib_linker_flags_debug      ${static_lib_linker_flags_common}   ${static_lib_linker_flags_debug})

# Release configuration.
set(c_compiler_flags_release           ${c_compiler_flags_common}          ${c_compiler_flags_release})
set(cxx_compiler_flags_release         ${cxx_compiler_flags_common}        ${cxx_compiler_flags_release})
set(exe_linker_flags_release           ${exe_linker_flags_common}          ${exe_linker_flags_release})
set(shared_lib_linker_flags_release    ${shared_lib_linker_flags_common}   ${shared_lib_linker_flags_release})
set(static_lib_linker_flags_release    ${static_lib_linker_flags_common}   ${static_lib_linker_flags_release})

# Ship configuration(inherits from the Release configuration).
set(c_compiler_flags_ship              ${c_compiler_flags_release}         ${c_compiler_flags_ship})
set(cxx_compiler_flags_ship            ${cxx_compiler_flags_release}       ${cxx_compiler_flags_ship})
set(exe_linker_flags_ship              ${exe_linker_flags_release}         ${exe_linker_flags_ship})
set(shared_lib_linker_flags_ship       ${shared_lib_linker_flags_release}  ${shared_lib_linker_flags_ship})
set(static_lib_linker_flags_ship       ${static_lib_linker_flags_release}  ${static_lib_linker_flags_ship})

# Profile configuration(inherits from the Release configuration).
set(c_compiler_flags_profile           ${c_compiler_flags_release}         ${c_compiler_flags_profile})
set(cxx_compiler_flags_profile         ${cxx_compiler_flags_release}       ${cxx_compiler_flags_profile})
set(exe_linker_flags_profile           ${exe_linker_flags_release}         ${exe_linker_flags_profile})
set(shared_lib_linker_flags_profile    ${shared_lib_linker_flags_release}  ${shared_lib_linker_flags_profile})
set(static_lib_linker_flags_profile    ${static_lib_linker_flags_release}  ${static_lib_linker_flags_profile})


# Convert a semicolon-separated list to a whitespace-separated string.
macro (convert_list_to_string output_string input_list)
  foreach (item ${input_list})
    if (DEFINED ${output_string})
      set (${output_string} "${${output_string}} ${item}")
    else ()
      set (${output_string} "${item}")
    endif ()
  endforeach ()
endmacro ()

# Acceleration flags are for all configs
convert_list_to_string(CMAKE_C_FLAGS                       "${ACCEL_C_FLAGS}")
convert_list_to_string(CMAKE_CXX_FLAGS                     "${ACCEL_CXX_FLAGS}")

# Apply Debug configuration compilation/linking flags.
convert_list_to_string(CMAKE_C_FLAGS_DEBUG                 "${c_compiler_flags_debug}")
convert_list_to_string(CMAKE_CXX_FLAGS_DEBUG               "${c_compiler_flags_debug} ${cxx_compiler_flags_debug}")
convert_list_to_string(CMAKE_EXE_LINKER_FLAGS_DEBUG        "${exe_linker_flags_debug}")
convert_list_to_string(CMAKE_SHARED_LINKER_FLAGS_DEBUG     "${shared_lib_linker_flags_debug}")
convert_list_to_string(CMAKE_MODULE_LINKER_FLAGS_DEBUG     "${shared_lib_linker_flags_debug}")

# Apply Release configuration compilation/linking flags.
convert_list_to_string(CMAKE_C_FLAGS_RELEASE               "${c_compiler_flags_release}")
convert_list_to_string(CMAKE_CXX_FLAGS_RELEASE             "${c_compiler_flags_release} ${cxx_compiler_flags_release}")
convert_list_to_string(CMAKE_EXE_LINKER_FLAGS_RELEASE      "${exe_linker_flags_release}")
convert_list_to_string(CMAKE_SHARED_LINKER_FLAGS_RELEASE   "${shared_lib_linker_flags_release}")
convert_list_to_string(CMAKE_MODULE_LINKER_FLAGS_RELEASE   "${shared_lib_linker_flags_release}")

# Apply Ship configuration compilation/linking flags.
convert_list_to_string(CMAKE_C_FLAGS_SHIP                  "${c_compiler_flags_ship}")
convert_list_to_string(CMAKE_CXX_FLAGS_SHIP                "${c_compiler_flags_ship} ${cxx_compiler_flags_ship}")
convert_list_to_string(CMAKE_EXE_LINKER_FLAGS_SHIP         "${exe_linker_flags_ship}")
convert_list_to_string(CMAKE_SHARED_LINKER_FLAGS_SHIP      "${shared_lib_linker_flags_ship}")
convert_list_to_string(CMAKE_MODULE_LINKER_FLAGS_SHIP      "${shared_lib_linker_flags_ship}")

# Apply Profile configuration compilation/linking flags.
convert_list_to_string(CMAKE_C_FLAGS_PROFILE               "${c_compiler_flags_profile}")
convert_list_to_string(CMAKE_CXX_FLAGS_PROFILE             "${c_compiler_flags_profile} ${cxx_compiler_flags_profile}")
convert_list_to_string(CMAKE_EXE_LINKER_FLAGS_PROFILE      "${exe_linker_flags_profile}")
convert_list_to_string(CMAKE_SHARED_LINKER_FLAGS_PROFILE   "${shared_lib_linker_flags_profile}")
convert_list_to_string(CMAKE_MODULE_LINKER_FLAGS_PROFILE   "${shared_lib_linker_flags_profile}")

# A convenience macro to apply the static library linker flags to all configurations of a given target.
macro(apply_static_lib_linker_flags target)
  set_property(TARGET ${target} PROPERTY
    STATIC_LIBRARY_FLAGS_DEBUG ${static_lib_linker_flags_debug}
    )
  set_property(TARGET ${target} PROPERTY
    STATIC_LIBRARY_FLAGS_RELEASE ${static_lib_linker_flags_release}
    )
  set_property(TARGET ${target} PROPERTY
    STATIC_LIBRARY_FLAGS_SHIP ${static_lib_linker_flags_ship}
    )
  set_property(TARGET ${target} PROPERTY
    STATIC_LIBRARY_FLAGS_PROFILE ${static_lib_linker_flags_profile}
    )
endmacro()

#--------------------------------------------------------------------------------------------------
# Main code.
#--------------------------------------------------------------------------------------------------
add_subdirectory(sandbox)
add_subdirectory(include)
add_subdirectory(src)

#--------------------------------------------------------------------------------------------------
# Documentation.
#--------------------------------------------------------------------------------------------------

if (WITH_DOXYGEN)
  find_package(Doxygen)

  if (DOXYGEN_FOUND)
    configure_file(
      ${PROJECT_SOURCE_DIR}/scripts/appleseed.doxyfile.in
      ${PROJECT_BINARY_DIR}/appleseed.doxyfile
      @ONLY
      )

    add_custom_target(
      genarate_api_reference ${DOXYGEN_EXECUTABLE} ${PROJECT_BINARY_DIR}/appleseed.doxyfile
      WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
      VERBATIM
      )

    set_target_properties(genarate_api_reference PROPERTIES FOLDER "Contents")
    set_target_progress_message(genarate_api_reference "Generating API reference with Doxygen...")
  endif ()
endif ()


#--------------------------------------------------------------------------------------------------
# Installation.
#--------------------------------------------------------------------------------------------------

install(
  FILES
  scripts/cleanmany.py
  scripts/convertmany.py
  scripts/rendermanager.py
  scripts/rendermany.py
  scripts/rendernode.py
  ${OSL_COMPILER}
  ${OSL_QUERY_INFO}
  ${OSL_MAKETX}
  ${OPENIMAGEIO_OIIOTOOL}
  ${OPENIMAGEIO_IDIFF}
  DESTINATION bin
  )

install(
  CODE "FILE(MAKE_DIRECTORY ${CMAKE_INSTALL_PREFIX}/images)"
  CODE "FILE(MAKE_DIRECTORY ${CMAKE_INSTALL_PREFIX}/images/autosave)"
  )

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
