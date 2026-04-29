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

### Phase 0 — infrastructure (in progress, partial)
- [x] vcpkg deps: `vulkan-headers`, `vulkan-loader`, `vulkan-memory-allocator`,
      `glslang`, `spirv-tools`.
- [x] CMake: `HYBRID_RHI` option (opengl|vulkan, default opengl). Detects
      `glslangValidator`. Wires `hybrid_compile_shaders()` to produce SPIR-V
      under `${CMAKE_BINARY_DIR}/shaders/`.
- [x] `src/renderer/vulkan/` core (real, working code, not stubs):
      `VulkanInstance` (validation layer + portability for MoltenVK),
      `VulkanDevice` (physical-device pick by score, descriptor-indexing
      probe, graphics+present queues), `VulkanSwapchain` (mailbox-or-FIFO,
      SRGB target, recreate-on-resize), `VulkanRenderBackend` orchestrator.
- [x] `src/renderer/rhi/` headers: `RhiTypes.h` (opaque handles, formats,
      usages, image layouts, descriptor model) and `Device.h` (`Device` and
      `CommandList` interfaces). Header-only — no implementation yet.
- [x] `src/platform/Platform.cpp` window-creation gated on
      `HYBRID_RHI_VULKAN`: requests `GLFW_NO_API` rather than a 4.6 context.
- [x] ImGui: `external::imgui_glfw_vulkan` static target wired up
      (`imgui_impl_glfw.cpp` + `imgui_impl_vulkan.cpp`); only built when
      `HYBRID_RHI=vulkan`.
- [x] Stub `RendererVulkanStub.cpp` satisfies `Renderer.h`'s API in vulkan
      mode so the editor links. It does no rendering — calls `LOG_WARN` once
      at Init.

**Status**: `HYBRID_RHI=opengl` should still build and run unchanged.
`HYBRID_RHI=vulkan` should compile and link, produce a hybrid_editor binary,
but the binary will only spin a GLFW window without graphics output. Phase 1
hooks the stub renderer up to the actual Vulkan device + presents the
swapchain.

**Estimate remaining for full Phase 0**: 0.5–1 day to verify the build on a
machine with the Vulkan SDK installed and to fix whatever doesn't compile.

### Phase 0.5 — verify the build (next session entry point)
This session was code-only; nothing has been compiled. The first thing to do
next is:
1. Install the Vulkan SDK (https://vulkan.lunarg.com/) on the dev machine.
   On macOS this also pulls MoltenVK.
2. `git submodule update --init --recursive` (the imgui submodule needs to
   exist for the imgui Vulkan backend to compile).
3. Configure with `cmake -DHYBRID_RHI=opengl …` first — verify the existing
   path didn't break.
4. Then `cmake -DHYBRID_RHI=vulkan …`. Expect a few CMake-finds to need
   tweaking. Likely culprits:
   - `find_package(VulkanMemoryAllocator)` target name varies by vcpkg
     version. Check the actual exported name and adjust the `if(TARGET …)`
     fallback chain in the root CMakeLists.
   - `find_package(glslang)` may require pulling specific components.
5. Once the vulkan build links, run it. Confirm:
   - The validation layer loads (look for "[vulkan] instance created
     (validation=true, …)" in logs).
   - The swapchain creates ("[vulkan] swapchain WxH N images, present=…").
   - The window comes up empty (the stub Renderer logs the "Phase 0" warning
     and doesn't draw).

### Phase 1 — RHI interface and clear-screen
- Define `rhi::Device`, `rhi::CommandList`, `rhi::BufferHandle`,
  `rhi::TextureHandle`, `rhi::PipelineHandle`.
- Stand up enough Vulkan to clear the swapchain and present. Verify on a Mac
  with `vkconfig` / Xcode capture.
- Implement the OpenGL backend of the same RHI. Keep current `gl*` calls
  inside it; expose the RHI surface to the renderer.

**Estimate**: 4–5 days. **Risk**: the RHI shape will be wrong on first try.
Expect to iterate when the second pass migrates.

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
