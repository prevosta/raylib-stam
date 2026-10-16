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
file(GLOB FLUID_SHADER_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/fluid/*.comp"
)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${FLUID_SHADER_FILES})
file(COPY shaders/fluid DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/shaders")

add_executable(compute_shader_smoke tests/compute_shader_smoke.c)
target_link_libraries(compute_shader_smoke PRIVATE raylib opengl32)

add_library(fluid_sim STATIC src/fluid_sim.c)
target_include_directories(fluid_sim PUBLIC src)
target_link_libraries(fluid_sim PUBLIC raylib PRIVATE opengl32)

add_library(performance_report STATIC src/performance_report.c)
target_include_directories(performance_report PUBLIC src)

add_executable(raylib_stam src/main.c)
target_link_libraries(raylib_stam PRIVATE fluid_sim performance_report raylib opengl32)

add_executable(fluid_sim_gpu_tests tests/fluid_sim_gpu.c)
target_link_libraries(fluid_sim_gpu_tests PRIVATE fluid_sim raylib opengl32)

add_executable(mouse_mapping_tests tests/mouse_mapping.c)
target_include_directories(mouse_mapping_tests PRIVATE src)
target_link_libraries(mouse_mapping_tests PRIVATE raylib)

add_executable(performance_report_tests tests/performance_report.c)
target_link_libraries(performance_report_tests PRIVATE performance_report)

enable_testing()
add_test(NAME compute_shader_smoke COMMAND compute_shader_smoke)
add_test(NAME fluid_sim_gpu COMMAND fluid_sim_gpu_tests)
add_test(NAME mouse_mapping COMMAND mouse_mapping_tests)
add_test(NAME performance_report COMMAND performance_report_tests)
set_tests_properties(compute_shader_smoke fluid_sim_gpu mouse_mapping performance_report PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
set_tests_properties(performance_report PROPERTIES LABELS "cpu")
