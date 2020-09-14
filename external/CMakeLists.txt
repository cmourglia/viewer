add_library(stb STATIC stb.cpp)
target_include_directories(stb PUBLIC stb)

add_library(tinygltf STATIC tinygltf.cpp)
target_include_directories(tinygltf PUBLIC tinygltf)

add_library(glad STATIC glad/src/glad.c)
target_include_directories(glad PUBLIC glad/include)

add_subdirectory(glm)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(glfw)

add_library(imgui STATIC
    imgui/imgui.cpp imgui/imgui_demo.cpp imgui/imgui_draw.cpp imgui/imgui_widgets.cpp
    imgui/examples/imgui_impl_glfw.cpp imgui/examples/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC imgui imgui/examples)
target_link_libraries(imgui PRIVATE glad glfw)
target_compile_definitions(imgui PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLAD)