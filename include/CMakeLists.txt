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
