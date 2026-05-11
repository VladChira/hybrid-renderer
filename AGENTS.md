# AGENTS.md

## Project overview

This project is a renderer-first real-time hybrid rendering engine.

Its goal is to combine deferred rasterization with selective ray tracing to approach offline quality on simple scenes while keeping interactive frame rates on modest hardware.

The codebase now includes a working hybrid baseline: deferred lighting + ray-traced shadow integration, alongside scene loading, renderer runtime, and debug/editor tooling.

## Target renderer capabilities (roadmap)

The first complete hybrid version is expected to support:

- loading glTF 2.0 scenes from disk
- a shared in-memory scene representation for raster and ray paths
- deferred geometry pass producing a G-buffer
- debug-friendly UI for inspecting scene data and render passes
- physically based shading baseline
- direct lighting in rasterization
- low-ray-count ray-traced soft shadows
- ray-traced reflections
- pass composition and post-processing
- qualitative comparison against offline references

Diffuse GI is important but expensive and uncertain. Do not block the rest of the renderer on GI-first development.

## Runtime flow (current code)

`src/main.cpp` -> `engine::Run()` -> `core::App::Run()`.

`core::App` owns startup/shutdown order and the frame loop:

1. initialize `platform` (GLFW window + OpenGL context)
2. initialize `renderer`
3. initialize `ui`
4. configure `assets` and queue default scene load
5. per frame:
   - poll platform events
   - consume async scene-load result
   - resolve active scene + camera
   - render frame (`BeginFrame` -> `SubmitScene` -> `EndFrame`)
   - build `UiState` from renderer outputs + scene
   - execute UI frame and process `UiCommand` mutations
   - swap buffers

## `src/` module map

### `src/engine`

- Thin app entry layer.
- `Engine.cpp` instantiates `core::App` and runs it.
- Keep orchestration in `core`.

### `src/core`

- High-level app orchestration and shared engine services.
- `App.*`: module lifecycle, frame loop, scene load requests, command/event processing.
- `UiCommandProcessor.*`: applies editor/UI mutation commands into scene + runtime state.
- `Log.*`: shared logger (console + file + in-memory buffer for UI console).
- `ResourceMonitor.*`: RAM sampling for UI performance graphs.
- `PerformanceTelemetry.*`: frame telemetry collection.

#### `src/core/scene`

- ECS world model and scene-loading coordination.
- `SceneWorld.*`: `entt` registry wrapper, hierarchy parenting, dirty propagation, world-transform updates.
- `SceneLoadService.*`: background worker that asks `AssetManager` to load `SceneWorld` assets asynchronously.
- `SceneCameraResolver.*`: resolves primary/active camera view for rendering.
- `SceneTypes.h`: umbrella include for scene math/assets/components.

#### `src/core/scene/types`

- Shared scene data model for assets/renderer/ui.
- `SceneMath.h`: `Transform`, `Aabb`.
- `SceneAssets.h`: `MeshAsset`, `MaterialAsset`, `Vertex`, texture/color-space metadata.
- `SceneComponents.h`: ECS components including cameras, mesh renderers, and light components (point/area/directional/HDRI + common shadow flags).

### `src/assets`

- Type-aware asset system with runtime handles.
- `AssetManager.*`: typed registry, loader registration, typed access via `AssetHandle<T>`.
- `DiskAssetDataSource.*`: filesystem-backed bytes rooted at `assets/`.
- `StbImageLoader.*`: LDR/HDR image decode into `ImageAsset`.
- `AssimpSceneLoader.*`: glTF/glb scene ingestion into `SceneWorld` with mesh/material/light/camera extraction.
- `AssimpTextureCache.*`: scene-relative texture resolution + deduped texture-handle cache during import.
- `ImageAsset.h`: CPU-side decoded image container.

### `src/platform`

- GLFW-based window/input abstraction.
- `Platform.*`: window + GL context setup, per-frame events/input snapshot, native handle exposure.
- `PlatformEvents.h`: event/input structs.
- `FileSystem.*`: byte-read helpers.

### `src/graphics`

- Graphics runtime gate.
- `GraphicsRuntime.*`: one-time GLAD init and active-backend tracking.

### `src/renderer`

- Rendering subsystem, scene extraction, frame orchestration.
- `Renderer.*`: top-level orchestration of passes/stores/ray-tracing cache, frame lifecycle, stats.
- `OpenGLRenderBackend.*`: backend begin/end submission to default framebuffer.
- `FrameResources.*`: owns frame textures/FBOs (scene, G-buffer, debug channels, raytrace masks/history/heatmap).
- `RendererTypes.h`: frame/view/settings/stats/output structs (includes ray-tracing + denoise settings).
- `SceneWorldSnapshot.*`: ECS -> renderer frame snapshot extraction (opaque/masked/blended buckets + light lists).
- `ShaderManager.*`: shader source loading (with includes) and graphics/compute compilation.
- `ShaderBindings.h`: canonical SSBO binding slots shared across shaders.

#### `src/renderer/passes`

- `GBufferPass`: geometry pass.
- `DeferredLightingPass`: PBR deferred shading + shadow mask consumption.
- `HdriPrecomputePass`: equirect->cubemap, irradiance, prefilter, BRDF LUT generation.
- `RayTracedShadowPass`: compute ray traversal writing per-light shadow-mask layers.
- `SpatioTemporalDenoisePass`: temporal accumulation + A-Trous filtering for ray-traced shadow masks.
- `TraversalHeatmapPass`: compute debug pass visualizing BVH traversal cost.
- `RenderTargetChannelsPass`: channel extraction textures for debug visualization.
- `AreaLightVisualizationPass`: debug/visual representation of area lights.

