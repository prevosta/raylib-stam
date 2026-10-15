cmake_minimum_required(VERSION 3.22)
project(compute_workload_prototype LANGUAGES C CXX)

include(FetchContent)

set(PLATFORM "Desktop" CACHE STRING "" FORCE)
set(OPENGL_VERSION "4.3" CACHE STRING "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
option(PROTOTYPE_REQUEST_NVIDIA "Request the NVIDIA high-performance GPU on Optimus systems" OFF)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 6.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)

configure_file(compute_workload.comp compute_workload.comp COPYONLY)

add_executable(compute_workload_benchmark benchmark.c)
target_link_libraries(compute_workload_benchmark PRIVATE raylib opengl32)

if(PROTOTYPE_REQUEST_NVIDIA)
    target_compile_definitions(compute_workload_benchmark PRIVATE PROTOTYPE_REQUEST_NVIDIA=1)
endif()
