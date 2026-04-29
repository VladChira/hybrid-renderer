cmake_minimum_required(VERSION 3.27)

project(HybridRenderer LANGUAGES C CXX)

# --------------------
# Options
# --------------------
option(HYBRID_BUILD_EDITOR "Build the editor application" ON)
option(HYBRID_BUILD_TESTS  "Build tests" ON)
option(HYBRID_IMGUI_DEMO   "Compile imgui_demo.cpp into ImGui library" OFF)
option(HYBRID_ENABLE_TRACY "Enable Tracy profiling instrumentation" ON)
# OFF during Phase 0 of the Vulkan migration — shaders still need explicit
# Vulkan-style `layout(set, binding)` qualifiers on samplers and the stub
# renderer doesn't load them anyway. Flip ON in Phase 1 as passes get ported.
option(HYBRID_BUILD_SPIRV  "Compile GLSL shaders to SPIR-V at build time" OFF)

# Render-hardware-interface backend. opengl is the legacy default while the
# vulkan path is being brought up. See VULKAN_PLAN.md.
set(HYBRID_RHI "opengl" CACHE STRING "Render backend: opengl | vulkan")
set_property(CACHE HYBRID_RHI PROPERTY STRINGS opengl vulkan)
if(NOT HYBRID_RHI MATCHES "^(opengl|vulkan)$")
  message(FATAL_ERROR "HYBRID_RHI must be one of: opengl, vulkan (got '${HYBRID_RHI}')")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")


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
find_package(glm CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# Vulkan dependencies. We deliberately do NOT pull `vulkan-loader` from
# vcpkg — older vcpkg loaders have a relative-path resolution bug for ICD
# manifests on macOS that breaks MoltenVK. Letting `find_package(Vulkan)`
# walk to the LunarG SDK (via $VULKAN_SDK) gets us a recent loader that
# resolves the path correctly. See VULKAN_PLAN.md decision log.
if(HYBRID_RHI STREQUAL "vulkan")
  find_package(Vulkan REQUIRED)
  find_package(VulkanMemoryAllocator CONFIG QUIET)
  if(NOT VulkanMemoryAllocator_FOUND)
    find_package(unofficial-vulkan-memory-allocator CONFIG QUIET)
  endif()
  find_package(glslang CONFIG QUIET)

  # glslangValidator is invoked from CMake to produce SPIR-V at build time.
  find_program(GLSLANG_VALIDATOR glslangValidator
               HINTS $ENV{VULKAN_SDK}/bin $ENV{VULKAN_SDK}/Bin)
  if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR
      "HYBRID_RHI=vulkan requires glslangValidator on PATH or in $VULKAN_SDK/bin. "
      "Install the Vulkan SDK (https://vulkan.lunarg.com/) or vcpkg's glslang.")
  endif()
  message(STATUS "Using glslangValidator: ${GLSLANG_VALIDATOR}")
endif()
if(HYBRID_ENABLE_TRACY)
  find_package(Tracy CONFIG QUIET)
  if(NOT Tracy_FOUND)
    find_package(unofficial-tracy CONFIG QUIET)
  endif()
endif()

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
hybrid_require_target(_HYBRID_SPDLOG_TGT    spdlog::spdlog)

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

add_library(thirdparty_spdlog INTERFACE)
add_library(thirdparty::spdlog ALIAS thirdparty_spdlog)
target_link_libraries(thirdparty_spdlog INTERFACE "${_HYBRID_SPDLOG_TGT}")

# Vulkan + VMA wrappers (always created so dual-backend code compiles).
add_library(thirdparty_vulkan INTERFACE)
add_library(thirdparty::vulkan ALIAS thirdparty_vulkan)
if(HYBRID_RHI STREQUAL "vulkan")
  if(TARGET Vulkan::Vulkan)
    target_link_libraries(thirdparty_vulkan INTERFACE Vulkan::Vulkan)
  elseif(TARGET Vulkan::Headers)
    target_link_libraries(thirdparty_vulkan INTERFACE Vulkan::Headers)
  endif()
  if(TARGET GPUOpen::VulkanMemoryAllocator)
    target_link_libraries(thirdparty_vulkan INTERFACE GPUOpen::VulkanMemoryAllocator)
  elseif(TARGET unofficial::VulkanMemoryAllocator)
    target_link_libraries(thirdparty_vulkan INTERFACE unofficial::VulkanMemoryAllocator)
  endif()
endif()

