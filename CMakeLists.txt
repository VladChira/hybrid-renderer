cmake_minimum_required(VERSION 3.27)

project(HybridRenderer LANGUAGES C CXX)

# --------------------
# Options
# --------------------
option(HYBRID_BUILD_EDITOR "Build the editor application" ON)
option(HYBRID_BUILD_TESTS  "Build tests" ON)
option(HYBRID_IMGUI_DEMO   "Compile imgui_demo.cpp into ImGui library" OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Output dirs (works for single- and multi-config generators)
set(_HYBRID_OUT_BIN "${CMAKE_BINARY_DIR}/bin")
set(_HYBRID_OUT_LIB "${CMAKE_BINARY_DIR}/lib")
foreach(cfg IN ITEMS "" DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  string(TOUPPER "${cfg}" cfg_u)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY${cfg_u} "${_HYBRID_OUT_BIN}")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY${cfg_u} "${_HYBRID_OUT_LIB}")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY${cfg_u} "${_HYBRID_OUT_LIB}")
endforeach()

# --------------------
# Common compile settings
# --------------------
add_library(hybrid_options INTERFACE)
add_library(hybrid::options ALIAS hybrid_options)

target_compile_features(hybrid_options INTERFACE cxx_std_17)
target_compile_definitions(hybrid_options INTERFACE
  GLFW_INCLUDE_NONE
  $<$<CONFIG:Debug>:HYBRID_DEBUG=1>
)

# Optional warning set (kept minimal; expand later if you want)
add_library(hybrid_warnings INTERFACE)
add_library(hybrid::warnings ALIAS hybrid_warnings)

if(MSVC)
  target_compile_options(hybrid_warnings INTERFACE /W4 /permissive-)
else()
  target_compile_options(hybrid_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

# --------------------
# Dependencies (vcpkg expects these to be available via toolchain)
# --------------------
find_package(OpenGL REQUIRED)
find_package(glm 1.0.3 CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)

# Helper: resolve varying exported target names into stable wrapper targets.
function(hybrid_require_target out_var)
  foreach(tgt IN ITEMS ${ARGN})
    if(TARGET "${tgt}")
      set(${out_var} "${tgt}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  message(FATAL_ERROR "Required CMake target not found. Tried: ${ARGN}")
endfunction()

hybrid_require_target(_HYBRID_GLFW_TGT   glfw glfw3 glfw3::glfw glfw3::glfw3)
hybrid_require_target(_HYBRID_ASSIMP_TGT assimp::assimp assimp)
# glm via vcpkg is glm::glm; no need to wrap, but we can for consistency:
hybrid_require_target(_HYBRID_GLM_TGT    glm::glm)

add_library(thirdparty_glfw INTERFACE)
add_library(thirdparty::glfw ALIAS thirdparty_glfw)
target_link_libraries(thirdparty_glfw INTERFACE "${_HYBRID_GLFW_TGT}")

add_library(thirdparty_assimp INTERFACE)
add_library(thirdparty::assimp ALIAS thirdparty_assimp)
target_link_libraries(thirdparty_assimp INTERFACE "${_HYBRID_ASSIMP_TGT}")

add_library(thirdparty_glm INTERFACE)
add_library(thirdparty::glm ALIAS thirdparty_glm)
target_link_libraries(thirdparty_glm INTERFACE "${_HYBRID_GLM_TGT}")

add_library(thirdparty_opengl INTERFACE)
add_library(thirdparty::opengl ALIAS thirdparty_opengl)
target_link_libraries(thirdparty_opengl INTERFACE OpenGL::GL)

# --------------------
# In-tree externals (imgui/glad/stb/entt)
# --------------------
add_subdirectory(external)

# --------------------
# Targets (skeleton)
# Replace the source lists with your real files.
# --------------------
add_library(hybrid_engine STATIC)
add_library(hybrid::engine ALIAS hybrid_engine)

target_link_libraries(hybrid_engine
  PUBLIC
    hybrid::options
    hybrid::warnings
    thirdparty::glm
    external::entt
    external::stb
  PRIVATE
    thirdparty::assimp
)

# Add engine sources here:
target_sources(hybrid_engine PRIVATE
  src/engine/Engine.cpp
)

target_include_directories(hybrid_engine
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if(HYBRID_BUILD_EDITOR)
  add_executable(hybrid_editor)
  target_sources(hybrid_editor PRIVATE
    src/main.cpp
  )

  target_link_libraries(hybrid_editor
    PRIVATE
      hybrid::engine
      hybrid::options
      hybrid::warnings
      external::glad
      external::imgui_glfw_opengl3
      thirdparty::glfw
      thirdparty::opengl
  )
endif()

if(HYBRID_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
