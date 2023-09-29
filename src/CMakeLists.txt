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

#--------------------------------------------------------------------------------------------------
# Include paths.
#--------------------------------------------------------------------------------------------------

# Common include paths.
include_directories(
  src/thirdparty/bcd
  src/thirdparty/glad/include
  src/thirdparty/murmurhash3
  ${PROJECT_SOURCE_DIR}/include/appleseed
  ${PROJECT_BINARY_DIR}/include/appleseed
  )

if (WITH_EMBREE)
  include_directories(${EMBREE_INCLUDE_DIRS})
endif ()

include_directories(
  ${IMATH_INCLUDE_DIRS}
  ${IMATH_INCLUDE_DIRS}/OpenEXR/
  ${LZ4_INCLUDE_DIR}
  ${OPENCOLORIO_INCLUDE_DIRS}
  ${OPENEXR_INCLUDE_DIRS}
  ${OPENEXR_INCLUDE_DIRS}/OpenEXR/
  ${OPENIMAGEIO_INCLUDE_DIRS}
  ${OSL_INCLUDE_DIRS}
  ${XERCES_INCLUDE_DIRS}
  ${ZLIB_INCLUDE_DIRS}
  )

#--------------------------------------------------------------------------------------------------
# Products.
#--------------------------------------------------------------------------------------------------

add_subdirectory(thirdparty/bcd)
add_subdirectory(thirdparty/glad)
add_subdirectory(thirdparty/murmurhash3)

add_subdirectory(appleseed)
add_subdirectory(appleseed.shaders)

if (WITH_CLI OR WITH_STUDIO OR WITH_BENCH OR WITH_TOOLS)
  add_subdirectory(appleseed.common)
endif ()

if (WITH_STUDIO OR WITH_BENCH)
  add_subdirectory(appleseed.qtcommon)
endif ()

if (WITH_CLI)
  add_subdirectory(appleseed.cli)
endif ()

if (WITH_PYTHON2_BINDINGS)
  set(Python_ADDITIONAL_VERSIONS 2.7)
  find_package(PythonLibs REQUIRED)

  # Splitting version string into list.
  string(REGEX MATCHALL "[0123456789]+" PYTHON_VERSION_LIST "${PYTHONLIBS_VERSION_STRING}")

  if (PYTHON_VERSION_LIST)
    # Accessing major and minor version numbers with the version list we got.
    list(GET PYTHON_VERSION_LIST 0 PYTHON_MAJOR_VERSION)
    list(GET PYTHON_VERSION_LIST 1 PYTHON_MINOR_VERSION)
  else()
    # Default to latest Python 2 release if version string cannot be read.
    message(STATUS "Cannot find Python version; using Python 2.7 by default.")
    set(PYTHON_MAJOR_VERSION "2")
    set(PYTHON_MINOR_VERSION "7")
  endif ()
endif ()

if (WITH_PYTHON2_BINDINGS OR WITH_PYTHON3_BINDINGS)
  add_subdirectory(appleseed.python)
endif ()

if (WITH_STUDIO)
  if (NOT WITH_PYTHON2_BINDINGS)
    message(FATAL_ERROR "Option WITH_PYTHON2_BINDINGS should be \"ON\" to build appleseed.studio.")
  endif ()
  add_subdirectory(appleseed.studio)
endif ()

if (WITH_BENCH)
  add_subdirectory(appleseed.bench)
endif ()

if (WITH_TOOLS)
  add_subdirectory(tools/animatecamera)
  add_subdirectory(tools/convertmeshfile)
  add_subdirectory(tools/denoiser)
  add_subdirectory(tools/dumpmetadata)
  add_subdirectory(tools/makefluffy)
  add_subdirectory(tools/projecttool)
endif ()

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
