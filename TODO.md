# TODO

RinGL should grow through small end-to-end slices. The first priority is not broad API coverage; it is proving that OpenGL ES state can be translated cleanly into RinGPU without leaking GL semantics into the native GPU boundary.

## Phase 0 — Repository bootstrap

- [x] Add the initial source/include/test directory layout.
- [x] Choose and document the build system used by RinOS integration.
- [x] Add formatting and warning policy for C/C++ sources.
- [x] Add a minimal CI build and test job.
- [x] Define a versioning policy for public RinGL headers.
- [x] Document how RinGL discovers or receives a RinGPU device/queue.

## Phase 1 — Context and core GL state

- [x] Implement `GLContext` creation and destruction.
- [x] Implement current-context binding for one thread.
- [x] Add GL error state and `glGetError` semantics.
- [x] Define internal object-name allocation with generation/lifetime checks.
- [x] Add dirty-state tracking so ordinary state changes do not emit RinGPU commands immediately.
- [x] Implement basic integer state queries for bindings, limits, program, viewport/scissor, and raster state.
- [x] Add tests for context isolation, object-name reuse, error behavior, and fixed-function state queries.

## Phase 2 — Buffers and vertex input

- [x] Implement buffer object creation/deletion/binding.
- [x] Implement `glBufferData` and bounded buffer uploads through RinGPU.
- [x] Implement array-buffer and element-array-buffer state.
- [x] Implement the initial vertex attribute state model.
- [x] Translate supported GL vertex formats into RinGPU vertex layouts.
- [x] Reject unsupported or out-of-range vertex fetches before submission.
- [x] Add buffer lifetime and bounds tests.

## Phase 3 — Shaders and programs

- [x] Define the supported initial GLSL ES language/version profile.
- [x] Implement shader object lifecycle and source storage.
- [x] Implement a bounded GLSL ES lexer/parser with initial semantic validation.
- [x] Lower the current scalar GLSL ES subset directly to RinShader RSH1.
- [x] Reuse RinShader validation through public `ringpu_create_shader_module()` before backend shader creation.
- [x] Implement the initial vertex/fragment shader linking checks.
- [x] Implement program object lifecycle and `glUseProgram`.
- [x] Add first-slice program reflection for shader I/O and shader-module handles.
- [x] Add positive and negative shader frontend tests.
- [x] Add RSH1 lowering tests for header, stage, input/output counts, and IR invalidation.
- [x] Add RinGPU shader-module realization and lifetime tests.
- [x] Add linked-program reflection tests for the non-resource shader subset.
- [x] Parse bounded `uniform sampler2D` declarations and retain names through shader compilation.
- [x] Link sampler uniforms into program locations and implement `getUniformLocation`/`uniform1i`-style state.
- [x] Report linked sampler uniforms through program reflection.
- [x] Parse and validate the initial fragment-shader `texture2D(sampler2D, vec2(...))` form.
- [x] Fail texture sampling RSH1 lowering explicitly rather than inventing a private IR encoding.
- [ ] Lower `texture2D()` after RinShader exposes an unambiguous 2D-coordinate/RGBA sample operation.
- [ ] Add general vertex-to-fragment varying support after RinShader/RinGPU exposes a position-builtin + user-varying interface contract.

## Phase 4 — First hardware-rendered triangle

- [x] Create a default framebuffer representation backed by an embedding-supplied presentable RinGPU image.
- [x] Implement color clear through a RinGPU render-pass clear submission.
- [x] Build the first-slice pipeline-cache key from program, vertex layout, attachment format, and current immutable state.
- [x] Lazily create/cache RinGPU graphics pipelines with bounded eviction and lifetime cleanup.
- [x] Begin/end RinGPU render passes for the first clear and draw operations.
- [x] Implement first-slice `glDrawArrays(GL_TRIANGLES, ...)` translation.
- [x] Track the default color image between UNDEFINED, PRESENT, and COLOR_TARGET states and emit required transitions.
- [x] Add the OS-Core adapter that maps the RinGL operation table to public RinGPU buffer, shader, pipeline, command, submit, and present APIs.
- [x] Add an OS-Core surface bridge that creates a RinGL context over the existing WebGL RinGPU core/queue/color image.
- [ ] Make the selected OS-Core RinGPU backend execute `DRAW_VERTICES` for the bridged target and present the first visible triangle.
- [x] Add a deterministic mock-RinGPU triangle integration test covering upload, shaders, pipeline, clear, draw, submit, and present ordering.

## Phase 5 — Indexed drawing and textures