#### `src/renderer/stores`

- `GeometryStore`: shared GPU vertex/index/primitive-descriptor store for raster + compute/ray consumers.
- `MaterialStore`: material SSBO + bindless texture residency and fallback textures.
- `LightStore`: light SSBO packing + shadow-caster layer allocation for ray shadow passes.

#### `src/renderer/raytracing`

- CPU/GPU acceleration-structure data model and builders.
- `Bvh.h`: std430 node/instance structs for BLAS/TLAS buffers.
- `BvhBuilder.*`: BVH construction.
- `MiddleSplitStrategy.*`, `SahSplitStrategy.*`: split heuristics.
- `AccelerationStructureCache.*`: BLAS cache per primitive, TLAS rebuild/sync, GPU SSBO upload, telemetry stats.

#### `src/renderer/opengl`

- RAII wrappers for low-level GL objects:
  - `GLBuffer`
  - `GLVertexArray`
  - `GLTexture`
  - `GLFramebuffer`
  - `GLShaderProgram`

### `src/ui`

- ImGui/ImPlot-based editor/debug UI.
- `Ui.*`: setup, dockspace, panel registry/draw, command-buffer return.
- `Dockspace.*`: persistent docking layout.
- `UiState.h`: scene/texture/view/settings state fed into panels.
- `UiCommands.h`: command variant for app actions, scene loads, entity/camera/light/material edits, editor camera navigation.

#### `src/ui/panels`

- Panel framework and built-ins.
- `Panel.*`: base class + registry.
- `ToolbarPanel`: high-level editor actions.
- `SceneHierarchyPanel`: ECS hierarchy and selection.
- `PropertiesPanel`: selected-entity component inspectors/mutators.
- `MaterialsPanel`: material list + edits.
- `ContentBrowserPanel`: scene/content browsing and scene load requests.
- `ViewportPanel`: renderer output display + gizmo interaction.
- `RenderTargetsPanel`: visualization/channel target switching.
- `SettingsPanel`: renderer settings (ray-traced shadows, denoiser, heatmap, tone mapping, HDRI precompute).
- `AccelerationStructurePanel`: BLAS/TLAS telemetry.
- `ConsolePanel`: log viewer.
- `PerformancePanel`: RAM/perf graphs.

#### `src/ui/panels/components`

- Component drawers for entity/material inspectors.
- Includes drawers for transforms, mesh renderer, cameras/camera target, and light types (point/area/directional/HDRI + shared light controls).

#### `src/ui/themes`

- Theme selection and color palettes.

### `src/utils`

- Lightweight helpers (`PathUtils.*`, `Banner.*`).

## Dependency direction (intended)

Keep dependencies flowing from low-level utilities toward orchestration:

- `platform`, `graphics`, `utils` are foundational
- `assets` + `core::scene` define shared scene/data
- `renderer` and `ui` consume shared scene/data
- `core::App` orchestrates modules

Avoid introducing dependencies from low-level modules back into `core::App` or specific UI panels.

## Where to implement what

- Scene ingestion / glTF mapping: `src/assets/AssimpSceneLoader.cpp`
- ECS transform/hierarchy/camera behavior: `src/core/scene/*`
- Frame orchestration and pass wiring: `src/renderer/Renderer.cpp`
- Render passes/shading: `src/renderer/passes/*`
- GPU scene/material/light stores: `src/renderer/stores/*`
- Ray tracing acceleration structures and traversal data: `src/renderer/raytracing/*`
- GL resource lifetime/state safety: `src/renderer/opengl/*`
- Editor workflows/debug visualization: `src/ui/*`
- App lifecycle and cross-module wiring: `src/core/App.cpp`

## Current state vs hybrid target (2026-05-01)

Implemented now:

- Deferred renderer with G-buffer + deferred lighting.
- Shared GPU geometry/material/light stores usable by raster and compute/ray paths.
- Ray-traced shadow pass (compute shader) writing per-light shadow-mask layers.
- Optional experimental environment visibility layer for HDRI contribution.
- Spatio-temporal denoising pipeline for shadow masks (temporal accumulation + A-Trous).
- BLAS/TLAS build/sync cache with GPU SSBO upload and runtime telemetry.
- BVH traversal heatmap debug pass and UI visualization.
- UI controls for ray-tracing, denoise, heatmap, and HDRI precompute settings.

Not implemented yet (major roadmap items):

- Ray-traced reflections.
- GI pipeline.

## Suggested next milestones

1. Add first reflection pass with roughness-aware sampling and denoise/integration path.
2. Add ray debugging views for hit distance/material/layer diagnostics.
3. Keep GI as a follow-up phase once shadows/reflections are robust.

## Notes

You cannot run builds in your sandboxed environment. Let me do it.

Material texture replacement UI is partially scaffolded in `src/ui/panels/components/MaterialComponentDrawer.cpp`: picker buttons exist, but the actual swap path is intentionally not wired yet.

When revisiting material texture swapping later, expected touchpoints are:

- `src/ui/UiCommands.h`: add a material-texture edit command covering slot selection + new texture path.
- `src/core/UiCommandProcessor.cpp`: resolve the new image handle and update the selected `MaterialAsset` texture slot.
- `src/renderer/stores/MaterialStore.*`: add a reliable invalidation/reupload path so edited material texture bindings and GPU records refresh deterministically.
