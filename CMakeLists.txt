cmake_minimum_required(VERSION 3.15)
project(viewer)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(external)

add_executable(viewer src/main.cpp)
target_link_libraries(viewer PRIVATE glm glfw glad stb tinygltf imgui)
target_compile_definitions(viewer PRIVATE $<$<CONFIG:Debug>:_DEBUG=1>)

target_compile_options(viewer PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/std:c++latest>)