# --------------------
# Helper: compile GLSL shaders to SPIR-V at build time.
# Each shader becomes a target dependency so editing it triggers a rebuild.
# --------------------
function(hybrid_compile_shaders out_target)
  set(options)
  set(one_value SHADER_ROOT OUTPUT_DIR)
  set(multi_value FILES)
  cmake_parse_arguments(SHC "${options}" "${one_value}" "${multi_value}" ${ARGN})

  if(NOT GLSLANG_VALIDATOR)
    message(WARNING
      "hybrid_compile_shaders called without glslangValidator; skipping SPIR-V build for ${out_target}")
    add_custom_target(${out_target})
    return()
  endif()

  set(_outputs)
  foreach(src IN LISTS SHC_FILES)
    file(RELATIVE_PATH rel "${SHC_SHADER_ROOT}" "${src}")
    set(out "${SHC_OUTPUT_DIR}/${rel}.spv")
    get_filename_component(out_dir "${out}" DIRECTORY)
    file(MAKE_DIRECTORY "${out_dir}")
    add_custom_command(
      OUTPUT  "${out}"
      COMMAND "${GLSLANG_VALIDATOR}" -V
              -I"${SHC_SHADER_ROOT}/include"
              -o "${out}"
              "${src}"
      DEPENDS "${src}"
      WORKING_DIRECTORY "${SHC_SHADER_ROOT}"
      COMMENT "glslang: ${rel} -> ${rel}.spv"
      VERBATIM)
    list(APPEND _outputs "${out}")
  endforeach()

  add_custom_target(${out_target} ALL DEPENDS ${_outputs})
endfunction()

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
    thirdparty::glfw
    external::imgui_glfw_opengl3
    external::implot
    thirdparty::spdlog
    external::imguizmo
    external::imguifiledialog
    $<$<PLATFORM_ID:Windows>:Psapi>
)

# Add engine sources here:
target_sources(hybrid_engine PRIVATE
  src/engine/Engine.cpp

  src/core/App.cpp
  src/core/Log.cpp
  src/core/PerformanceTelemetry.cpp
  src/core/ResourceMonitor.cpp
  src/core/UiCommandProcessor.cpp
  src/core/scene/SceneLoadService.cpp
  src/core/scene/SceneWorld.cpp
  src/core/scene/SceneCameraResolver.cpp

  src/platform/Platform.cpp
  src/platform/FileSystem.cpp
  src/graphics/GraphicsRuntime.cpp

  src/ui/Ui.cpp
  src/ui/panels/ContentBrowserPanel.cpp
  src/ui/panels/ConsolePanel.cpp
  src/ui/panels/MaterialsPanel.cpp
  src/ui/panels/PropertiesPanel.cpp
  src/ui/panels/RenderTargetsPanel.cpp
  src/ui/panels/PerformancePanel.cpp
  src/ui/panels/SettingsPanel.cpp
  src/ui/panels/ToolbarPanel.cpp
  src/ui/panels/Panel.cpp
  src/ui/panels/SceneHierarchyPanel.cpp
  src/ui/panels/ViewportPanel.cpp
  src/ui/panels/components/NameComponentDrawer.cpp
  src/ui/panels/components/TransformComponentDrawer.cpp
  src/ui/panels/components/MaterialComponentDrawer.cpp
  src/ui/panels/components/MeshRendererComponentDrawer.cpp
  src/ui/panels/components/CameraComponentDrawer.cpp
  src/ui/panels/components/CameraTargetComponentDrawer.cpp
  src/ui/panels/components/LightCommonComponentDrawer.cpp
  src/ui/panels/components/PointLightComponentDrawer.cpp
  src/ui/panels/components/DirectionalLightComponentDrawer.cpp
  src/ui/panels/components/AreaLightComponentDrawer.cpp
  src/ui/panels/components/HdriLightComponentDrawer.cpp
  src/ui/Dockspace.cpp
  src/ui/UiStateUtils.cpp

  src/assets/AssetManager.cpp
  src/assets/DiskAssetDataSource.cpp
  src/assets/AssimpSceneLoader.cpp
  src/assets/AssimpTextureCache.cpp
  src/assets/StbImageLoader.cpp
  src/assets/AssetUtils.cpp

  src/renderer/stores/GeometryStore.cpp
  src/renderer/stores/LightStore.cpp
  src/renderer/stores/MaterialStore.cpp
  src/renderer/SceneWorldSnapshot.cpp

  src/renderer/raytracing/BvhBuilder.cpp
  src/renderer/raytracing/MiddleSplitStrategy.cpp
  src/renderer/raytracing/SahSplitStrategy.cpp
  src/renderer/raytracing/AccelerationStructureCache.cpp

  src/ui/panels/AccelerationStructurePanel.cpp

  src/utils/PathUtils.cpp
  src/utils/Banner.cpp
)

