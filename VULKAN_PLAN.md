# Vulkan Migration Plan

This document is the working plan for porting the hybrid-renderer from OpenGL 4.6
to Vulkan 1.3 + MoltenVK. It is not a finished design — it is the plan we are
discovering by doing. Update it as decisions get made or invalidated.

## 1. Goals and non-goals

**Goals**
- Run on macOS (Apple Silicon and Intel via MoltenVK), Linux, and Windows
  natively — one C++ codebase, one shader source tree.
- Preserve the existing renderer architecture: pass-oriented design, the
  `Renderer::Render` orchestration, frame resource layout. The rewrite is
  *under* the existing pass interfaces, not above them.
- Compile shaders once at build time to SPIR-V; ship SPIR-V with the binary.

**Non-goals (V1)**
- Performance parity with native Metal. MoltenVK gets ~85–95%; that's fine.
- Multiple frames in flight beyond 2. Keeps sync simple.
- A rendergraph / automatic barrier insertion. Hand-coded barriers are fine
  for ~10 passes.
- Hot-reloading shaders. Build-time SPIR-V only for V1; runtime glslang can
  come back later.
- Headless / server modes.
- Replacing GLFW. GLFW's Vulkan integration is solid.

## 2. Strategy: parallel backend, switched at compile time

Two backends live side-by-side until the Vulkan path is feature-complete:

```
src/renderer/
  rhi/            <-- new: backend-agnostic resource interfaces (header-only)
  opengl/         <-- existing GL impl
  vulkan/         <-- new: Vulkan impl
  passes/         <-- pass code targets rhi/, not gl directly
```

Build-time selection via `HYBRID_RHI=opengl|vulkan` CMake option. Default is
`opengl` until Vulkan reaches parity, then we flip the default and the OpenGL
path becomes the legacy fallback.

### Why a parallel backend, not a hard cutover

- The codebase has working art tests on GL. Losing that bisecting capability
  during the migration would be expensive.
- A pass-by-pass cutover (one pass on Vulkan, the rest on GL) is impossible —
  GL and Vulkan can't share a swapchain or interop their textures cheaply.
  The unit of switching is the *whole renderer*, but the code organization
  can still be incremental.
- The RHI abstraction is forced to be honest: anything one backend can do but
  the other can't is a real portability cost we surface immediately rather
  than discovering at the end.

### What "the RHI" actually is

A header-only set of opaque handles + a small `Device` interface. It is *not*
trying to be bgfx or sokol_gfx. It exists to:

- Type-erase `VkBuffer` / `GLuint` etc behind `rhi::BufferHandle`.
- Group operations onto a `rhi::CommandList` interface.
- Move resource creation/destruction onto a `rhi::Device` interface.

It is allowed to leak Vulkan-shaped concepts (explicit barriers, descriptor
sets, image layouts) up through the interface. We are not trying to pretend
GL-style implicit sync is real. The OpenGL backend will simulate Vulkan
semantics where needed (e.g., barrier calls become `glMemoryBarrier`).

## 3. Concrete inventory of what we're porting

From `find /src` and `wc -l`:

- **Wrapper classes** (~700 LoC): `GLBuffer`, `GLFramebuffer`, `GLShaderProgram`,
  `GLTexture`, `GLVertexArray`.
- **Passes** (~1,800 LoC): `GBufferPass`, `DeferredLightingPass`,
  `HdriPrecomputePass` (the biggest at 640 lines — does cubemap convolution +
  prefilter + BRDF LUT), `AreaLightVisualizationPass`, `RenderTargetChannelsPass`,
  `RayTracedShadowPass`, `SpatioTemporalDenoisePass`, `TraversalHeatmapPass`.
- **Renderer plumbing** (~1,300 LoC): `Renderer.cpp`, `FrameResources.cpp`,
  `OpenGLRenderBackend.cpp`, `ShaderManager.cpp`.
- **Stores**: `GeometryStore`, `MaterialStore`, `LightStore`,
  `AccelerationStructureCache`. These mostly upload SSBOs — straightforward.
- **Shaders**: 14 raster (.vert/.frag) + 4 compute (.comp) + 4 includes.
- **External**: glad (GL loader), GLFW, ImGui (with glfw+opengl3 backend).

Grand total: ~3,800 LoC of GL-touching C++ + 18 shaders.

## 4. Architecture decisions

### 4.1 Shader pipeline

Build-time only:
- Source: existing GLSL, with one tweak — explicit `set` and `binding`
  layout qualifiers (`layout(set=0, binding=2) ...`).
- Compiler: `glslangValidator` invoked from CMake via `add_custom_command`.
  Each `.vert/.frag/.comp` produces a `.spv` next to it under
  `${CMAKE_BINARY_DIR}/shaders/`.
