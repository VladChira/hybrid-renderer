# Hybrid Renderer — Implementation Plan

## Guiding principles
- **Refactor first, then build.** Every phase below Phase 0 assumes the shared-resource SSBO refactor has landed and raster still looks identical.
- **Share GPU resources between raster and RT.** One geometry SSBO, one material SSBO, one light SSBO, bindless textures. No parallel "RT-only" pipelines.
- **All BVH work happens on the CPU.** GPU only reads. Rebuilds happen on worker threads driven by the existing dirty-queue system.
- **Make every RT buffer inspectable.** Shadow masks, reflection masks, BVH node counts, traversal heatmaps — all exposed through the debug UI.
- **Land each phase visually.** The scene should look plausible (or better) at every phase boundary; never leave the renderer half-broken between phases.

---

## Phase 0 — Shared GPU resource refactor

Goal: rewire raster to consume the data layouts the ray passes will need, with **zero visible change** to rasterized output. This phase is the biggest-effort-to-smallest-visible-change phase; do not collapse it.

### 0.1 Unified geometry store
- New: [src/renderer/GeometryStore.h](src/renderer/GeometryStore.h) / `.cpp`.
- One global vertex SSBO (`GL_SHADER_STORAGE_BUFFER`, also bound as `GL_ARRAY_BUFFER` at draw time) and one global index SSBO (same, also `GL_ELEMENT_ARRAY_BUFFER`).
- Growth: geometric reallocation with copy. No freeing on unload for v1 (accept leaks; sponza loads once).
- Per-primitive descriptor table (another SSBO):
  ```cpp
  struct GpuPrimitive {
      uint32_t vertex_offset;   // in vertices
      uint32_t vertex_count;
      uint32_t index_offset;    // in indices
      uint32_t index_count;
      uint32_t material_index;
      uint32_t blas_root;       // filled in Phase 1
      uint32_t _pad0, _pad1;
  };
  ```
- `GpuSceneResourceCache` collapses into `GeometryStore` + `TextureStore`. Keying stays `(mesh_id, primitive_index) -> primitive_id`.
- Raster keeps VAOs but they reference the same GL buffer objects. Each draw binds the same VAO with `glDrawElementsBaseVertex(..., index_offset, vertex_offset)`.
- **Done when**: G-buffer pass is visually identical; renderdoc shows a single backing buffer reused across draws.

### 0.2 Material SSBO + bindless textures
- New: [src/renderer/MaterialStore.h](src/renderer/MaterialStore.h) / `.cpp`.
- One material SSBO mirroring `MaterialAsset`:
  ```cpp
  struct GpuMaterial {
      vec4 base_color_factor;
      vec4 emissive_factor;           // w = alpha_cutoff
      float metallic_factor;
      float roughness_factor;
      float normal_scale;
      float occlusion_strength;
      uvec4 alpha_mode_pad;           // x = alpha_mode
      uvec2 base_color_handle;        // bindless
      uvec2 mr_handle;
      uvec2 normal_handle;
      uvec2 occlusion_handle;
      uvec2 emissive_handle;
      uvec2 _pad;
  };
  ```
- Bindless textures via `GL_ARB_bindless_texture`:
  - Extend [src/renderer/opengl/GLTexture.h](src/renderer/opengl/GLTexture.h) with `GetBindlessHandle()` / `MakeResident()` / `MakeNonResident()`.
  - Texture residency held as long as the texture is in any material record.
- `gbuffer.frag` reads `materials[primitive.material_index]`, resolves textures via `sampler2D(handle)` constructor.
- Placeholder "missing" textures become residents once at startup and reused.
- **Risk**: bindless is an extension, not core 4.6. Add a graphics-runtime assertion at init. If unsupported: abort (user already confirmed the target hardware supports compute, which on NV/AMD drivers also exposes bindless — fine).
- **Done when**: every material-bound sampler uniform is removed from `gbuffer.frag`.

