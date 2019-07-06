#
# Created 190622 lynnl
#

cmake_minimum_required(VERSION 2.8)

project(csentry)

if (${CMAKE_MAJOR_VERSION} GREATER 2)
# see: https://cmake.org/cmake/help/latest/policy/CMP0042.html
    cmake_policy(SET CMP0042 NEW)
endif (${CMAKE_MAJOR_VERSION} GREATER 2)

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
check_c_compiler_flag("-std=c99" C89_SUPPORT)
if (C89_SUPPORT)
    add_definitions(-std=c99)
else ()
    message(FATAL_ERROR "The compiler ${CMAKE_C_COMPILER} have no c99 support, "
                        "Please use a newer C/C++ compiler.")
endif ()

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

