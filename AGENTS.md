# AGENTS.md

## Project overview

This project is a renderer-first real-time hybrid rendering engine.

Its long-term goal is to combine rasterization with selective ray tracing to approach offline quality on simple scenes while keeping interactive frame rates on modest hardware.

Today, the codebase already has the scaffolding for this direction (scene loading, renderer runtime, debug UI, module boundaries), but the currently implemented render path is primarily OpenGL forward rasterization.

## Target renderer capabilities (roadmap)

The first complete hybrid version is expected to support:

- loading at least minimal glTF 2.0 scenes from disk
- an in-memory scene representation usable by both raster and ray-tracing paths
- a deferred geometry pass producing a G-buffer
- a debug-friendly UI for inspecting scene data and render passes
- physically based shading baseline
- direct lighting in rasterization
- low-ray-count ray-traced soft shadows
- ray-traced reflections
- pass composition and post-processing
- qualitative comparison against offline references

Diffuse GI is important but expensive and uncertain. Do not block the rest of the renderer on GI-first development.

## Runtime flow (current code)

`src/main.cpp` -> `engine::Run()` -> `core::App::Run()`.

`core::App` owns module startup/shutdown order and the frame loop:

1. initialize `platform` (GLFW window + OpenGL context)
2. initialize `renderer`
3. initialize `ui`
4. configure `assets` and queue default scene load (`scenes/sponza/Sponza.gltf`)
5. per frame:
   - poll platform events
   - consume async scene-load result
   - update scene transforms
   - render frame (`BeginFrame` -> `SubmitScene` -> `EndFrame`)
   - render UI panels
   - swap buffers

## `src/` module map

### `src/engine`

- Thin application entry layer.
- `Engine.cpp` just instantiates `core::App` and runs it.
- Keep this layer minimal; orchestration logic belongs in `core`.

### `src/core`

- High-level app orchestration and shared engine services.
- `App.*`: module lifecycle, frame loop, scene load requests, command/event processing.
- `Log.*`: shared logger (console + file + in-memory ring buffer for UI console panel).
- `ResourceMonitor.*`: background RAM sampling for performance graphs.

#### `src/core/scene`

- ECS world model and scene-loading coordination.
- `SceneWorld.*`: `entt` registry wrapper, hierarchy parenting, dirty propagation, world-transform updates.
- `SceneLoadService.*`: worker-thread service that asynchronously asks `AssetManager` to load `SceneWorld` assets.
- `SceneTypes.h`: umbrella include for scene math/assets/components.

#### `src/core/scene/types`

- Scene data model used across assets/renderer/ui.
- `SceneMath.h`: `Transform`, `Aabb`.
- `SceneAssets.h`: `MeshAsset`, `MaterialAsset`, `Vertex`, texture sampler/color-space metadata.
- `SceneComponents.h`: ECS components (`Name`, `Transform`, `Hierarchy`, `MeshRenderer`, `Camera`, `CameraTarget`).

### `src/assets`

- Asset system with type-aware loading and runtime handles.
- `AssetManager.*`:
  - registry of asset records (`AssetId` -> typed `shared_ptr<void>`)
  - loader registration (`IAssetLoader`) and data source selection (`IAssetDataSource`)
  - typed access via `AssetHandle<T>`
- `DiskAssetDataSource.*`: filesystem-backed byte loading rooted at `assets/`.
- `StbImageLoader.*`: image decode (LDR + HDR) into `ImageAsset`.
- `AssimpSceneLoader.*`: glTF/glb loader building `SceneWorld`, materials, meshes, cameras; uses async worker fan-out for mesh/material conversion.
- `AssimpTextureCache.*`: scene-relative texture path resolution + deduplicated texture handle cache during Assimp import.
- `ImageAsset.h`: CPU-side decoded image container.

### `src/platform`

- Windowing/input/platform abstraction over GLFW.
- `Platform.*`:
  - creates window + OpenGL 3.3 core context
  - stores events and input snapshot each frame
  - exposes native window handle to other modules
- `PlatformEvents.h`: event and input structs (`PlatformEvent`, `InputState`, `NativeWindowHandle`).
- `FileSystem.*`: basic `ReadAllBytes` helper.

