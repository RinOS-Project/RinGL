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
- [ ] Implement basic state queries required by the initial profile.
- [x] Add tests for context isolation, object-name reuse, and error behavior.

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
- [ ] Lower supported GLSL ES constructs directly to RinShader IR.
- [ ] Reuse RinShader validation before shader modules reach a RinGPU backend.
- [ ] Implement vertex/fragment shader linking and interface checks.
- [ ] Implement program object lifecycle and `glUseProgram`.
- [ ] Add uniform/reflection metadata needed by the first rendering slice.
- [x] Add positive and negative shader frontend tests.

## Phase 4 — First hardware-rendered triangle

- [ ] Create a default framebuffer representation backed by a presentable RinGPU image.
- [ ] Implement color clear.
- [ ] Build a pipeline-cache key from program, vertex layout, attachment format, and fixed-function state.
- [ ] Lazily create/cache RinGPU graphics pipelines.
- [ ] Automatically begin/end a RinGPU render pass around compatible GL operations.
- [ ] Implement `glDrawArrays` for triangles.
- [ ] Translate GL-visible resource state into required RinGPU transitions/barriers.
- [ ] Submit and present the first triangle through RinGPU.
- [ ] Add a deterministic triangle integration test or sample.

## Phase 5 — Indexed drawing and textures

- [ ] Implement `glDrawElements` with robust index/vertex range validation.
- [ ] Implement texture object lifecycle and texture-unit state.
- [ ] Implement the first 2D color texture formats.
- [ ] Implement texture upload/sub-upload.
- [ ] Implement sampler state and map it to RinGPU sampler objects.
- [ ] Implement sampled-image/program bindings.
- [ ] Add a textured-triangle integration test.

## Phase 6 — Framebuffers and fixed-function state

- [ ] Implement framebuffer/renderbuffer object models.
- [ ] Map FBO attachments to RinGPU render-pass attachments.
- [ ] Implement framebuffer completeness validation for supported combinations.
- [ ] Implement viewport and scissor state.
- [ ] Implement face culling and front-face state.
- [ ] Implement depth test/write state.
- [ ] Implement blending and color write masks.
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
- [ ] Add mock-RinGPU tests that inspect generated commands without requiring hardware.
- [ ] Add hardware/QEMU integration tests where RinGPU support exists.
- [ ] Add shader compiler differential/negative tests.
- [ ] Add API trace tests for representative GL sequences.
- [ ] Run an appropriate GLES conformance suite when the implementation is mature enough.
- [ ] Document every known incompatibility before advertising a supported GL/GLES version.

## Optional software backend

- [ ] Evaluate whether `OS-Core/libs/aquamarine` can be evolved into a useful RinGL software backend.
- [ ] Keep the existing Aquamarine software graphics library distinct from Aquamarine Shader Language.
- [ ] Do not make the GL frontend depend on software-rasterizer-specific types.
- [ ] If implemented, keep hardware and software backends behaviorally aligned through shared GL validation/state tests.

## First milestone definition of done

The first milestone is complete when a RinOS process can create a RinGL context, compile/link a minimal vertex and fragment shader, upload a vertex buffer, clear the default framebuffer, issue `glDrawArrays(GL_TRIANGLES, ...)`, and present the result through RinGPU, with validation and integration tests covering the same path.
