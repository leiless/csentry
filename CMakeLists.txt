#
# Created 190622 lynnl
#

cmake_minimum_required(VERSION 3.0)

project(csentry VERSION 0.1)

# see: https://cmake.org/cmake/help/latest/policy/CMP0042.html
cmake_policy(SET CMP0042 NEW)

set(DEBUG_ARGS -DDEBUG -g -O0 -fno-inline)
add_definitions(-Wall -Wextra -Werror -pedantic)
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

include_directories(include)

find_package(curl REQUIRED)

add_executable(csentry
        include/csentry.h
        src/utils.h
    src/csentry.c
)