# Backend-specific sources. Both backends still live in the tree; only the
# selected one is compiled in. Renderer.cpp + the passes are GL-flavoured
# today (Phase 0 of the Vulkan migration); they live under the opengl branch
# until ported. The vulkan build provides a stub renderer.
if(HYBRID_RHI STREQUAL "opengl")
  target_sources(hybrid_engine PRIVATE
    src/renderer/Renderer.cpp
    src/renderer/FrameResources.cpp
    src/renderer/ShaderManager.cpp
    src/renderer/OpenGLRenderBackend.cpp
    src/renderer/opengl/GLBuffer.cpp
    src/renderer/opengl/GLTexture.cpp
    src/renderer/opengl/GLFramebuffer.cpp
    src/renderer/opengl/GLShaderProgram.cpp
    src/renderer/opengl/GLVertexArray.cpp
    src/renderer/passes/DeferredLightingPass.cpp
    src/renderer/passes/GBufferPass.cpp
    src/renderer/passes/HdriPrecomputePass.cpp
    src/renderer/passes/AreaLightVisualizationPass.cpp
    src/renderer/passes/RenderTargetChannelsPass.cpp
    src/renderer/passes/TraversalHeatmapPass.cpp
    src/renderer/passes/RayTracedShadowPass.cpp
    src/renderer/passes/SpatioTemporalDenoisePass.cpp
  )
  target_compile_definitions(hybrid_engine PUBLIC HYBRID_RHI_OPENGL=1)
elseif(HYBRID_RHI STREQUAL "vulkan")
  target_sources(hybrid_engine PRIVATE
    src/renderer/RendererVulkanStub.cpp
    src/renderer/vulkan/VulkanCommon.cpp
    src/renderer/vulkan/VulkanRenderBackend.cpp
    src/renderer/vulkan/VulkanInstance.cpp
    src/renderer/vulkan/VulkanDevice.cpp
    src/renderer/vulkan/VulkanSwapchain.cpp
  )
  target_compile_definitions(hybrid_engine PUBLIC HYBRID_RHI_VULKAN=1)
  target_link_libraries(hybrid_engine PRIVATE thirdparty::vulkan)
endif()

# Compile shaders to SPIR-V at build time when targeting Vulkan and the
# shaders have been ported to Vulkan-style explicit layouts.
if(HYBRID_RHI STREQUAL "vulkan" AND HYBRID_BUILD_SPIRV)
  file(GLOB_RECURSE _HYBRID_SHADER_SRC CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert"
       "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag"
       "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.comp")
  hybrid_compile_shaders(hybrid_shaders
    SHADER_ROOT  "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    OUTPUT_DIR   "${CMAKE_BINARY_DIR}/shaders"
    FILES        ${_HYBRID_SHADER_SRC}
  )
  add_dependencies(hybrid_engine hybrid_shaders)
  target_compile_definitions(hybrid_engine PRIVATE
    HYBRID_SHADER_BIN_DIR="${CMAKE_BINARY_DIR}/shaders")
endif()

target_include_directories(hybrid_engine
  BEFORE
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/external/imgui"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_definitions(hybrid_engine PRIVATE
  HYBRID_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
  $<$<BOOL:${HYBRID_ENABLE_TRACY}>:HYBRID_ENABLE_TRACY=1>
)

if(HYBRID_ENABLE_TRACY)
  hybrid_require_target(_HYBRID_TRACY_TGT
    Tracy::TracyClient
    Tracy::TracyClientStatic
    tracy::TracyClient
    unofficial::tracy::tracy-client
    unofficial::tracy::tracy
  )
  target_link_libraries(hybrid_engine PRIVATE "${_HYBRID_TRACY_TGT}")
endif()


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
      external::implot
      thirdparty::glfw
  )
  if(HYBRID_RHI STREQUAL "opengl")
    target_link_libraries(hybrid_editor PRIVATE
      external::glad
      external::imgui_glfw_opengl3
      thirdparty::opengl)
  else()
    target_link_libraries(hybrid_editor PRIVATE
      thirdparty::vulkan
      external::imgui_glfw_vulkan)
  endif()
endif()

if(HYBRID_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
