### GBuffer CPU Overhead Reduction (Staged UBO/SSBO, Core OpenGL)

#### Summary
Replace per-primitive uniform traffic with GPU buffers (UBO/SSBO), then minimize texture/state churn via material-sorted draw submission.  
Target outcome: `GBufferPass::DrawOpaqueMasked` CPU time drops materially, with `uniform_updates` near zero and texture binds reduced to material changes instead of primitive frequency.

#### Implementation Changes
- **Stage 1: Immediate overhead cuts (low risk)**
  - Add uniform-location caching in `GLShaderProgram` so any remaining uniform writes stop calling `glGetUniformLocation` every draw.
  - Add tiny GL state cache in GBuffer pass (`last VAO`, per-unit `last texture`) to skip redundant `glBindTexture`/`vao.Bind()` calls.
  - Build a `DrawItem` list once per frame for opaque+masked and stable-sort by material texture key (`base/mr/normal image ids`, sampler state), then by geometry key.

- **Stage 2: UBO/SSBO-driven per-draw data (main fix)**
  - Upgrade GBuffer shaders to GLSL 4.60 (`shaders/gbuffer.vert`, `shaders/gbuffer.frag`).
  - Introduce GPU data blocks:
    - `FrameUBO` (binding 0): `view`, `projection` (updated once/frame).
    - `InstanceSSBO` (binding 1): `model`, `normal_matrix`, `material_index`, `entity_id` (one element per draw item).
    - `MaterialSSBO` (binding 2): base color/alpha, metallic/roughness, normal scale, alpha mode/cutoff, texcoord selectors, texture-present flags.
  - Replace per-draw uniforms with `glDrawElementsInstancedBaseInstance(..., instanceCount=1, baseInstance=draw_index)`.
  - In shader, index SSBO data with `gl_BaseInstance`; remove per-draw `u_model`, `u_base_color`, `u_*` material uniforms, and `u_instance_id`.

- **Stage 3: Texture binding policy (core-only)**
  - Keep 3 fixed sampler units for base/mr/normal, but bind only when sorted material key changes.
  - Keep white/flat-normal fallback textures; use them when texture index/handle is invalid.
  - Add material cache records so texture resolve work is not repeated in hot draw loop.

- **Stage 4: Optional follow-up if still CPU-bound**
  - Move to packed geometry buffers + `glMultiDrawElementsIndirect` batches (same SSBO layouts reused).
  - This is optional and only needed if draw-call count remains the dominant cost after Stage 2/3.

#### API / Type Changes
- `RendererStats::GBufferStats` additions:
  - `material_batches`
  - `state_changes_skipped`
  - `ssbo_upload_bytes`
- `GLShaderProgram` additions:
  - cached uniform lookup path (for non-SSBO passes too)
  - optional block-binding helper for UBO setup
- Internal GBuffer pass structs:
  - `GpuInstanceData`
  - `GpuMaterialData`
  - `DrawItem` + `MaterialSortKey`

#### Test Plan
- Visual parity:
  - Compare old/new GBuffer outputs (`rt0`, `rt1`, entity id, depth) on Sponza and one high-texture scene.
- Performance acceptance:
  - `uniform_updates` in GBuffer close to 0 (except pass-global setup).
  - `texture_binds` scales with material batches, not primitive count.
  - Tracy shows lower `GBufferPass::DrawOpaqueMasked` CPU time than current 4.73 ms on the same camera path.
- Edge scenarios:
  - primitives with missing textures/materials (fallback textures still correct),
  - alpha-masked materials (cutoff behavior preserved),
  - mixed UV0/UV1 mappings and normal-scale correctness.

#### Assumptions / Defaults
- OpenGL 4.6 runtime is required and available; GLSL 4.60 is allowed for GBuffer shaders.
- Optimization applies to opaque+masked passes; blended path stays unchanged.
- Texture strategy stays portable core-GL (no bindless dependency).
