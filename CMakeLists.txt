cmake_minimum_required(VERSION 3.22)
project(raylib_stam LANGUAGES C)

include(FetchContent)

set(PLATFORM "Desktop" CACHE STRING "" FORCE)
set(OPENGL_VERSION "4.3" CACHE STRING "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 6.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)

configure_file(shaders/compute_smoke.comp compute_smoke.comp COPYONLY)
file(COPY shaders/fluid DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/shaders")

add_executable(compute_shader_smoke tests/compute_shader_smoke.c)
target_link_libraries(compute_shader_smoke PRIVATE raylib opengl32)

add_library(fluid_sim STATIC src/fluid_sim.c)
target_include_directories(fluid_sim PUBLIC src)
target_link_libraries(fluid_sim PUBLIC raylib PRIVATE opengl32)

add_executable(raylib_stam src/main.c)
target_link_libraries(raylib_stam PRIVATE fluid_sim raylib opengl32)

add_executable(fluid_sim_gpu_tests tests/fluid_sim_gpu.c)
target_link_libraries(fluid_sim_gpu_tests PRIVATE fluid_sim raylib opengl32)

enable_testing()
add_test(NAME compute_shader_smoke COMMAND compute_shader_smoke)
add_test(NAME fluid_sim_gpu COMMAND fluid_sim_gpu_tests)
set_tests_properties(compute_shader_smoke fluid_sim_gpu PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