### 0.3 Light SSBO
- One SSBO per light type (directional, point, area), plus three `uniform uint` counts.
- `deferred_lighting.frag` reads from SSBOs instead of uniform arrays. Delete the fixed-size uniform arrays.
- Same SSBOs will be bound read-only in the RT shadow and reflection passes — shadow and reflection logic shares the exact same light data as deferred lighting.
- **Done when**: lighting still looks identical; MAX_* constants are gone from the shader.

### 0.4 Shader manager hygiene
- Hot reload is out of scope (nice-to-have, skip).
- Add **uniform block / SSBO binding-point registry** so bindings aren't duplicated literals across shaders. New: `src/renderer/ShaderBindings.h` with `constexpr` slot numbers:
  ```cpp
  namespace binding {
      constexpr GLuint k_geometry_vertices  = 0;
      constexpr GLuint k_geometry_indices   = 1;
      constexpr GLuint k_primitives         = 2;
      constexpr GLuint k_materials          = 3;
      constexpr GLuint k_directional_lights = 4;
      constexpr GLuint k_point_lights       = 5;
      constexpr GLuint k_area_lights        = 6;
      constexpr GLuint k_blas_nodes         = 7;
      constexpr GLuint k_tlas_nodes         = 8;
      constexpr GLuint k_tlas_instances     = 9;
      // ...
  }
  ```
- Shaders use matching explicit `layout(std430, binding = N)` declarations.

### 0.5 Render-target viewer
- New: `RenderTargetsPanel` under [src/ui/panels/](src/ui/panels/).
- Renderer exposes a named list `std::vector<RenderTargetView>` (name + `GLTexture*`) in its frame output.
- Panel: combo box to pick a target, ImGui::Image renders it. Drop-down swaps what the viewport shows.
- Land this in Phase 0 specifically because it becomes the primary diagnostic tool for Phase 1+.
- **Done when**: you can view RT0, RT1, depth, entity ID independently from the UI.

**Phase 0 exit criterion**: raster output byte-identical (or near enough — float precision aside). Commit boundary before starting Phase 1.

---

## Phase 1 — Acceleration structures

### 1.1 CPU BVH data model
- New: [src/renderer/raytracing/](src/renderer/raytracing/) subdirectory.
  - `Bvh.h` / `Bvh.cpp` — node struct + traversal utilities (CPU).
  - `BvhBuilder.h` / `BvhBuilder.cpp` — SAH top-down builder.
  - `AccelerationStructureCache.h` / `.cpp` — owns all BLAS + TLAS state, drives uploads.

- Node (CPU, identical layout on GPU):
  ```cpp
  struct BvhNode {
      glm::vec3 bmin;
      int32_t   left_or_first;   // if leaf: first primitive index
      glm::vec3 bmax;
      int32_t   right_or_count;  // if leaf: -(primitive_count) — sign flags leaf
  };
  static_assert(sizeof(BvhNode) == 32);
  ```
- **Granularity: per-primitive BLAS.** One BLAS per `(mesh, primitive_index)`. Materials are per-BLAS (no triangle-level material tagging needed). Aligns with `PrimitiveCacheKey`.
- BLAS content: indices into the existing global index SSBO; vertex data read from global vertex SSBO. No duplicate geometry storage.
- BLAS leaf stores a range into a **BLAS triangle index remap table** — permutation of `index_offset..index_offset+index_count` that groups spatially coherent triangles into leaves. This is the only new geometry-adjacent buffer.

### 1.2 SAH builder
- Top-down, bucket-binning SAH with 16 buckets per axis.
- Max leaf size: 4 triangles (tune later). Max depth: 64.
- Parallelize across BLAS builds using a simple worker thread pool (piggyback on existing async infra if easy; otherwise `std::async`). Within a BLAS, single-threaded for v1.
- Unit tests (in `tests/`):
  - Every input triangle referenced by exactly one leaf.
  - Every internal node's bounds contain both children's bounds.
  - Random-ray CPU traversal agrees with brute-force intersection on a synthetic scene.
- Time + memory stats saved into `AccelerationStructureCache::Stats` for the debug UI.

