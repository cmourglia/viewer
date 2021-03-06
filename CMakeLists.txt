cmake_minimum_required(VERSION 3.15)
project(spark)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(external)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/renderer/dfggen.cpp
    src/renderer/program.h src/renderer/program.cpp
    src/renderer/material.h src/renderer/material.cpp
    src/renderer/environment.h src/renderer/environment.cpp
    src/renderer/render_primitives.h src/renderer/render_primitives.cpp
    src/renderer/renderer.h src/renderer/renderer.cpp
    src/renderer/texture.h src/renderer/texture.cpp
    src/renderer/frame_stats.h src/renderer/frame_stats.cpp
    src/assets/asset.h src/assets/asset.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE glm glfw glad stb imgui assimp)
target_include_directories(${PROJECT_NAME} PRIVATE src)
target_compile_definitions(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:_DEBUG=1> NOMINMAX _CRT_SECURE_NO_WARNINGS)

target_compile_options(${PROJECT_NAME} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/std:c++latest>)
target_compile_options(${PROJECT_NAME} PRIVATE -W3 -Werror)