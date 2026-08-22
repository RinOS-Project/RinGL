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
- [x] Lower the initial one-sampler/one-call constant-coordinate `texture2D()` form to public RSH1 `SAMPLE_IMAGE_2D_F32` component operations.
- [x] Add texture RSH1 tests for resource slots, component selectors, and RGBA output stores.
- [x] Parse/link/lower the initial `varying vec2` profile and map it to public RinGPU perspective varying descriptors.
- [x] Lower `texture2D(sampler2D, varyingVec2)` for the initial textured-triangle profile.
- [ ] Expand texture expressions beyond the initial constant/varying-coordinate one-sampler slice.
- [ ] Expand varying support beyond the initial `vec2` perspective-interpolated profile.

## Phase 4 — First hardware-rendered triangle

- [x] Create a default framebuffer representation backed by an embedding-supplied presentable RinGPU image.
- [x] Implement color clear through a RinGPU render-pass clear submission.
- [x] Build the first-slice pipeline-cache key from program, vertex layout, attachment format, and current immutable state.
- [x] Lazily create/cache RinGPU graphics pipelines with bounded eviction and lifetime cleanup.
- [x] Begin/end RinGPU render passes for the first clear and draw operations.
- [x] Implement first-slice `glDrawArrays(GL_TRIANGLES, ...)` translation.
- [x] Track the default color image between UNDEFINED, PRESENT, and COLOR_TARGET states and emit required transitions.
- [x] Expose the post-submit default color-image state to trusted embeddings so a caller-owned presentation surface can remain synchronized across RinGL presentation and reuse.
- [x] Add the OS-Core adapter that maps the RinGL operation table to public RinGPU buffer, shader, pipeline, command, submit, and present APIs.
- [x] Extend the adapter to public native graphics pipeline, raster-state, typed resource-binding, and graphics-resource-bind commands.
- [x] Add an OS-Core surface bridge that creates a RinGL context over the existing WebGL RinGPU core/queue/color image.
- [x] Confirm the selected OS-Core software RinGPU backend executes generic `DRAW_VERTICES`; presentation/display integration remains a RinGPU/display concern rather than RinGL logic.
- [x] Add a deterministic mock-RinGPU triangle integration test covering upload, shaders, pipeline, clear, draw, submit, and present ordering.

## Phase 5 — Indexed drawing and textures

- [x] Implement `glDrawElements` for `UNSIGNED_BYTE`/`UNSIGNED_SHORT`/`UNSIGNED_INT` with robust index-buffer and referenced-vertex range validation.
- [x] Retain bounded buffer shadow contents so indexed draws can reject unsafe vertex fetches before RinGPU submission.
- [x] Map indexed draws through the append-only RinGPU adapter boundary, including native RinGPU `UINT8` indices.
- [x] Implement texture object lifecycle and eight texture-unit bindings.
- [x] Implement initial level-0 `RGBA`/`UNSIGNED_BYTE` 2D texture storage with bounded dimensions.
- [x] Implement robust CPU-side `texImage2D` zero initialization and bounded `texSubImage2D` updates.
- [x] Implement GLES-style texture filtering/wrap sampler state and invalidate realized samplers when it changes.
- [x] Implement sampler uniform locations and program-selected texture-unit state.
- [x] Lazily realize complete level-0 texture storage as CPU-visible RinGPU sampled RGBA8 images and upload the canonical shadow contents.
- [x] Lazily map texture filtering/wrap state to RinGPU sampler objects.
- [x] Add fake-RinGPU tests for image/sampler realization, cache hits, invalidation, and level-zero completeness.
- [x] Transition sampled images to `SHADER_READ`, create typed image/sampler bind groups, and bind them inside the render pass before draws.
- [x] Add a native-contract textured-draw mock test covering texture realization, resource transition, bind group creation, raster state, and draw ordering.
- [x] Add a varying-backed textured-triangle integration test with interleaved position/UV input and perspective-interpolated texture coordinates.

## Phase 6 — Framebuffers and fixed-function state