### 1.3 TLAS
- Per-frame, rebuilt when any transform is dirty. Instance list comes from `FrameSceneData`. Sponza-scale → full rebuild is cheap (< 1 ms on worker).
- TLAS instance (GPU):
  ```cpp
  struct GpuBlasInstance {
      mat4  world_from_local;
      mat4  local_from_world;   // precomputed inverse
      uint  blas_root_node;     // offset into global BLAS node SSBO
      uint  primitive_id;       // index into GpuPrimitive table (for material + geometry fetch)
      uint  entity_id;          // for picking/debug
      uint  _pad;
  };
  ```
- TLAS leaves store instance indices. BLAS is reached via `tlas_instance.blas_root_node`.
- Later refinement (out of scope for v1): TLAS **refit** instead of rebuild when only transforms change. Cheap improvement; defer.

### 1.4 Dirty-queue integration
- `AccelerationStructureCache::Sync(FrameSceneData&, RenderDirtyQueues&)`:
  - `mesh_entities` dirty or new primitive → (re)build BLAS for that primitive, async.
  - `transform_entities`, `hierarchy_entities`, `destroyed_entities`, `structure_changed` → mark TLAS dirty.
  - At end of sync: if TLAS dirty, rebuild now (sync, on main thread for simplicity first). If any BLAS is still building, skip this frame's RT passes and fall back to raster-only (renderer already works fine without RT).
- Upload strategy: on change, write to a staging CPU vector, then `glBufferSubData` (or persistent-mapped ring buffer — keep it simple, plain `glBufferSubData`).

### 1.5 Debug visualization (pick your order; I'd land in this order)
1. **BVH stats panel**: node counts per BLAS, TLAS node count, build time ms, memory MB, depth histogram.
2. **BVH wireframe viz**: new line-rendering pass. Toggle shows TLAS boxes; second toggle shows BLAS boxes for selected entity. Levels slider controls max depth drawn.
3. **Traversal heatmap**: a dedicated compute shader that traces one ray per pixel (primary visibility from camera) against the AS and outputs node-visit count → false-color texture → render-target viewer. Cheap, high-signal.

### 1.6 Tests
- `tests/BvhBuilderTests.cpp` covering the invariants in §1.2.
- `tests/TlasTests.cpp` — small synthetic scene with 3–4 instances, assert traversal correctness.

**Phase 1 exit criterion**: heatmap compute shader runs, renders plausible depth-traced false-color image into a render target, visible in the debug UI. No lighting effect yet.

---

## Phase 2 — Ray tracing pass infrastructure

### 2.1 Pass abstraction
- New: `src/renderer/raytracing/RayPass.h`.
- Shared GLSL utility headers (compiled as `#include`-emulated text concatenation by the shader manager — add a minimal `#include` preprocessor if not present; otherwise single-file includes):
  - `shaders/rt/bvh_traversal.glsl` — BLAS + TLAS stack-based traversal.
  - `shaders/rt/geometry_fetch.glsl` — from `(instance, primitive_in_blas, barys)` reconstructs position/normal/tangent/uv.
  - `shaders/rt/material_fetch.glsl` — reads material SSBO, samples bindless textures.
  - `shaders/rt/shading.glsl` — direct lighting (Cook-Torrance, same as `deferred_lighting.frag`, extracted and shared).
  - `shaders/rt/random.glsl` — blue noise + RNG (hash-based per-pixel seed).

### 2.2 Traversal specifics
- Stack: fixed-size local array, 64 entries (matches max depth).
- TLAS traversal:
  - Intersect ray with TLAS. On leaf: transform ray by `local_from_world` of the instance, recurse into BLAS.
  - On BLAS hit: return `(instance_id, triangle_id_in_blas, t, barys)`.
  - Keep closest hit across all BLAS intersections (not exiting early on first instance).
- Any-hit (for shadow rays): early-out on first hit with `t < t_max`.
- Robustness: watertight triangle intersection (Woop / Möller-Trumbore with edge tie-break) — worth the extra code to avoid cracks at seams.

