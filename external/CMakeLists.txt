add_library(stb STATIC stb.cpp)
target_include_directories(stb PUBLIC stb)

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
    imgui/imgui.cpp imgui/imgui_demo.cpp imgui/imgui_draw.cpp imgui/imgui_widgets.cpp imgui/imgui_tables.cpp
    imgui/backends/imgui_impl_glfw.cpp imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC imgui imgui/backends)
target_link_libraries(imgui PRIVATE glad glfw)
target_compile_definitions(imgui PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLAD)

# ASSIMP
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)

set(DISABLED_ASSIMP_BUILDS
        AMF 3DS AC ASE ASSBIN B3D BVH DXF CSM HMP IRRMESH
        IRR LWO LWS M3D MD2 MD3 MD5 MDC MDL NFF NDO OFF
        OGRE OPENGEX MS3D COB BLEND IFC XGL Q3D Q3BSP RAW
        SIB SMD TERRAGEN 3D X X3D 3MF MMD PLY STL)

foreach(BUILD ${DISABLED_ASSIMP_BUILDS})
    set(ASSIMP_BUILD_${BUILD}_IMPORTER OFF CACHE BOOL "" FORCE)
endforeach()

if(MSVC)
    add_compile_options(/Wv:18)
endif()
add_subdirectory(assimp)
