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

# CMake provides us with a way to set fPIC - use it.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Print a progress message when a target is about to be built.
macro (set_target_progress_message target message)
	add_custom_command (
		TARGET ${target}
		PRE_BUILD
		COMMAND ${CMAKE_COMMAND} -E echo ${message}
		)
endmacro()

# Replace slashes by backslashes in a string.
macro (slashes_to_backslashes output input)
	string (REGEX REPLACE "/" "\\\\" ${output} ${input})
endmacro ()

# Filter a list of items using a regular expression.
macro (filter_list output_list input_list regex)
	foreach (item ${input_list})
		if (${item} MATCHES ${regex})
			list (APPEND ${output_list} ${item})
		endif ()
	endforeach ()
endmacro ()

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

# Assign a whitespace-separated string to a variable, given a list.
macro (set_to_string output_variable first_arg)
	set (arg_list ${first_arg} ${ARGN})
	convert_list_to_string (${output_variable} "${arg_list}")
endmacro ()

#
# A variant of the QT5_WRAP_CPP macro designed to moc .cpp files.
#
# It differs from the original macro in two ways:
#
#   1.  It generates a file of the form moc_cpp_XXX.cxx for a given input file XXX.cpp,
#       whereas the original macro generates a file of the form moc_XXX.cxx for a given
#       input file XXX.h.
#
#   2.  It adds a dependency between the input file XXX.cpp and the generated file so
#       that the generated file gets rebuilt whenever the input file is modified. This
#       is necessary because the generated file is to be included in the input file.
#
# The original QT5_WRAP_CPP macro can be found in qt5/lib/cmake/Qt5Core/Qt5CoreMacros.cmake
#

macro (QT5_WRAP_CPP_CPLUSPLUS_FILES outfiles)
	QT5_GET_MOC_FLAGS (moc_flags)

	set (options)
	set (oneValueArgs TARGET)
	set (multiValueArgs OPTIONS DEPENDS)

	cmake_parse_arguments (_WRAP_CPP "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

	set (moc_files ${_WRAP_CPP_UNPARSED_ARGUMENTS})
	set (moc_options ${_WRAP_CPP_OPTIONS})
	set (moc_target ${_WRAP_CPP_TARGET})
	set (moc_depends ${_WRAP_CPP_DEPENDS})

	foreach (it ${moc_files})
		get_filename_component (it ${it} ABSOLUTE)
		QT5_MAKE_OUTPUT_FILE (${it} moc_cpp_ cxx outfile)
		QT5_CREATE_MOC_COMMAND (${it} ${outfile} "${moc_flags}" "${moc_options}" "${moc_target}" "${moc_depends}")
		set (${outfiles} ${${outfiles}} ${outfile})
		set_source_files_properties (${it} PROPERTIES OBJECT_DEPENDS ${outfile})
	endforeach (it)
endmacro ()

#--------------------------------------------------------------------------------------------------
# Copy a target binary to the sandbox.
#--------------------------------------------------------------------------------------------------

macro (get_sandbox_bin_path path)
	file(TO_CMAKE_PATH ${PROJECT_SOURCE_DIR}/sandbox/bin/${CMAKE_BUILD_TYPE} ${path})
endmacro ()

macro (get_sandbox_lib_path path)
	file(TO_CMAKE_PATH ${PROJECT_SOURCE_DIR}/sandbox/lib/${CMAKE_BUILD_TYPE} ${path})
endmacro ()

macro (get_sandbox_py_path path)
	get_sandbox_lib_path (${path})
	file(TO_MAKE_PATH ${${path}}/python ${path})
endmacro ()

macro (get_relative_path_from_module_name path name)
	string (REPLACE "." "/" ${path} ${name})
endmacro ()

macro (add_copy_target_exe_to_sandbox_command target)

	get_sandbox_bin_path (bin_path)

	add_custom_command (TARGET ${target} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory ${bin_path}
		COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${target}> ${bin_path}
		)
endmacro ()

macro (add_copy_target_lib_to_sandbox_command target)
	get_sandbox_lib_path (lib_path)

	add_custom_command (TARGET ${target} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory ${lib_path}
		COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${target}> ${lib_path}
		)
endmacro ()

macro (add_copy_target_to_sandbox_py_module_command target module_name)
	get_sandbox_py_path (py_path)
	get_relative_path_from_module_name (relative_module_path ${module_name})
	set (module_path "${py_path}/${relative_module_path}")

	add_custom_command (TARGET ${target} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory ${module_path}
		COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${target}> ${module_path}
		)
endmacro ()

macro (add_copy_py_file_to_sandbox_py_module_command py_src module_name)
	get_sandbox_py_path (py_path)
	get_relative_path_from_module_name (relative_module_path ${module_name})
	set (module_path "${py_path}/${relative_module_path}")

	add_custom_command (TARGET appleseed.python.copy_py_files POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory ${module_path}
		COMMAND ${CMAKE_COMMAND} -E copy ${py_src} ${module_path}
		)
endmacro ()

macro (add_copy_dir_to_sandbox_py_module_command py_dir module_name)
	get_sandbox_py_path (py_path)
	get_relative_path_from_module_name (relative_module_path ${module_name})
	set (module_path "${py_path}/${relative_module_path}")

	add_custom_command (TARGET appleseed.python.copy_py_files POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory ${module_path}
		COMMAND ${CMAKE_COMMAND} -E copy -r ${py_dir} ${module_path}
		)
endmacro ()

macro (add_copy_studio_py_file_to_sandbox_py_module_command py_src module_name)
	get_sandbox_py_path (py_path)
	get_relative_path_from_module_name (relative_module_path ${module_name})
	set (module_path "${py_path}/${relative_module_path}")

	add_custom_command (TARGET appleseed.studio.copy_py_files POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory ${module_path}
		COMMAND ${CMAKE_COMMAND} -E copy ${py_src} ${module_path}
		)
endmacro ()