### 2.3 Sync discipline
- Memory barriers (`glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT)`) between compute dispatches that write → read textures/SSBOs.
- Workgroup size: start with 8×8 for image passes; tune later.

**Phase 2 exit criterion**: a test compute shader in the heatmap from §1.5 now uses the shared traversal/fetch/shading includes and produces first-hit albedo — i.e. a "ray-traced camera" debug image identical (modulo filtering) to the raster output. This is the single best confidence test for AS correctness.

---

## Phase 3 — Ray-traced shadows

### 3.1 Shadow mask outputs
- One per-light shadow mask texture, `R8` format (enough for v1; can become `R16F` if denoising needs more precision).
- Allocated per active shadow-casting light (`cast_shadows == true`). Sized to render extent.
- All shadow masks exposed in the render-target viewer.

### 3.2 Shadow pass
- New: `RayTracedShadowPass`.
- One dispatch per shadow-casting light, 1 thread per pixel.
- Inputs: G-buffer depth, normal (for bias along normal), light SSBOs.
- Per pixel:
  - Reconstruct world pos from depth.
  - Offset along normal (bias term, small, roughness-scaled).
  - **Directional**: single ray in `-light.direction`.
  - **Point**: ray to `light.position`, `t_max = distance`.
  - **Area**: one stochastic sample on the area light's rectangle. Blue-noise-decorrelate between pixels and temporally between frames.
- Output: visibility [0,1] per light. For directional/point that's binary {0,1} before denoising; for area it's stochastic 0/1 before denoising.
- Multi-light: one dispatch per light (simpler, clean barriers) in v1. Batching later.

### 3.3 Composition
- `deferred_lighting.frag` changes: where it currently loops over lights, multiply each light's contribution by `texture(shadow_mask_<light>, uv).r`.
- Lights without a mask (non-shadow-casters) default to 1.0.
- Bias from separability is explicit and accepted.

### 3.4 Denoising
- Two-stage, shared denoiser module that operates on one shadow mask at a time:
  1. **Temporal accumulation**:
     - Reproject prior frame mask using camera motion (store previous-frame view/projection).
     - Clamp history with neighborhood min/max to reduce ghosting.
     - Blend: `mask = mix(new_mask, history, α)`, `α ≈ 0.9` if history valid, 0 otherwise.
     - Track history length per pixel for the next stage.
  2. **À trous bilateral filter**, variance-guided, 3–5 iterations with exponentially increasing stride:
     - Edge-stop weights: depth, normal, history length.
     - Small kernel (5×5) per iteration.
- New shaders under `shaders/rt/denoise/*.comp`.
- Parameters exposed in a new settings panel section (radius, α, number of iterations) — both for tuning and for the debug story.

### 3.5 Risks
- Noise level in area-light shadows with 1 spp is high; denoiser must be right before those lights look acceptable. Budget the majority of Phase 3 to denoising.
- Contact hardening is out of scope for v1; area light shadows will look slightly uniformly soft. Noted, not fixed.

**Phase 3 exit criterion**: directional + point shadows crisp and stable; area light shadows soft, temporally stable, no visible fireflies at modest motion.

---

## Phase 4 — Ray-traced reflections

### 4.1 Reflection pass
- New: `RayTracedReflectionPass`.
- Full-res in v1 (can drop to half-res later if perf demands).
- One thread per pixel. Reads G-buffer normal/roughness/metallic.

### 4.2 Sampling
- **Importance-sampled GGX** direction from view dir + normal + roughness. Use blue-noise-seeded stratified sampling.
- 1 sample per pixel per frame (denoise + accumulate handles the rest).
- Skip (output IBL directly) if `metallic < eps && roughness > threshold` — pure-dielectric rough surfaces aren't worth it.

### 4.3 Hit shading
- On hit, full direct lighting via shared `shaders/rt/shading.glsl`:
  - Loops over the light SSBOs — same data as deferred lighting.
  - **Shadows at hit points are skipped in v1** (would need recursive ray tracing budget). Accept bias; this is a standard simplification.
  - Emissive, ambient term from IBL diffuse.