- Distribution: SPIR-V blobs are loaded at runtime from a known directory
  (same scheme as today's `ShaderManager` loading GLSL).
- Includes: glslang supports `#include` natively with `--include-dir`. Our
  `shaders/include/` already works under it.

Why not `shaderc` or runtime compilation? Build-time SPIR-V is faster to
load, removes glslang from the runtime, and the project doesn't need on-the-fly
shader edits today.

### 4.2 Bindless textures

The GL path uses `GL_ARB_bindless_texture` for materials. The Vulkan path
uses `VK_EXT_descriptor_indexing` (or its 1.2 core version) with a single
large descriptor set holding a `[]` array of combined image samplers,
indexed by a per-draw push constant.

This is well-supported on MoltenVK as of the last few years (with
`VK_KHR_descriptor_update_template` and `descriptor indexing` features). We'll
gate on those features at device selection time.

Sampler strategy: a small fixed pool of sampler objects (linear/nearest x
clamp/wrap/mirror), referenced by index. Avoids one-sampler-per-texture cost.

### 4.3 Synchronization model

We keep it simple:
- **One graphics queue, one transfer queue if separate.** No async compute in
  V1.
- **Two frames in flight.** Each frame has its own command pool and per-frame
  resources (uniforms, descriptor sets that change per draw).
- **Explicit barriers per pass.** Each pass declares "I read X, I write Y";
  the renderer inserts the right `vkCmdPipelineBarrier` between passes.
- **Image layouts** tracked on the texture handle (mutable in the RHI).

### 4.4 Resource lifetime

- A `DeferredDeleteQueue` keyed on frame index. Resources released this frame
  get destroyed once we know `frames_in_flight + 1` frames have elapsed.
- VMA (`VulkanMemoryAllocator`) for buffer/image allocation. Adopted via
  vcpkg.

### 4.5 Swapchain and present

- Triple-buffered VK_PRESENT_MODE_MAILBOX_KHR if available, else FIFO.
- Swapchain images are color-only. The render pipeline targets offscreen
  attachments (matches today's GBuffer / scene framebuffers); a final
  blit/copy lands the result on the swapchain image.

### 4.6 Window integration

GLFW with `GLFW_NO_API` window hints. Surface creation via
`glfwCreateWindowSurface`. No more `MakeContextCurrent`.

### 4.7 ImGui

Switch from `imgui_impl_opengl3` to `imgui_impl_vulkan`. Same `imgui_impl_glfw`
host integration. ImGui owns its own descriptor pool. This is mechanical.

## 5. Migration sequence

The passes graph defines the order. Anything upstream of a pass must be ready
before that pass migrates. Estimates assume someone fluent in Vulkan; double
them otherwise.

### Phase 0 — infrastructure ✅ DONE
- [x] vcpkg deps: `vulkan-memory-allocator`, `glslang`, `spirv-tools`.
      Notably **not** `vulkan-loader` or `vulkan-headers` — see decision log.
- [x] CMake: `HYBRID_RHI` option (opengl|vulkan, default opengl), `mac-ninja`
      and `mac-ninja-vulkan` presets. `find_package(Vulkan)` resolves to the
      LunarG SDK via `$VULKAN_SDK`. `hybrid_compile_shaders()` produces SPIR-V
      from a *whitelist* of source files (not a glob — the existing GL
      shaders need explicit `set/binding` qualifiers before they can join).
- [x] `src/renderer/vulkan/` core: `VulkanInstance` (validation +
      portability for MoltenVK), `VulkanDevice` (physical-device pick by
      score, descriptor-indexing probe, graphics+present queues),
      `VulkanSwapchain` (mailbox-or-FIFO, SRGB target, recreate-on-resize),
      `VulkanRenderBackend` orchestrator (now also owns VMA + offscreen
      target + per-frame command pool/sync).
- [x] `src/renderer/rhi/` headers: `RhiTypes.h` and `Device.h`. Still
      header-only — no implementation yet. Will get fleshed out in Phase 2
      as the first pass needs them.
- [x] `src/platform/Platform.cpp` gated on `HYBRID_RHI_VULKAN` —
      `GLFW_NO_API` window hint when on Vulkan.
- [x] ImGui: `external::imgui_glfw_vulkan` target wired up (only built when
      `HYBRID_RHI=vulkan`). Not actually used yet; Phase 7 work.

### Phase 0.5 — verify the build ✅ DONE
- [x] Vulkan SDK installed (1.4.341.1, with MoltenVK).
- [x] vcpkg installed and configured (`$VCPKG_ROOT`).
- [x] `cmake --preset mac-ninja` builds (and fails to *run* as expected:
      Mac caps OpenGL at 4.1, GLFW can't honor the 4.6 request).
- [x] `cmake --preset mac-ninja-vulkan` builds, links, runs. The stub
      Renderer logs its Phase-0 warning and we open a blank GLFW window.

### Phase 1A — clear-screen via vkCmdClearColorImage ✅ DONE
- [x] `Renderer::Init(NativeWindowHandle)` — window plumbed in, OpenGL
      ignores the param. App.cpp passes `platform.GetNativeHandle()`.
- [x] App.cpp gates the UI module behind `HYBRID_RHI_OPENGL` so the editor
      reaches its main loop on Vulkan. UI bringup is Phase 7.
- [x] `VulkanRenderBackend::BeginFrame/EndFrame` with two frames in flight
      (per-frame command pool, command buffer, image-acquire/render-finished
      semaphores, in-flight fence). Auto-recreates swapchain on
      `VK_SUBOPTIMAL_KHR` / `VK_ERROR_OUT_OF_DATE_KHR`.
- [x] Stub Renderer's EndFrame: layout-transition swapchain image →
      `vkCmdClearColorImage` → layout-transition to PRESENT_SRC.
- [x] Validated on Mac/MoltenVK: teal swapchain, resizable.

### Phase 1B — compute foundation ✅ DONE
- [x] `VulkanAllocator.cpp` defines `VMA_IMPLEMENTATION` (single TU). VMA
      uses dynamic-vulkan-functions mode so it resolves entry points via
      `vkGet*ProcAddr` rather than at link time.
- [x] `VulkanRenderBackend` owns `VmaAllocator` and an offscreen RGBA8
      storage image (size = swapchain extent, recreated on resize).
- [x] `VulkanShader.{h,cpp}`: SPIR-V file loader + `VkShaderModule` wrapper.
- [x] `shaders/compute/swapchain_clear.comp` — first real Vulkan shader,
      writes a time-varying gradient to a `set=0, binding=0` storage image
      with size + time in push constants. SPIR-V whitelist enabled so this
      one shader compiles to `.spv` at build time.
- [x] Compute pipeline state in the stub Renderer: descriptor set layout (1
      storage image), pipeline layout (set + 16-byte push constants),
      `VkPipeline`, descriptor pool, two per-frame descriptor sets (one per
      frame in flight, written when the offscreen image gets recreated).
- [x] EndFrame: offscreen `UNDEFINED→GENERAL`, dispatch compute,
      `GENERAL→TRANSFER_SRC` and swapchain `UNDEFINED→TRANSFER_DST`,
      `vkCmdBlitImage` offscreen→swapchain, `TRANSFER_DST→PRESENT_SRC`.
- [x] Validated on Mac/MoltenVK: gradient renders, animates with time,
      survives resize.

**This is the foundation every remaining compute-pass port reuses:**
SPIR-V load → descriptor set layout → pipeline layout → pipeline →
descriptor pool → per-frame sets → barriers + dispatch.

### Phase 2 — first real pass: TraversalHeatmapPass (next session entry point)
Smallest existing compute pass. Inputs: gbuffer depth, BVH SSBOs (TLAS +
BLAS nodes, triangle indices, primitives, instances). Output: a storage
image (the heatmap target). Tests:

- Adding the existing shader to the SPIR-V whitelist *after* porting its
  `uniform sampler2D` declarations to explicit `layout(set, binding)`.
- SSBO descriptors (`VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`).
- Pulling `GeometryStore` / `AccelerationStructureCache` SSBOs across the
  GL→Vulkan boundary. The buffer data formats (`std430`) are already
  Vulkan-compatible.
- The first time we have to actually flesh out `rhi::Device` for Vulkan —
  the inline-everything-in-the-stub-Renderer pattern won't survive contact
  with a real pass that needs SSBO uploads + lifetime management.

**Recommended attack order**:
1. Migrate the *shader*: add `layout(set, binding)` to every uniform sampler
   in `traversal_heatmap.comp`. Add it to the SPIR-V whitelist. Verify
   `.spv` builds.
2. Implement *just enough* of `rhi::Device` for buffer creation +
   descriptor writes. Don't try to retrofit GLBuffer → it stays GL-only.
3. Stand up a `TraversalHeatmapVulkanPass` (or rename and dual-implement
   the existing class) that creates SSBOs from the GeometryStore data and
   runs the dispatch.
4. Pipe the result back into the same display path the gradient uses now
   (blit to swapchain).

**Estimate**: 2–3 days of focused work.

### Phase 2 — first pass: TraversalHeatmapPass
Smallest compute pass. One input (BVH SSBO + gbuffer depth), one storage image
output. Tests: shader compile pipeline, descriptor set layout, compute
dispatch, image storage write, barrier between compute write and downstream
sample.

**Estimate**: 2 days.

### Phase 3 — GBufferPass
First raster pass. Tests: vertex/index buffer binding, uniforms via
descriptor sets, MRT framebuffers (attachment 0..2 + depth), bindless
descriptor indexing for materials. This is the pass that will demand a real
GeometryStore/MaterialStore migration.

**Estimate**: 1 week. **Risk**: the bindless materials work is the largest
single piece of the entire port.

### Phase 4 — DeferredLightingPass + ShadowMaskUpscale-equivalent (RT shadows)
Single fullscreen quad reading the gbuffer. The shadow path follows because
deferred lighting consumes the shadow mask.

- `RayTracedShadowPass`: compute pass with TLAS/BLAS SSBOs. Same shape as
  TraversalHeatmapPass, just bigger.
- `SpatioTemporalDenoisePass`: 2 compute passes (temporal + a-trous), reads
  current and prev gbuffer copies.
- `glCopyImageSubData` of prev gbuffer → `vkCmdCopyImage`.

**Estimate**: 1 week.

### Phase 5 — HdriPrecomputePass
Cubemap rendering with 6 layered render targets. Uses geometry shader-style
multi-view in GL (or one draw per face). On Vulkan, multi-view rendering via
`VK_KHR_multiview` or just 6 separate render passes.

**Estimate**: 4–5 days.

### Phase 6 — remaining passes
`AreaLightVisualizationPass`, `RenderTargetChannelsPass`. Both small.

**Estimate**: 2–3 days.

### Phase 7 — ImGui Vulkan backend + final swap
Replace `imgui_impl_opengl3` with `imgui_impl_vulkan`. Flip the default
`HYBRID_RHI` to `vulkan`. Burn-in.

**Estimate**: 2 days.

### Phase 8 — cleanup
Either: keep both backends and gate at runtime (worth it if Linux/Windows
users want GL for older hardware), or delete `src/renderer/opengl/` and
`glad`. Probably delete — Vulkan 1.0 runs on almost everything OpenGL 4.6
runs on.

**Estimate**: 1 day.

**Total**: ~5–6 weeks of focused work. Probably 2–3× that with interruptions.

## 6. Risk register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| MoltenVK lacks `descriptor_indexing` features we want | Low (it has them now) | High | Validate at Phase 0 with a tiny test program |
| Bindless materials are harder than expected | Medium | High | Phase 3 is the make-or-break point. Spend extra time prototyping if needed |
| ImGui Vulkan integration breaks the editor | Low | Medium | Phase 7 has a known-working Dear ImGui example; mostly cookbook |
| Tracy's Vulkan zone API differs from GL | Low | Low | Tracy supports Vulkan zones natively; just a different macro |
| BVH SSBOs change semantics | Low | Low | Same `std430` layout works on both |
| Existing shaders break going through glslang strict mode | Medium | Low | Catch at Phase 0; fix shader-by-shader |
| The RHI abstraction is wrong | Certain | Medium | Plan to refactor it after Phase 4. Don't over-design upfront |

## 7. Decision log

This section captures decisions we make as we go, with the *why* — so future
work doesn't re-litigate them.

- **2026-04-29**: Plan started. Branch is `vulkan`.
- **2026-04-29**: Don't pull `vulkan-loader` (or `vulkan-headers`) from vcpkg.
  vcpkg's vulkan-loader 1.4.328 has a relative-path resolution bug for ICD
  manifests on macOS — when MoltenVK_icd.json's `library_path` is
  `../../../lib/libMoltenVK.dylib`, this loader resolves it against the
  binary's CWD rather than against the manifest file. The dlopen silently
  fails, `vkCreateInstance` succeeds with no real driver, and `vkCreate
  MetalSurfaceEXT` is unresolvable → `VK_ERROR_EXTENSION_NOT_PRESENT`. The
  LunarG SDK loader (1.4.341+) resolves correctly. CMake now uses
  `find_package(Vulkan)` which picks up `$VULKAN_SDK`. Lost ~1 hour to this.
- **Pick Vulkan over WebGPU/bgfx**: Vulkan is the closest semantic match to
  what we're already doing; WebGPU is younger and bgfx wraps things at a
  higher level than we'd want for this project. MoltenVK gives us Mac
  support. Already covered in the chat thread that produced this plan.
- **Build-time SPIR-V**: keeps glslang out of the shipping binary, faster
  startup. We pay rebuild-on-shader-edit, which we already pay today since
  the GL path also reads from disk.
- **No rendergraph in V1**: 8 passes. Hand-coded ordering and barriers fit
  in someone's head. We can introduce a rendergraph if/when we double the
  pass count.
- **2026-04-29**: Whitelist of SPIR-V-compiled shaders (not glob). Existing
  shaders use bare `uniform sampler2D` and rely on `glUniform1i` for unit
  assignment — that's not legal in Vulkan/SPIR-V. Each shader joins the
  whitelist when its samplers are migrated to explicit `layout(set, binding)`.
  Stops the SPIR-V build from failing on shaders we haven't ported yet
  while still letting us validate the toolchain on the ones we have.
- **2026-04-29**: Compute-via-blit, not compute-into-swapchain. Apple's
  surface formats often don't expose `STORAGE` usage (and even when they
  do, the SRGB encoding semantics get awkward). Compute writes into an
  offscreen RGBA8 image with `STORAGE | TRANSFER_SRC`, and a final
  `vkCmdBlitImage` copies into the swapchain. Same pattern the production
  renderer will use anyway (offscreen scene-color → blit to present).
- **2026-04-29**: Per-frame descriptor sets, written on resize. Two
  descriptor sets (one per frame in flight), each pre-allocated from a
  small pool. They get re-pointed at the offscreen image whenever the
  offscreen extent changes — which is once at startup and again on resize,
  both of which already wait-idle. Avoids the complexity of
  `UPDATE_AFTER_BIND` while still being safe under in-flight commands.
- **2026-04-29**: VMA in dynamic-function-loading mode
  (`VMA_DYNAMIC_VULKAN_FUNCTIONS=1`). Resolves entry points via
  `vkGet*ProcAddr` at allocator-create time. Avoids link-time coupling to
  a specific loader version (which bit us once already, and might again).
- **2026-04-29**: Defer the abstract `rhi::Device` impl past Phase 2.
  Phase 1 landing notes called for implementing it as Phase 2's first
  step, but the risk register lists "RHI abstraction is wrong" as
  *certain*, and the heatmap pass is the only consumer right now. Wrote
  the pass with direct Vulkan calls instead (mirrors the gradient stub),
  on the bet that 2-3 ported passes will give us enough evidence to shape
  `rhi::Device` correctly the first time. Revisit at Phase 4.
- **2026-04-29**: Keep ported shaders dual-target via a `SET_BINDING(s,b)`
  macro shim in `shaders/include/common.glsl` plus an `#ifdef VULKAN` UBO
  vs loose-uniform split. The GL driver (Mac caps GL at 4.1, but other
  platforms still run GL) doesn't recognize `set=`; glslang auto-defines
  `VULKAN` under `-V`. The shim keeps the same `.comp` source compiling
  for both backends until the GL path is retired. Required because
  ported passes still need to work on the GL side until each pass's GL
  cpp moves out of the build.
- **2026-04-29**: Phase 7 brought up before Phase 3. Plan estimates Phase 3
  (GBuffer / first raster pass) at ~1 week and Phase 7 (ImGui Vulkan) at 2
  days. Doing the cheap one first restores the editor UI which makes every
  subsequent phase nicer to develop against (panels, scene browser, AS
  stats panel reading real BVH numbers). Doesn't add risk to Phase 3.
- **2026-04-29**: ImGui-Vulkan via dynamic rendering, not legacy render
  pass. `VK_KHR_dynamic_rendering` is core in Vulkan 1.3 and supported by
  MoltenVK. Avoids per-swapchain-image framebuffer setup + resize
  bookkeeping. Cost: must enable `VkPhysicalDeviceVulkan13Features::
  dynamicRendering` on device init. Future raster passes will use this
  too — Phase 3's GBuffer pipeline can target it directly.
- **2026-04-29**: UI hook pattern instead of restructuring `Renderer`'s
  Begin/End API. Renderer exposes `SetUiRenderHook(std::function<void(
  VkCommandBuffer)>)`; EndFrame opens a `vkCmdBeginRendering` scope on
  the swapchain image (post-blit, layout = COLOR_ATTACHMENT_OPTIMAL,
  loadOp=LOAD) and invokes the hook before transitioning to PRESENT_SRC.
  One command buffer, one submit, no extra sync. Trade-off: hook-style
  coupling instead of an API split, but the API split would have rippled
  through every call site. Easy to revisit if more hooks accumulate.
- **2026-04-29**: Frame ordering differs by backend. GL builds ImGui state
  AND submits draws inside `Ui::Frame` (after `EndFrame` so it has the
  frame's textures). Vulkan splits this — `Ui::Frame` builds draw lists
  *before* `EndFrame`, the renderer's hook records them later. App.cpp
  has an `#ifdef` to swap the order. This means the Vulkan path's
  `ui_state` doesn't see this-frame's renderer outputs (they aren't
  available yet) — fine while the per-frame texture handles are all 0
  in stage-1; will need attention when ViewportPanel goes to sample the
  offscreen image (stage-2).

## 8. Open questions

These are things I noticed while writing the plan and don't have answers to.
Flag any of them that block progress.

- **Should the OpenGL path stay shipping-quality forever, or get retired
  after the Vulkan path is stable?** Affects how careful I am with the RHI
  abstraction. My default: retire GL after Phase 7, keep the code in a
  branch for a release cycle.
- **Apple Silicon CI**: do we have any? If not, the macOS port is going to
  be tested manually. Worth setting up something cheap (GitHub Actions has
  Apple Silicon runners on paid plans).
- **What MoltenVK version do we target?** I plan to use the one shipped with
  the Vulkan SDK (1.3.x). Pinning a specific version is worth it once we
  have a CI catching Mac regressions.
- **Material textures: do we need mipmaps?** The current GLTexture wrapper
  has mipmap support. Vulkan needs explicit mipmap generation via blit
  chain. Adds ~50 LoC.

## 9. Working notes

This is where we write things down as the migration progresses, so the next
session has context. Append; don't overwrite.

### Phase 7-stage-1 landing notes (2026-04-29) — ImGui editor running on Vulkan

What runs now: the editor UI is back. Dockspace, scene hierarchy, properties,
content browser, console, settings, render-targets, performance, and the
acceleration-structure panel all render via ImGui's Vulkan backend through
dynamic rendering. The AS panel surfaces the real BVH stats from the loaded
scene (helmet glTF), validating the Phase 2B data flow end-to-end through
the Ui side.

**Architecture**: the Renderer exposes a single hook
(`SetUiRenderHook(std::function<void(VkCommandBuffer)>)`). Inside
`EndFrame`, after the offscreen→swapchain blit, the renderer transitions
the swapchain image `TRANSFER_DST → COLOR_ATTACHMENT_OPTIMAL`, opens a
`vkCmdBeginRendering` scope (loadOp=LOAD so the heatmap blit is
preserved), invokes the hook, closes the scope, then transitions to
PRESENT_SRC. The hook is implemented as `Ui::RenderImGuiInto(cmd)` which
just calls `ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd)`.
One command buffer, one submit, no extra sync.

**The frame-ordering split** (see decision-log entry above) is the only
non-trivial structural difference: Vulkan calls `ui.Frame` *before*
`renderer.EndFrame` so ImGui draw lists exist when the hook fires. GL
keeps the prior order. App.cpp has a small `#ifdef` to handle both.

**Per-pass plumbing template** (see `Ui::InitVulkan` for the worked
example):
1. `IMGUI_CHECKVERSION` + `ImGui::CreateContext` + `ImPlot::CreateContext`
   + font + theme — backend-agnostic, common with the GL path.
2. `ImGui_ImplGlfw_InitForVulkan(window, true)`.
3. `vkCreateDescriptorPool` for ImGui's font + user textures (1000
   entries, `FREE_DESCRIPTOR_SET` flag — the standard ImGui example pool).
4. Fill `ImGui_ImplVulkan_InitInfo`. Critical fields: `ApiVersion =
   VK_API_VERSION_1_3`, `UseDynamicRendering = true`, and
   `PipelineInfoMain.PipelineRenderingCreateInfo` with the swapchain
   color format. ImGui_ImplVulkan_Init creates the pipeline internally
   when those are set.
5. Skipped: explicit `ImGui_ImplVulkan_CreateFontsTexture` — modern
   ImGui versions upload fonts lazily on first RenderDrawData call.

**Build-system gotcha worth keeping in mind**: ToolbarPanel's ctor calls
`stbi_load` + `glGenTextures` for its icons. With glad uninitialized in
Vulkan mode, the gl* function pointers are null and the call segfaults
on construction. Stage-1 fix: `#ifdef HYBRID_RHI_OPENGL` around the
ToolbarPanel registration in `Ui::RegisterDefaultPanels`. Stage-2 will
register icon textures via `ImGui_ImplVulkan_AddTexture`.

**Sidequest**: `core::ResourceMonitor::QueryProcessRamMB` had a
hardcoded fall-through-to-zero on macOS (no procfs). Added a Mach
`task_info(... MACH_TASK_BASIC_INFO ...)` branch so the PerformancePanel
shows real RAM usage on Mac. RSS-equivalent across all three platforms now.

**Validation layers off in RelWithDebInfo**. We learned this the hard way
chasing the ToolbarPanel segfault: HYBRID_DEBUG (and therefore
`enable_validation`) only fires in Debug builds. RelWithDebInfo runs the
release Vulkan path with no validation. For runtime debugging, either
switch to Debug (`-DCMAKE_BUILD_TYPE=Debug`) or set
`VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` in the env. Worth
considering whether we want validation in RelWithDebInfo too — the perf
hit is small for development.

**Open items for Phase 7-stage-2**:
- ViewportPanel sampling the offscreen image. Needs `VK_IMAGE_USAGE_
  SAMPLED_BIT` on the offscreen, a sampler, an `ImGui_ImplVulkan_
  AddTexture` registration that gets refreshed on resize, and an
  `ImageLayout = SHADER_READ_ONLY_OPTIMAL` transition after the heatmap
  dispatch (replacing the current TRANSFER_SRC blit path). Once that's
  in, the offscreen→swapchain blit can be dropped entirely — ImGui
  clears the swapchain itself and the panel composes the rendered
  image inside the dockspace.
- ToolbarPanel icons via `ImGui_ImplVulkan_AddTexture`. Each icon would
  be a small VkImage uploaded once at startup and registered as an
  ImTextureID.
- ImGui_ImplVulkan_SetMinImageCount on swapchain recreation. Currently
  we don't call this; if the swapchain image count ever changes (e.g.
  after a present mode switch) we'd be inconsistent with ImGui's
  internal state. Low risk in practice but worth fixing.
- Tracy GPU zones — still GL-only. ImGui's render time is now part of
  the renderer's command buffer; would be useful to wrap it in a Tracy
  zone alongside the heatmap dispatch and blit.

### Phase 2B landing notes (2026-04-29) — heatmap driven by real scene BVH

What runs now: editor loads a glTF (e.g. `scenes/helmet/DamagedHelmet.gltf`)
via the existing asset path, App.cpp resolves the scene's primary camera,
calls `renderer.SubmitScene(scene_world, view, settings)`. The Vulkan
Renderer drives `SceneFrameCache` → walks mesh instances and calls
`GeometryStore::GetOrAppend` per primitive → `AccelerationStructureCache::
SyncBlas` / `SyncTlas` → mirrors the CPU vectors (`Primitives()`,
`BlasNodes()`, `TlasNodes()`, `TlasInstances()`) into the heatmap pass's
host-visible SSBOs. The pass dispatches with the real camera. The
heatmap viz now silhouettes the actual scene mesh.

**The data-flow shape we landed on**:
- `GeometryStore` and `AccelerationStructureCache` keep their existing
  CPU-side vectors. The Vulkan path just *never calls* their GL methods
  (`Init` / `Sync` / `Upload` / `Bind*`). New CPU-side accessors
  (`BlasNodes` / `BlasTriangles` / `TlasNodes` / `TlasInstances`) were
  added to `AccelerationStructureCache` to expose what was already
  there. No refactor of the stores; the Vulkan path is a parallel
  reader.
- The pass owns its host-visible SSBOs and re-uploads on every dirty
  frame. Reallocation (when sizes grow) is gated behind a
  `Device::WaitIdle` in the Renderer; the dirty-detection currently
  compares last-uploaded byte counts. This stalls the pipeline once on
  scene load and again whenever BVH topology changes — fine for the
  editor's mostly-static workload, will need revisiting under dynamic
  scenes.

**The descriptor-write split** in the pass is now three helpers, each
touching only the bindings it owns:
- `WriteImageDescriptors(view)` → binding 0, called on Init + on resize
- `WriteUboDescriptors()` → binding 1, called once on Init
- `WriteSsboDescriptors()` → bindings 2/7/9/10, called when any SSBO is
  reallocated by `UpdateSsbos`

This reads cleaner than Phase 2A's all-in-one `SetOutputImageView` and
gives us a clear pattern for the next pass: split descriptor writes by
who controls the lifetime of the underlying resource.

**The GL-wrapper link footgun**: `GeometryStore` and
`AccelerationStructureCache` hold `GLBuffer` / `GLVertexArray` members
directly. Even though the Vulkan path never calls their GL methods,
the C++ destructors and ctors reference the GL wrapper symbols, and
those don't link unless their .cpp files are compiled. Workaround:
add `src/renderer/opengl/GLBuffer.cpp` + `GLVertexArray.cpp` to the
Vulkan target sources. They're dead at runtime — glad's function
pointers exist (linked transitively via `imgui_glfw_opengl3`), the
methods just never get called. Cost: ~200 LoC of dead code in the
Vulkan binary.

The clean fix is to refactor the stores to split CPU side from GL side
(or template-parameterize the GPU buffer type). Tracked as cleanup, not
urgent.

**Open items carried into Phase 3**:
- Stores still have `GLBuffer`/`GLVertexArray` members in Vulkan mode.
  When we get to Phase 3 (GBuffer / first raster pass), the stores will
  also need to upload to Vulkan vertex/index buffers — that's the right
  forcing function for the CPU/GPU split refactor.
- `Device::WaitIdle` on every BVH dirty-frame is a placeholder. Two
  cleaner options: (a) per-frame-in-flight SSBO copies (memory cost,
  no stalls), (b) gate descriptor rewrites + memcpy on the per-frame
  fence (no extra memory, no stalls, but more bookkeeping). Pick when
  perf actually hurts — currently the editor's BVH only changes on
  scene load.
- Heatmap is the only consumer of the BVH SSBOs right now. When
  `RayTracedShadowPass` lands (Phase 4), the SSBOs should move out of
  the heatmap pass into a shared place — either `rhi::Device` or a
  `VulkanGeometryStore`/`VulkanAccelerationStructureCache` mirror.

### Phase 2A landing notes (2026-04-29) — heatmap running on MoltenVK with synthetic BVH

Phase 2 split into 2A (Vulkan-side bring-up of the pass, validated against
hand-fabricated input) and 2B (real `SceneWorld → SubmitScene → BVH SSBOs`
plumbing). 2A is done; 2B is next. What runs now: orbiting camera around a
unit AABB at the origin; `TraversalHeatmapVulkanPass` writes the false-
coloured visit count into the offscreen image; backend blits to swapchain.
With the synthetic 1-instance / 1-BLAS-leaf input, the visit counts are 1
(miss) vs 3 (hit), both of which land in the cool half of the LUT — so the
visible result is two-tone blue, which is the correct prediction.

**Shader-migration pattern** (template for the rest of the ported shaders).
Worked example: `traversal_heatmap.comp` + `heatmap_bvh_traversal.glsl`.
Ported shaders need to compile under both backends until the GL path is
retired, so:

1. Add `set=N` qualifiers via the `SET_BINDING(s, b)` macro from
   `shaders/include/common.glsl`. It expands to `set = s, binding = b`
   under Vulkan and `binding = b` under GL (glslang auto-defines `VULKAN`
   when invoked with `-V`).
2. Loose `uniform` declarations are illegal in Vulkan — pack them into a
   UBO. Keep both shapes:

   ```glsl
   #ifdef VULKAN
   layout(std140, SET_BINDING(0, 1)) uniform Params {
       mat4 a;
       mat4 b;
       vec4 c;        // use vec4 not vec3 — std140 padding
       /* ... */
   };
   #else
   uniform mat4 a;
   uniform mat4 b;
   uniform vec3 c;
   #endif
   ```

   Members are accessed as bare names in both cases. The Vulkan-side `vec4`
   is read with `c.xyz` so the same shader body works for both shapes.

3. Add `#extension GL_GOOGLE_include_directive : require` under the
   `VULKAN` guard if the shader uses `#include` — glslang refuses to
   process `#include` natively without it. The GL ShaderManager inlines
   includes itself, so this extension line is Vulkan-only.

4. Add the shader file to `_HYBRID_VULKAN_SHADERS` in CMakeLists.txt to
   join the SPIR-V whitelist.

**Per-pass plumbing template** (see `TraversalHeatmapVulkanPass.cpp`):

1. CreateDescriptorSetLayout — one binding per shader binding, sparse
   indices are fine (e.g., 0/1/2/7/9/10 for the heatmap pass).
2. CreatePipelineLayout (no push constants for this one — params live in
   a UBO).
3. LoadSpirv + CreateShaderModule + CreateComputePipelines.
4. CreateDescriptorPool sized for `kMaxFramesInFlight` sets, with pool
   sizes summed across all bindings (e.g., 1×storage_image + 1×UBO +
   4×SSBO per set, times frames-in-flight).
5. AllocateDescriptorSets — one set per frame in flight.
6. Allocate per-frame UBOs (host-visible, persistently mapped) so the CPU
   can refresh per-dispatch params in `Execute` without sync.
7. SSBOs that are static for the lifetime of the pass: also host-visible
   for now (Phase 2A has tiny synthetic data; production will want
   device-local + staging upload).
8. `SetOutputImageView(view)` writes all per-frame descriptor sets in a
   single `vkUpdateDescriptorSets` call. Called on Init and on resize
   (resize already waits idle).

**Per-frame execution template** (see `Execute` + `EndFrame`):

1. memcpy params into this-frame's mapped UBO.
2. transition output image UNDEFINED → GENERAL.
3. bind pipeline + bind descriptor set (this frame's), dispatch.
4. transition output GENERAL → TRANSFER_SRC, swapchain UNDEFINED →
   TRANSFER_DST, blit, then TRANSFER_DST → PRESENT_SRC.

The Renderer stub still owns the inbound/outbound image barriers around
the dispatch — the pass doesn't transition layouts itself. This keeps
the pass's responsibilities to "I write GENERAL when called". Whether
that boundary is right will get re-evaluated when chaining passes shows
up (Phase 4 has a denoise that consumes the shadow output).

**Verification idiom: synthetic BVH for shape validation.** A
hand-fabricated 1-instance / 1-BLAS-leaf BVH with deterministic visit
counts is a small, predictable input that exercises the full descriptor
chain (UBO + 4 SSBOs + storage image) without needing scene-data
plumbing. The same pattern should work for the next compute passes:
build the smallest input that still exercises every shader binding, ship
it before driving from real scene data. The synthetic structs are local
to `RendererVulkanStub.cpp` (search `BuildSyntheticBvh`) and can be
deleted once 2B lands.

**Open items carried into Phase 2B**:
- The offscreen image is still backend-owned. The pass's descriptor
  write reaches into the backend for `OffscreenImageView()` directly.
  When `rhi::Device` lands (post-Phase 4 per the explicit decision-log
  entry), this becomes a `RegisterExternalTexture` call or similar.
- BVH SSBOs are host-visible. Fine for the synthetic 4×144-byte input;
  not fine for a real scene with thousands of nodes. Phase 2B should
  decide whether to upgrade to device-local + staging upload, or wait
  until perf actually matters.
- `VkPipelineCache` is still not used. Two passes is still small enough
  that "fresh compile per pipeline" doesn't sting; revisit at Phase 4.
- Tracy GPU zones are still GL-only. `HYBRID_PROFILE_GPU_*` abstraction
  is still a TODO; do it when GPU profiling is needed for actual perf
  work, not before.

**Build-system gotchas worth keeping in mind**:
- glslang's `-I"path"` under CMake `VERBATIM` mode passes the literal
  quote characters through to the tool. Use `"-I${path}"` (quotes around
  the whole arg) instead. Cost ~10 minutes.
- The include search base for ported shaders is `shaders/`, not
  `shaders/include/` — matches the GL ShaderManager. CMake's `-I` arg
  needs to point at `shaders/` so `#include "include/foo.glsl"`
  resolves.

### Phase 1 landing notes (2026-04-29) — gradient running on MoltenVK
Concrete patterns established this session, to reuse as we port real passes:

**Per-pass plumbing template** (see `RendererVulkanStub.cpp` for the worked
example, search for `CreateClearPipeline`):

```
1. LoadSpirv("compute/<shader>.spv") + CreateShaderModule
2. vkCreateDescriptorSetLayout (bindings on set=0)
3. vkCreatePipelineLayout (set + push constants)
4. vkCreateComputePipelines
5. vkCreateDescriptorPool sized for the frames-in-flight count
6. vkAllocateDescriptorSets (one set per frame in flight)
7. vkUpdateDescriptorSets (called whenever the bound resources change —
   typically only on swapchain resize)
```

**Per-frame execution template** (see `EndFrame` in the same file):
```
1. transition writeable images to GENERAL (or COLOR_ATTACHMENT later)
2. bind pipeline + bind descriptor set (this frame's) + push constants
3. vkCmdDispatch
4. transition outputs to whatever the next consumer needs
5. (eventually) chain to the next pass's barrier+dispatch
6. last pass: blit to swapchain image, transition to PRESENT_SRC
```

**Things that haven't been tested yet but we'll need soon**:
- Sampling a texture (combined image sampler) — only storage images so far.
- Reading SSBOs from compute — straight extension of the descriptor model.
- Per-frame uniform buffer updates — VMA's `HOST_VISIBLE`/persistently-mapped
  pattern.
- Pipeline cache — currently `vkCreateComputePipelines` is called fresh
  each run. Fine for one shader, will want a `VkPipelineCache` once we
  have many.
- `VkDescriptorPool` resets / freeing — current pool is a fixed size; for
  per-pass-per-frame sets we'll likely want a pool-per-frame that gets
  `vkResetDescriptorPool`'d at the top of each frame.

**The `rhi/` interface is still header-only.** The stub Renderer talks to
`VulkanRenderBackend` directly. That was right for Phase 1 (one shader,
one pass — abstraction would have been premature). It stops being right
when the second pass arrives. **Phase 2 should implement
`rhi::Device::CreateBuffer/CreateTexture/CreateComputePipeline` etc. for
Vulkan as it ports the heatmap pass.**

**Tracy GPU zones are still GL-only.** The `HYBRID_PROFILE_GL_*` macros
aren't in the Vulkan path. Add `HYBRID_PROFILE_GPU_*` (Tracy `TracyVkZone`
on Vulkan, existing macros on GL) when GPU profiling becomes important —
probably while debugging the first real pass.

### Phase 0 landing notes (2026-04-29)
- The Renderer.h public API is small (Init/Shutdown/Resize/BeginFrame/
  SubmitScene/EndFrame/GetStats/GetAccelerationStructureStats) — the stub
  matches it. When porting for real, the natural place to hold the
  `VulkanRenderBackend` is `Renderer::Impl`.
- The actual GL Renderer pulls the window from the current GL context
  (implicit). The Vulkan path needs an explicit window pointer. Two options
  for Phase 1:
  - Add a `Renderer::Init(NativeWindowHandle window)` overload (least
    invasive; OpenGL ignores the param).
  - Accept the window through a separate setter before Init.
  Pick one in Phase 1 based on what reads cleanest.
- Renderer.cpp/FrameResources.cpp/passes/* are now *only* compiled in
  `HYBRID_RHI=opengl`. We do *not* need to make them compile under Vulkan;
  we replace them entirely with rhi-flavoured equivalents over the course
  of the migration. Each file that gets ported moves out of the
  `if(HYBRID_RHI STREQUAL "opengl")` block in CMakeLists.txt and into the
  unconditional set, with a parallel `*Vulkan.cpp` file no longer needed.
- Tracy: existing `HYBRID_PROFILE_GL_*` macros are GL-specific. Tracy has
  Vulkan zone macros (`TracyVkZone`); plan a thin `HYBRID_PROFILE_GPU_*`
  abstraction in Phase 1.
- The `RhiTypes.h` `Format` enum is intentionally tiny — only formats this
  project actually uses. Add new entries as passes get ported.

### Initial survey (2026-04-29)
- The codebase already separates GL wrappers cleanly into `src/renderer/opengl/`.
  This is a gift — moving the RHI boundary is straightforward.
- Direct GL calls *outside* the wrappers occur in:
  - `Renderer.cpp` (line 562, 578: `glClearTexImage`; line 771, 776:
    `glCopyImageSubData`; line 147: `glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS)`)
  - `FrameResources.cpp` (some direct binding)
  - The pass `.cpp` files (they call `glActiveTexture`, `glBindTexture`,
    `glDispatchCompute` directly even though they have `GLShaderProgram`
    wrappers)
- Bindless usage is in `MaterialStore` — Renderer.cpp:142 hard-fails on init
  if `GL_ARB_bindless_texture` is missing. This is the most invasive part
  of the migration.
- ImGui uses the glfw+opengl3 backends already in `external/`. Will need
  the vulkan backend added.
- Compute shaders are vanilla `#version 460 core` with `layout(local_size_x...)`
  and `layout(binding=N) uniform image2D...`. They translate to SPIR-V
  cleanly; the only edits needed are `set=N` qualifiers (currently
  binding-only).
