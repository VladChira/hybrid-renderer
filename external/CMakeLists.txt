# external/
#   imgui/   (docking branch)
#   glad/    (generated output committed: include/ + src/glad.c)
#   stb/     (headers you copied)
#   entt/    (submodule or snapshot)

# --------------------
# stb (header-only)
# --------------------
add_library(external_stb INTERFACE)
add_library(external::stb ALIAS external_stb)

target_include_directories(external_stb SYSTEM INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/stb"
)

# --------------------
# EnTT (header-only)
# --------------------
add_library(external_entt INTERFACE)
add_library(external::entt ALIAS external_entt)

# EnTT layout differs depending on how you vendor it.
# Common cases:
# - repo root has 'src/entt' headers
# - repo root has 'single_include/entt'
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/entt/single_include/entt/entt.hpp")
  target_include_directories(external_entt SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/entt/single_include"
  )
else()
  target_include_directories(external_entt SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/entt/src"
  )
endif()

# --------------------
# GLAD (compiled C library)
# --------------------
add_library(external_glad STATIC
  "${CMAKE_CURRENT_SOURCE_DIR}/glad/glad.c"
)
add_library(external::glad ALIAS external_glad)

target_include_directories(external_glad SYSTEM PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/glad"
)

# --------------------
# Dear ImGui (docking) - core library
# --------------------
add_library(external_imgui STATIC
  "${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_draw.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_tables.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_widgets.cpp"
)

if(HYBRID_IMGUI_DEMO)
  target_sources(external_imgui PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_demo.cpp"
  )
endif()

add_library(external::imgui ALIAS external_imgui)

target_include_directories(external_imgui SYSTEM PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/imgui"
)

target_link_libraries(external_imgui PUBLIC
  hybrid::options
)

# --------------------
# ImPlot (source) - depends on ImGui
# --------------------
add_library(external_implot STATIC
  "${CMAKE_CURRENT_SOURCE_DIR}/implot/implot.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/implot/implot_items.cpp"
)

add_library(external::implot ALIAS external_implot)

target_include_directories(external_implot SYSTEM PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/implot"
  "${CMAKE_CURRENT_SOURCE_DIR}/imgui"
)

target_link_libraries(external_implot PUBLIC
  external::imgui
)

# --------------------
# ImGui backends: GLFW + OpenGL3
# Requires wrapper targets created in root:
#   thirdparty::glfw
#   thirdparty::opengl
# --------------------
add_library(external_imgui_glfw_opengl3 STATIC
  "${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends/imgui_impl_glfw.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends/imgui_impl_opengl3.cpp"
)

add_library(external::imgui_glfw_opengl3 ALIAS external_imgui_glfw_opengl3)

target_include_directories(external_imgui_glfw_opengl3 SYSTEM PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/imgui"
  "${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends"
)

target_compile_definitions(external_imgui_glfw_opengl3 PUBLIC
  IMGUI_IMPL_OPENGL_LOADER_GLAD
)

target_link_libraries(external_imgui_glfw_opengl3
  PUBLIC
    external::imgui
    external::glad
    thirdparty::glfw
    thirdparty::opengl
)
