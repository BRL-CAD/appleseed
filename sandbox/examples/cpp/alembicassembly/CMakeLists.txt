
#
# This source file is part of appleseed.
# Visit https://appleseedhq.net/ for additional information and resources.
#
# This software is released under the MIT license.
#
# Copyright (c) 2017-2018 Esteban Tovagliari, The appleseedhq Organization
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
# CMake configuration.
#--------------------------------------------------------------------------------------------------

cmake_minimum_required (VERSION 3.9 FATAL_ERROR)

project (alembicassembly)

set (CMAKE_MODULE_PATH
    ${PROJECT_SOURCE_DIR}/../../../share/cmake/Modules
    ${PROJECT_SOURCE_DIR}/../../../../cmake/modules
)


#--------------------------------------------------------------------------------------------------
# Build options.
#--------------------------------------------------------------------------------------------------

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    option (USE_FIND_PACKAGE_FOR_OIIO "Use find_package() for OpenImageIO library"         OFF)
    option (USE_FIND_PACKAGE_FOR_OSL  "Use find_package() for OpenShadingLanguage library" OFF)
else ()
    set (USE_FIND_PACKAGE_FOR_OIIO ON)
    set (USE_FIND_PACKAGE_FOR_OSL  ON)
endif ()

option (USE_STATIC_OIIO     "Use static OpenImageIO libraries"          ON)
option (USE_STATIC_OSL      "Use static OpenShadingLanguage libraries"  ON)


#--------------------------------------------------------------------------------------------------
# Compile flags.
#--------------------------------------------------------------------------------------------------

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_CXX_EXTENSIONS OFF)


#--------------------------------------------------------------------------------------------------
# Find packages.
#--------------------------------------------------------------------------------------------------

find_package (Appleseed)
find_package (Imath)
find_package (OpenEXR)
find_package (Alembic)

if (USE_STATIC_OIIO)
    add_definitions (-DOIIO_STATIC_BUILD)
endif ()
if (USE_FIND_PACKAGE_FOR_OIIO)
    find_package (OpenImageIO REQUIRED)
endif ()

if (USE_STATIC_OSL)
    add_definitions (-DOSL_STATIC_BUILD)
endif ()
if (USE_FIND_PACKAGE_FOR_OSL)
    find_package (OSL REQUIRED)
endif ()


#--------------------------------------------------------------------------------------------------
# Include paths.
#--------------------------------------------------------------------------------------------------

include_directories (${APPLESEED_INCLUDE_DIRS})
include_directories (${IMATH_INCLUDE_DIRS})
include_directories (${OPENEXR_INCLUDE_DIRS})
include_directories (${ALEMBIC_INCLUDE_DIR})

if (USE_FIND_PACKAGE_FOR_OIIO)
    include_directories (${OPENIMAGEIO_INCLUDE_DIRS})
else ()
    include_directories (${APPLESEED_DEPS_STAGE_DIR}/oiio-debug/include)
endif ()

if (USE_FIND_PACKAGE_FOR_OSL)
    include_directories (${OSL_INCLUDE_DIRS})
else ()
    include_directories (${APPLESEED_DEPS_STAGE_DIR}/osl-debug/include)
endif ()


#--------------------------------------------------------------------------------------------------
# Preprocessor definitions.
#--------------------------------------------------------------------------------------------------

if (MSVC)
    add_definitions (/D "_CRT_SECURE_NO_WARNINGS")
    add_definitions (/D "_SCL_SECURE_NO_WARNINGS")
endif ()


#--------------------------------------------------------------------------------------------------
# Product.
#--------------------------------------------------------------------------------------------------

add_library (alembicassembly SHARED alembicassembly.cpp)
set_target_properties (alembicassembly PROPERTIES PREFIX "")

target_link_libraries (alembicassembly
    ${APPLESEED_LIBRARIES}
    ${IMATH_LIBRARIES}
    ${OPENEXR_LIBRARIES}
    ${ALEMBIC_ABC_LIBRARY}
)
