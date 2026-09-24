cmake_minimum_required(VERSION 3.16)
project(DumpSource2 CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(${PROJECT_SOURCE_DIR}/cmake/platform.cmake)
include_directories(src/main)

add_subdirectory(vendor)
add_subdirectory(src/main)
