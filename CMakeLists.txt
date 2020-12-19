cmake_minimum_required(VERSION 3.15)
project(viewer)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(external)

add_executable(viewer
    src/main.cpp
    src/renderer/dfggen.cpp
    src/renderer/program.h src/renderer/program.cpp
    src/renderer/material.h src/renderer/material.cpp
    src/renderer/environment.h src/renderer/environment.cpp
    src/renderer/render_primitives.h src/renderer/render_primitives.cpp)

target_link_libraries(viewer PRIVATE glm glfw glad stb imgui assimp)
target_include_directories(viewer PRIVATE src)
target_compile_definitions(viewer PRIVATE $<$<CONFIG:Debug>:_DEBUG=1>)

target_compile_options(viewer PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/std:c++latest>)