- [x] Implement framebuffer/renderbuffer object models (bounded lifecycle, binding, level-zero `COLOR_ATTACHMENT0` tracking, RGBA8 renderbuffer storage, and automatic detach on texture/renderbuffer deletion).
- [x] Map supported RGBA8 color FBO texture/renderbuffer attachments to lazy RinGPU color targets, including image-state transitions, render-pass clear/draw targets, and `COPY_SOURCE` readback capability.
- [x] Implement framebuffer completeness validation for the supported single level-zero RGBA8 color-attachment combinations.
- [x] Implement viewport and scissor GL state, validation, defaults, and queries.
- [x] Implement face-culling/front-face GL state, validation, defaults, and queries.
- [x] Implement depth-test function/write-mask GL state, defaults, validation, dirty tracking, and queries.
- [x] Implement initial blend factor/equation and color-write-mask GL state, defaults, validation, dirty tracking, and queries.
- [x] Map supported blend, cull, front-face, and color-write state into native RinGPU graphics pipelines and include it in pipeline caching.
- [x] Map viewport/scissor state through dynamic RinGPU raster-state commands, including finite negative viewport origins and clipped scissor rectangles.
- [x] Preserve no-op semantics for zero-area viewport, all-channel color mask off, and `CULL_FACE` with `FRONT_AND_BACK` in the current color-only profile.
- [x] Map D32 clear and every GLES depth comparison (`NEVER`, `LESS`, `EQUAL`, `LEQUAL`, `GREATER`, `NOTEQUAL`, `GEQUAL`, `ALWAYS`) for an embedding-supplied default framebuffer through native RinGPU depth render passes.
- [x] Map D32 custom depth-renderbuffer FBO attachments with matching RGBA8 color attachments to lazy RinGPU images, depth render-pass clear/load, and all eight depth-tested draw predicates; strict C11 and the RinOS surface integration test cover completeness, dimension mismatch rejection, clear, and draw behavior.
- [ ] Implement stencil support after framebuffer/renderbuffer depth-stencil storage is in place.

## Phase 7 — Data movement, synchronization, and observability

- [ ] Implement clear/copy paths that must end or split render passes.
- [x] Implement current immediate-submit `glFlush` semantics.
- [x] Implement `glFinish` using an optional versioned sync extension, RinGPU fences, fenced queue submission, and `ringpu_wait_fence()`.
- [x] Implement bounded current-color-framebuffer `RGBA/UNSIGNED_BYTE` `glReadPixels` logic through COPY_SOURCE transition, completion wait, RinGPU image readback, and BGRA-to-RGBA swizzle where required.
- [x] Add a fake-RinGPU synchronization/readback test covering monotonic fence values, waits, COPY_SOURCE transition, and pixel swizzle.
- [x] Enable readback on the RinOS WebGL surface color image by creating it with `COPY_SOURCE` usage and `CPU_READABLE` in addition to its existing present/color-target flags; the focused `rin_webgl_ringl_bridge_test` verifies clear, BGRA-to-RGBA readback, and present through the shared surface.
- [ ] Define device-loss handling and GL-visible failure behavior.
- [ ] Add broader tests for ordering across draws, copies, barriers, flushes, finish, and readbacks.

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
- [x] Add a native-contract mock test for textured resource/raster ordering.
- [x] Add RSH1 regression coverage for the initial varying-backed texture path.
- [x] Add mock synchronization/readback coverage for `finish` and default-framebuffer `readPixels`.
- [x] Add a real RinGPU surface integration test that reads texture-FBO clear/draw output and renderbuffer-FBO clear output back as RGBA pixels.
- [x] Keep the standalone Meson build clean under its C11/`-Werror` policy, including public-header self-containment and texture-module realization coverage.
- [ ] Add hardware/QEMU integration tests where RinGPU support exists.
- [ ] Add shader compiler differential/negative tests.
- [ ] Add API trace tests for representative GL sequences.
- [ ] Run an appropriate GLES conformance suite when the implementation is mature enough.
- [ ] Document every known incompatibility before advertising a supported GL/GLES version.

## RinGPU/RinShader dependencies

See `docs/ringpu-gaps.md`. There are currently no known native-contract blockers for the GLES 2.0 milestone; new entries should be added only for concrete representational gaps.

## Optional software backend

- [ ] Evaluate whether `OS-Core/libs/aquamarine` can be evolved into a useful RinGL software backend.
- [ ] Keep the existing Aquamarine software graphics library distinct from Aquamarine Shader Language.
- [ ] Do not make the GL frontend depend on software-rasterizer-specific types.
- [ ] If implemented, keep hardware and software backends behaviorally aligned through shared GL validation/state tests.

## First milestone definition of done

The first milestone is complete when a RinOS process can create a RinGL context, compile/link a minimal vertex and fragment shader, upload a vertex buffer, clear the default framebuffer, issue `glDrawArrays(GL_TRIANGLES, ...)`, and present the result through RinGPU, with validation and integration tests covering the same path.