- [x] Implement `glDrawElements` for `UNSIGNED_SHORT`/`UNSIGNED_INT` with robust index-buffer and referenced-vertex range validation.
- [x] Retain bounded buffer shadow contents so indexed draws can reject unsafe vertex fetches before RinGPU submission.
- [x] Map indexed draws through the append-only RinGPU adapter boundary.
- [x] Add GLES 2.0 `UNSIGNED_BYTE` index support using the additive native RinGPU `UINT8` index format.
- [x] Implement texture object lifecycle and eight texture-unit bindings.
- [x] Implement initial level-0 `RGBA`/`UNSIGNED_BYTE` 2D texture storage with bounded dimensions.
- [x] Implement robust CPU-side `texImage2D` zero initialization and bounded `texSubImage2D` updates.
- [x] Implement GLES-style texture filtering/wrap sampler state and invalidate realized samplers when it changes.
- [x] Implement sampler uniform locations and program-selected texture-unit state.
- [x] Lazily realize complete level-0 texture storage as CPU-visible RinGPU sampled RGBA8 images and upload the canonical shadow contents.
- [x] Lazily map texture filtering/wrap state to RinGPU sampler objects.
- [x] Add fake-RinGPU tests for image/sampler realization, cache hits, invalidation, and level-zero completeness.
- [ ] Bind realized sampled images/samplers to RinGPU graphics resource slots before draws (waiting on texture sampling IR lowering and varying linkage).
- [ ] Add a textured-triangle integration test.

## Phase 6 — Framebuffers and fixed-function state

- [ ] Implement framebuffer/renderbuffer object models.
- [ ] Map FBO attachments to RinGPU render-pass attachments.
- [ ] Implement framebuffer completeness validation for supported combinations.
- [x] Implement viewport and scissor GL state, validation, defaults, and queries.
- [x] Implement face-culling/front-face GL state, validation, defaults, and queries.
- [ ] Apply viewport/scissor/culling to submitted rendering after RinGPU exposes a rasterization-state contract.
- [ ] Implement depth test/write state and map the supported subset to RinGPU depth pipelines.
- [ ] Implement blending and color write masks and map the supported subset to RinGPU blend pipelines.
- [ ] Implement stencil support once the required RinGPU contract is available.
- [ ] Include all immutable draw-relevant state in pipeline caching.

## Phase 7 — Data movement, synchronization, and observability

- [ ] Implement clear/copy paths that must end or split render passes.
- [ ] Implement `glFlush` semantics.
- [ ] Implement `glFinish` semantics using RinGPU fences.
- [ ] Implement bounded framebuffer readback.
- [ ] Implement buffer readback where required by the target profile.
- [ ] Define device-loss handling and GL-visible failure behavior.
- [ ] Add tests for ordering across draws, copies, barriers, flushes, and readbacks.

## Phase 8 — OpenGL ES compatibility expansion

- [ ] Inventory required OpenGL ES 2.0 entry points, enums, limits, and queries.
- [ ] Track implementation status per API instead of claiming version support early.
- [ ] Close GLES 2.0 semantic gaps found by conformance-style tests.
- [ ] Decide the boundary for OpenGL ES 3.x features.
- [ ] Add VAOs, instancing, additional texture formats, MRT, and other GLES 3.x features only after the underlying RinGPU contracts are ready.

## Phase 9 — WebGL-facing readiness

- [ ] Keep WebGL validation/security policy outside the raw RinGL GL implementation.
- [ ] Define a clean embedding API for browser contexts and surfaces.
- [ ] Ensure robust buffer/texture access independent of backend behavior.
- [ ] Ensure context loss can be propagated predictably to a browser implementation.
- [ ] Audit allocation limits and integer overflow paths for untrusted content.
- [ ] Add WebGL-oriented negative tests for malformed state and shader input.

## Phase 10 — Testing and conformance

- [ ] Add unit tests for every state transition and validation rule.
- [x] Add mock-RinGPU tests that inspect generated commands without requiring hardware for the first triangle path.
- [ ] Add hardware/QEMU integration tests where RinGPU support exists.
- [ ] Add shader compiler differential/negative tests.
- [ ] Add API trace tests for representative GL sequences.
- [ ] Run an appropriate GLES conformance suite when the implementation is mature enough.
- [ ] Document every known incompatibility before advertising a supported GL/GLES version.

## RinGPU/RinShader dependencies

See `docs/ringpu-gaps.md`. RinGL should stop cleanly at these native-boundary gaps rather than introducing GL-specific behavior or private shader encodings into RinGPU.

## Optional software backend

- [ ] Evaluate whether `OS-Core/libs/aquamarine` can be evolved into a useful RinGL software backend.
- [ ] Keep the existing Aquamarine software graphics library distinct from Aquamarine Shader Language.
- [ ] Do not make the GL frontend depend on software-rasterizer-specific types.
- [ ] If implemented, keep hardware and software backends behaviorally aligned through shared GL validation/state tests.

## First milestone definition of done

The first milestone is complete when a RinOS process can create a RinGL context, compile/link a minimal vertex and fragment shader, upload a vertex buffer, clear the default framebuffer, issue `glDrawArrays(GL_TRIANGLES, ...)`, and present the result through RinGPU, with validation and integration tests covering the same path.
