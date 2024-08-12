cmake_minimum_required(VERSION 3.10)
project(KeyboardDriver C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED True)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(driver src/driver.c)

find_path(HIDAPI_INCLUDE_DIR NAMES hidapi.h
        PATHS /opt/homebrew/include /usr/local/include /usr/include
        PATH_SUFFIXES hidapi)
find_library(HIDAPI_LIBRARY NAMES hidapi
        PATHS /opt/homebrew/lib /usr/local/lib /usr/lib)

if(NOT HIDAPI_INCLUDE_DIR OR NOT HIDAPI_LIBRARY)
    message(FATAL_ERROR "HIDAPI not found. Please specify its location with -DHIDAPI_ROOT=<path>")
endif()

target_include_directories(driver PRIVATE ${HIDAPI_INCLUDE_DIR})
target_link_libraries(driver PRIVATE ${HIDAPI_LIBRARY})

if(APPLE)
    target_link_libraries(driver PRIVATE "-framework ApplicationServices")
endif()

find_package(Threads REQUIRED)
target_link_libraries(driver PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(driver PRIVATE /W4 /O2 /Oi /Ot /GL /fp:fast)
    target_link_options(driver PRIVATE /LTCG)
else()
    target_compile_options(driver PRIVATE
            -Wall -Wextra -pedantic
            -O3
            -ffast-math
            -flto
            -fomit-frame-pointer
            -march=native
            -mtune=native
    )
    target_link_options(driver PRIVATE -flto)
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT supported OUTPUT error)
if(supported)
    set_property(TARGET driver PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    message(WARNING "IPO is not supported: ${error}")
endif()

message(STATUS "HIDAPI include dir: ${HIDAPI_INCLUDE_DIR}")
message(STATUS "HIDAPI library: ${HIDAPI_LIBRARY}")