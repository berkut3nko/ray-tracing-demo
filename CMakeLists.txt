cmake_minimum_required(VERSION 3.28)

# --- 1. Enabling modules ---
set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API "2182bf5c-ef0d-489a-91da-49dbc3090d2a")
set(CMAKE_EXPERIMENTAL_CXX_MODULE_DYNDEP ON)

# --- 2. Scaning for clang features ---
find_program(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS 
    NAMES clang-scan-deps-18 clang-scan-deps
    DOC "Path to clang-scan-deps"
)

if(NOT CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS)
    message(FATAL_ERROR "Could NOT find clang-scan-deps-18. Install clang-tools-18.")
else()
    message(STATUS "Using C++ Module Scanner: ${CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS}")
endif()

# --- 3. Project ---
project(RayTracingDemo
    VERSION 0.0.1
    LANGUAGES CXX C
)

# --- 4. Standart claryfication C++ ---
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_link_options("-fuse-ld=lld")

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wno-missing-field-initializers
        -march=native
    )
endif()

# Dependencies
include(cmake/Dependencies.cmake)

# --- 5. Target ---
add_executable(${PROJECT_NAME}
    src/main.cpp
)

# Setting up Modules 
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)

target_sources(${PROJECT_NAME} PUBLIC
    src/LoadModel.cpp
    src/SurfaceAreaHeuristic.cpp
)


target_sources(${PROJECT_NAME} PUBLIC
    FILE_SET CXX_MODULES FILES 
    src/Engine.cppm
    src/Types.cppm
    src/ShaderController.cppm
    src/Window.cppm
    src/UI.cppm
)

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_SCAN_FOR_MODULES ON)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    glfw
    imgui_lib
    tinygltf_lib
)

# Create dir for shaders
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders
)

# Copy compiled shader
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/src/shaders/raytrace.comp.spv
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders/raytrace.comp.spv
    COMMENT "Copying shaders to build directory"
)

enable_testing()
add_subdirectory(tests)