### `src/graphics`

- Graphics backend runtime gate.
- `GraphicsRuntime.*`:
  - one-time OpenGL function loading via GLAD
  - tracks active backend (`None`, `OpenGL`)
  - shared by renderer and UI init to avoid duplicate unsafe init paths

### `src/renderer`

- Rendering subsystem and renderer-facing scene extraction.
- `Renderer.*`:
  - current implementation: OpenGL forward pass to offscreen framebuffer
  - per-frame render target allocation
  - primitive GPU upload cache (`mesh_id + primitive_index`)
  - simple render modes (`Lit`, `Unlit`, `Wireframe` via shader mode + polygon mode)
- `RendererTypes.h`: frame context, render extent/view/settings, outputs, renderer stats.
- `SceneWorldSnapshot.*`: converts ECS scene world into renderer snapshot (`RenderMeshInstance` list with world transforms and transformed bounds).
- `ShaderManager.*`: loads shader text from `shaders/` and compiles/link programs.
- `RendererUtils.h`: small helper conversions (e.g., `int` size -> `RenderExtent`).

#### `src/renderer/opengl`

- Small RAII wrappers around GL objects:
  - `GLBuffer`
  - `GLVertexArray`
  - `GLTexture`
  - `GLFramebuffer`
  - `GLShaderProgram`
- These wrappers are the preferred place for low-level GL lifetime/state safety changes.

### `src/ui`

- ImGui-based editor/debug UI.
- `Ui.*`:
  - ImGui/ImPlot setup and backend integration
  - dockspace construction
  - panel registration and draw dispatch
  - returns command buffer (`UiCommand`) to app
- `Dockspace.*`: persistent docking layout builder.
- `UiState.h`: data pushed from app/renderer into panels (scene pointer + viewport texture).
- `UiCommands.h`: currently only `Quit` command.

#### `src/ui/panels`

- Panel framework and built-in panels.
- `Panel.*`: base class + panel registry.
- `SceneHierarchyPanel`: scene tree from ECS hierarchy, selection source.
- `PropertiesPanel`: selected-entity component inspectors.
- `ViewportPanel`: displays renderer output texture (aspect-preserving fit).
- `ConsolePanel`: in-memory log viewer with severity colors.
- `PerformancePanel`: RAM plot from `ResourceMonitor` samples.
- `PlaceholderPanel`: empty stubs for future panels.

#### `src/ui/panels/components`

- Read-only component drawers used by `PropertiesPanel`.
- Drawers exist for `Name`, `Transform`, `MeshRenderer`, `Camera`, and `CameraTarget`.

#### `src/ui/themes`

- Theme selection and color palettes.
- `Themes.h`: theme enum, palette builder, theme application entry.
- `EmbraceTheDarkness.h` / `EmbraceTheLightness.h`: raw ImGui style/color definitions.

### `src/utils`

- Lightweight non-domain helpers.
- `PathUtils.*`: extension extraction.
- `Banner.*`: startup banner text loading from `banner.txt`.

## Dependency direction (intended)

Keep dependencies moving from low-level toward orchestration:

- `platform`, `graphics`, `utils` are foundational utilities
- `assets` + `core::scene` define scene/data
- `renderer` and `ui` consume scene/data
- `core::App` orchestrates everything

Avoid introducing dependencies from low-level modules back into `core::App` or UI panels.

## Where to implement what

- Scene ingestion / glTF mapping issues: `src/assets/AssimpSceneLoader.cpp`
- ECS transform/hierarchy behavior: `src/core/scene/SceneWorld.cpp`
- Render path changes (passes, shading, outputs): `src/renderer/Renderer.cpp` (+ `RendererTypes.h` as needed)
- GL resource lifetime/state bugs: `src/renderer/opengl/*`
- Editor workflows and debug visualization: `src/ui/*`
- App lifecycle and cross-module wiring: `src/core/App.cpp`

## Current state vs hybrid target

- Current renderer implements deferred shading partially, missing IBL, area lights.
- No ray traced effects are in place (shadows would be the first)
