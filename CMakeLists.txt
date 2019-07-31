#
# Created 190622 lynnl
#

cmake_minimum_required(VERSION 2.8)

project(csentry)

if (${CMAKE_MAJOR_VERSION} GREATER 2)
# see: https://cmake.org/cmake/help/latest/policy/CMP0042.html
    cmake_policy(SET CMP0042 NEW)
endif (${CMAKE_MAJOR_VERSION} GREATER 2)

cmake_host_system_information(RESULT CONST_HOSTNAME QUERY HOSTNAME)

cmake_host_system_information(RESULT CONST_PHYS_MEM_TOTAL QUERY TOTAL_PHYSICAL_MEMORY)
cmake_host_system_information(RESULT CONST_PHYS_MEM_FREE QUERY AVAILABLE_PHYSICAL_MEMORY)
cmake_host_system_information(RESULT CONST_PHYS_CORES QUERY NUMBER_OF_PHYSICAL_CORES)
cmake_host_system_information(RESULT CONST_LOGI_CORES QUERY NUMBER_OF_LOGICAL_CORES)

if (${UNIX})
    find_program(UNAME_TOOL uname)
    execute_process(
            COMMAND ${UNAME_TOOL} -srvm
            OUTPUT_VARIABLE CONST_UNAME
            OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()

if (${APPLE})
    find_program(SYSCTL_TOOL sysctl)
    execute_process(
            COMMAND ${SYSCTL_TOOL} -n hw.model
            OUTPUT_VARIABLE CONST_HW_MODEL
            OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()

if (${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.10.0")
    cmake_host_system_information(RESULT CONST_OS_RELEASE QUERY OS_RELEASE)
    string(STRIP "${CONST_OS_RELEASE}" CONST_OS_RELEASE)

    cmake_host_system_information(RESULT CONST_OS_VERSION QUERY OS_VERSION)
    string(STRIP "${CONST_OS_VERSION}" CONST_OS_VERSION)
endif()

include_directories("/usr/local/include")

# Add -fno-inline if you suppose to C99 and later standards
set(DEBUG_ARGS -DDEBUG -g -O0)
#add_definitions(-Wall -Wextra -Werror -pedantic)
add_definitions(-Wall -Wextra -Werror)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type (default Debug)")
    add_definitions(${DEBUG_ARGS})
elseif (CMAKE_BUILD_TYPE MATCHES Debug)
    add_definitions(${DEBUG_ARGS})
endif ()
# Release build alreay set "-O2 -DNDEBUG"

# see: https://cmake.org/cmake/help/latest/module/CheckCCompilerFlag.html
include(CheckCCompilerFlag)
# XXX: COMPILER_SUPPORTS_C99 var name will trigger a bug in macOS CMake(3.14.5) ?!
check_c_compiler_flag("-std=c99" C99_SUPPORT)
if (C99_SUPPORT)
    add_definitions(-std=c99)
else ()
    message(FATAL_ERROR "The compiler ${CMAKE_C_COMPILER} have no c99 support, "
                        "Please use a newer C/C++ compiler.")
endif ()

configure_file(src/constants.h.in ${CMAKE_CURRENT_BINARY_DIR}/src/constants.h)

include_directories(include)

find_package(curl REQUIRED)
find_package(cjson REQUIRED)

set(LIBS curl cjson)

add_executable(test
    include/csentry.h
    src/csentry.c
    src/curl_ez.h
    src/utils.h
    src/utils.c
    tests/test.c
)
target_link_libraries(test ${LIBS})

