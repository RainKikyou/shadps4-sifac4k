# SPDX-FileCopyrightText: 2020 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

set(SOURCE_FILE ${CMAKE_ARGV3})
set(HEADER_FILE ${CMAKE_ARGV4})
set(INPUT_FILE ${CMAKE_ARGV5})

get_filename_component(CONTENTS_NAME ${SOURCE_FILE} NAME)
string(REPLACE "." "_" CONTENTS_NAME ${CONTENTS_NAME})
string(TOUPPER ${CONTENTS_NAME} CONTENTS_NAME)

# Function to recursively parse #include directives and replace them with file contents
function(parse_includes file_path output_content)
    file(READ ${file_path} file_content)
    # This regex includes \n at the begin to (hackish) avoid including comments
    string(REGEX MATCHALL "\n#include +\"[^\"]+\"" includes "${file_content}")

    set(parsed_content "${file_content}")
    foreach (include_match ${includes})
        string(REGEX MATCH "\"([^\"]+)\"" _ "${include_match}")
        set(include_file ${CMAKE_MATCH_1})
        get_filename_component(include_full_path "${file_path}" DIRECTORY)
        set(include_full_path "${include_full_path}/${include_file}")

        if (NOT EXISTS "${include_full_path}")
            message(FATAL_ERROR "Included file not found: ${include_full_path} from ${file_path}")
        endif ()

        parse_includes("${include_full_path}" sub_content)
        string(REPLACE "${include_match}" "\n${sub_content}" parsed_content "${parsed_content}")
    endforeach ()
    set(${output_content} "${parsed_content}" PARENT_SCOPE)
endfunction()

parse_includes("${SOURCE_FILE}" CONTENTS)

get_filename_component(OUTPUT_DIR ${HEADER_FILE} DIRECTORY)
file(MAKE_DIRECTORY ${OUTPUT_DIR})

set(SHADER_CONTENTS "\n${CONTENTS}\n")
string(LENGTH "${SHADER_CONTENTS}" CONTENTS_LENGTH)
set(CHUNK_SIZE 1024)
set(OFFSET 0)

file(WRITE ${HEADER_FILE} "#pragma once\n\n#include <string>\n\nnamespace HostShaders {\n\ninline const std::string ${CONTENTS_NAME} = [] {\n    std::string result;\n")
while (OFFSET LESS CONTENTS_LENGTH)
    math(EXPR REMAINING "${CONTENTS_LENGTH} - ${OFFSET}")
    if (REMAINING LESS CHUNK_SIZE)
        set(CURRENT_SIZE ${REMAINING})
    else()
        set(CURRENT_SIZE ${CHUNK_SIZE})
    endif()
    string(SUBSTRING "${SHADER_CONTENTS}" ${OFFSET} ${CURRENT_SIZE} CHUNK)
    file(APPEND ${HEADER_FILE} "    result += R\"shader_src(${CHUNK})shader_src\";\n")
    math(EXPR OFFSET "${OFFSET} + ${CURRENT_SIZE}")
endwhile()
file(APPEND ${HEADER_FILE} "    return result;\n}();\n\n} // namespace HostShaders\n")