- On miss, sample the prefiltered IBL environment using the ray direction + surface roughness.

### 4.4 Compositing
- Output: `rgba16f` reflection buffer; alpha = "confidence" (1 if a good RT sample was produced; 0 if we skipped).
- In `deferred_lighting.frag`, replace the IBL specular term with `mix(ibl_specular, rt_reflection, confidence * roughness_gate)`. Pure mirrors → 100% RT; rough metals → mostly RT; rough dielectrics → mostly IBL.

### 4.5 Denoising
- Reuse the Phase 3 denoiser module but with its own parameters:
  - Temporal reprojection weighted by hit-distance reprojection (not just camera motion) — important for moving reflective objects, nice-to-have if time allows; otherwise camera-only reprojection is fine and just slightly ghosty.
  - À trous with roughness-aware kernel width.
- Shared `shaders/rt/denoise/*` files; configure per-call.

**Phase 4 exit criterion**: Sponza's metallic surfaces show ray-traced reflections of the scene itself (self-reflection from ground, curtains, etc.), with IBL continuity on dielectric/rough surfaces.

---

## Phase 5 — Debug, telemetry, polish

Items, in priority order:

1. **Pass timings**: GL timer queries around each pass (raster + RT). Frame-time panel with a bar per pass.
2. **Ray budget HUD**: rays dispatched per frame, split shadows/reflections/other. Per-pixel average shown in an overlay.
3. **Per-target viewer improvements**: add remapping (exposure, channel isolation, histogram) — makes debugging shadow masks and normal buffers much less painful.
4. **BVH wireframe toggles**: if not landed in Phase 1.5, land here.
5. **Debug compare mode**: toggle raster-only vs hybrid output side-by-side in the viewport for A/B review.

---

## Cross-cutting concerns

- **Shader includes**: GLSL has no `#include`. Either add a tiny text preprocessor in `ShaderManager` or rely on manual concatenation. Adding the preprocessor costs an afternoon and pays off permanently; recommend doing it as part of Phase 2.1.
- **Error handling boundary**: bindless + compute + SSBO are capability gated. Do a single capability check at renderer init and either enable or refuse (hard error). Do not attempt to fall back to a non-RT path mid-flight.
- **Testing**: CPU BVH builder is the one piece with non-trivial algorithmic risk. Commit to unit tests there; most other correctness will be validated visually via the debug UI.
- **Memory footprint**: Sponza primitive count is low; even naively stored BVH + TLAS will be < 100 MB. Not a concern for v1.
- **Threading**: BLAS builds on worker threads; TLAS on main thread for simplicity (it's small and must complete before AS upload each frame). Revisit if TLAS build shows up in profiles.

---

## Explicitly deferred (not in this plan, acknowledged)

- Diffuse GI / path tracing. Separate phase later.
- TLAS refit (vs rebuild). Cheap upgrade post-v1.
- Recursive rays (shadows at reflection hit points, secondary bounces).
- SVGF-grade denoising. Temporal + à trous is the v1 ceiling.
- GPU LBVH / GPU refit. Out of scope.
- Half-res reflections / variable-rate shading. Possible perf lever later.
- Shader hot reload.

---

## Phase dependency graph

```
Phase 0 (refactors) ─┬─► Phase 1 (BVH) ─► Phase 2 (RT infra) ─┬─► Phase 3 (shadows) ─┐
                     │                                         │                       ├─► Phase 5 (polish)
                     └────────────────────────────────────────┴─► Phase 4 (reflections)┘
```

Phases 3 and 4 can proceed in parallel after Phase 2 lands; in practice do 3 first because its denoiser will be reused in 4, and area-light shadows are the most stressing workload — validating the denoiser there means reflections get a mature denoiser from day one.

---

**Rough effort shape** (not timelines — just relative proportions): Phase 0 and Phase 3 are the two biggest. Phase 1 is medium with high correctness risk but narrow surface. Phase 2 is small but load-bearing. Phase 4 is medium. Phase 5 is background work throughout.
