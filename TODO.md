# TODO

RinGL should grow through small end-to-end slices. The first priority is not broad API coverage; it is proving that OpenGL ES state can be translated cleanly into RinGPU without leaking GL semantics into the native GPU boundary.

## Phase 0 — Repository bootstrap

- [x] Add the initial source/include/test directory layout.
- [x] Choose and document the build system used by RinOS integration.
- [x] Add formatting and warning policy for C/C++ sources.
- [x] Add a minimal CI build and test job.
- [x] Define a versioning policy for public RinGL headers.
- [x] Document how RinGL discovers or receives a RinGPU device/queue.
- [x] Provide a CMake `RinGL::RinGL` static target whose source list matches the Meson library, so the Ladybird `AK_OS_RINOS` build links the tested RinGL implementation directly.

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
  - [x] Implement transactional `glBufferSubData`-style range replacement:
    validate the complete range before submission and swap a staged RinGPU
    backing buffer only after its full upload succeeds.
- [x] Implement array-buffer and element-array-buffer state.
- [x] Implement the initial vertex attribute state model.
- [x] Translate supported GL vertex formats into RinGPU vertex layouts.
  - [x] Map WebGL 1 `FLOAT`/`BYTE`/`UNSIGNED_BYTE`/`SHORT`/`UNSIGNED_SHORT`
    attributes, including normalized 8/16-bit conversion and byte strides, to
    executable scalar RinGPU input formats.
  - [x] Resolve a linked program's active generic attribute locations to its
    dense scalar RinGPU inputs, so unrelated enabled arrays are not fetched.
  - [x] Translate disabled active generic attribute current values into explicit
    Float32 RinGPU inputs, including WebGL's `(0, 0, 0, 1)` default and the
    `vertexAttrib[1-4]f` update rules, only when the embedding advertises the
    constant-input capability.
- [x] Reject unsupported or out-of-range vertex fetches before submission.
- [x] Add buffer lifetime and bounds tests.

## Phase 3 — Shaders and programs

- [x] Define the supported initial GLSL ES language/version profile.
- [x] Implement shader object lifecycle and source storage.
  - [x] Expose bounded shader-source inspection: the complete source length is
    queryable and a caller-owned buffer receives only a NUL-terminated prefix
    that fits. Invalid handles report `INVALID_VALUE` without modifying caller
    storage; the focused shader test covers complete, truncated, query-only,
    and invalid-handle cases.
  - [x] Retain an attached shader after `deleteShader`, reject reuse of its
    deleted public name, and release it only after `detachShader` or program
    destruction.
- [x] Implement a bounded GLSL ES lexer/parser with initial semantic validation.
- [x] Lower the current scalar GLSL ES subset directly to RinShader RSH1.
- [x] Reuse RinShader validation through public `ringpu_create_shader_module()` before backend shader creation.
- [x] Implement the initial vertex/fragment shader linking checks.
- [x] Implement program object lifecycle and `glUseProgram`.
- [x] Add first-slice program reflection for shader I/O and shader-module handles.
  - [x] Preserve linked vertex attribute names/widths and expose stable active
    attribute locations for the bounded supported shader profiles, including
    `bindAttribLocation` requests applied only by the next successful link.
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
- [x] Add native packed WebGL 1 texture input: `RGB`/
  `UNSIGNED_SHORT_5_6_5`, `RGBA`/`UNSIGNED_SHORT_4_4_4_4`, and
  `RGBA`/`UNSIGNED_SHORT_5_5_5_1` use two-byte RGB565/RGBA4/RGB5_A1 shadow and
  RinGPU sampled-image storage rather than RGBA8 normalization. Mock-RinGPU
  tests verify format/upload/sub-image bytes and the actual RinGL-to-RinGPU
  bridge verifies each `texture2D()` output, including packed alpha semantics.
- [ ] Expand texture expressions beyond the initial constant/varying-coordinate one-sampler slice.
  - [x] Lower `texture2D(sampler2D, vec2(float))` as a finite constant coordinate splat, with the same scalar value stored in both RSH1 sampling-coordinate registers.
  - [x] Add the bounded two-sampler/two-call constant-coordinate addition profile: exactly two declared samplers are each sampled once and combined as `texture2D(a, vec2(...)) + texture2D(b, vec2(...))`. Lowering emits declaration-ordered resource pairs `[0, 1]` and `[2, 3]`; RinGL creates one typed bind group, transitions both distinct images before the draw, and direct/indexed RinGPU/Aquamarine bridge tests read the resulting yellow pixel.
  - [x] Generalize the constant-coordinate additive profile to one through eight declared samplers: a multi-declaration chain uses each sampler exactly once and lowering emits the complete declaration-ordered image/sampler pair table. The formulaic RSH1 layout consumes at most 85 instructions and 80 registers, so the RinGL ceiling is raised to 96 while remaining below RinGPU's public 256-register limit. Strict IR tests preserve the one/two-sampler bytecode layouts, inspect a three-sampler reverse declaration-order chain, and compile/lower all eight samplers; the RinGPU/Aquamarine bridge renders reverse-order red + green + blue and the full eight-sampler/16-resource ceiling as white via direct and indexed draws.
  - [x] Permit a declared `sampler2D` to occur in one through eight finite constant-coordinate calls in the additive chain, including when other declarations are unused. Every call gets independent coordinate/sample/accumulator registers; active declarations are compacted in declaration order into dense typed RSH1 image/sampler pairs, and RinGL maps those pairs back to the program uniform by name when it builds the selective bind group. Strict IR and RinGL→RinGPU→Aquamarine tests leave the first declaration unbound while the repeated second sampler draws its real clamped result. RinGPU's invariant that every declared RSH1 resource has a type and use remains intact.
  - [x] Generalize the perspective `varying vec2` additive profile to one through eight texture calls over one through eight declarations. Calls share the same interpolated coordinate, are added left-to-right, and may repeat one sampler while other declarations are inactive. Active declarations compact in declaration order into dense image/sampler pairs and the saved mapping selects the corresponding linked uniform. The one/two-call layouts are preserved; the formulaic maximum is 69 instructions and 64 registers. Strict IR covers a three-call repeated second declaration, while the RinGL→RinGPU→Aquamarine bridge leaves the first declaration inactive and renders the repeated active green texture.
  - [x] Extend the shared-`varying vec2` chain with an independent, finite literal offset for every call: `texture2D(sampler, uv + vec2(u, v))` and `texture2D(sampler, uv - vec2(u, v))`. The lexer accepts finite decimal/exponent literals, lowering emits two `CONST_F32` plus two `ADD_F32`/`SUB_F32` operations immediately before that call's sample, and reuses four temporary scalar registers only after the prior sample has consumed them. The all-offset ceiling is 101 instructions and 68 registers. Strict IR verifies both signs, exact Float32 bits, and temporary-register reuse; the RinGL→RinGPU→Aquamarine bridge samples a distinct 2×2 blue texel selected only by `uv + vec2(0.5, -0.5)`.
  - [x] Lower a bounded two-`varying vec2` texture profile: `firstUv` and `secondUv` map to separate RSH1 perspective input pairs, and every one-through-eight call selects either pair while preserving declaration-ordered sampler resources. The existing two-`vec2` vertex/profile reflection supplies all four scalar varying descriptors. Strict IR verifies the second pair uses its distinct physical input registers; the RinGL→RinGPU→Aquamarine bridge reads a 64-red texel through `firstUv` and a 128-green texel through `secondUv`, so a collapsed coordinate pair cannot produce the required `(64,128,0,255)` output.
  - [x] Lower `firstUv + secondUv` and `firstUv - secondUv` as bounded two-varying sampler coordinates. The parser only permits declared `vec2` varyings, and RSH1 emits two component-wise operations against the separate physical input pairs. Strict IR verifies those input locations and the bridge samples the distinct black texel at the summed `(1, 1)` coordinate rather than either red/green operand texel.
  - [x] Accept one fragment-local direct alias, `vec2 localUv = uv;`, before `gl_FragColor` and use it as the shared varying-coordinate texture argument. The parser records a typed vec2 local and the bounded lowerer accepts only an alias of its one declared `varying vec2`; every RSH1 sample still reads the live perspective input registers. Strict IR preserves the direct-coordinate resource/sample layout.
  - [x] Lower one fragment-local finite affine coordinate, `vec2 localUv = uv +/- vec2(u, v);`, before the shared varying-coordinate texture chain. The local `CONST_F32` plus `ADD_F32`/`SUB_F32` operations execute once before samples; optional per-call offsets use a separate four-register temporary so their Float32 order is preserved.
  - [x] Permit a second direct/finite-affine local `vec2` derived from the first, and lower the two declarations in source order before samples. Each non-direct local has its own RSH1 temporary quartet; a later call-local offset uses a separate quartet, preserving Float32 evaluation order. The combined eight-call ceiling is 109 instructions and 76 registers. Strict IR verifies the two local operations and the 8-call/16-resource ceiling; the RinGL→RinGPU→Aquamarine bridge samples the distinct blue texel selected only through `baseUv` then `sampleUv`.
  - [x] Permit up to six direct/finite-affine local `vec2` values derived in source order before sampling. The worst all-affine eight-call plus call-offset layout is 125 instructions and 92 registers, inside the RSH1 128-instruction/96-register limits. Strict IR verifies the complete six-stage dependency chain; the actual RinGL→RinGPU→Aquamarine draw reaches the blue texel only after the sixth local subtracts the final `vec2(0.0, 0.25)`.
  - [x] Extend local and per-call bounded coordinate operations from addition/subtraction to component-wise multiplication and nonzero division by finite `vec2` literals. They use the existing RSH1 `MUL_F32`/`DIV_F32` execution path; zero divisor components are rejected while lowering. Strict IR verifies both opcodes and the focused bridge executes the six-local chain containing both operations before it reads the blue texel.
  - [x] Accept one named local coordinate from both declared perspective inputs: `vec2 mixedUv = firstUv +/- secondUv;` followed by `texture2D(sampler, mixedUv)`. RinGL emits two component-wise `ADD_F32`/`SUB_F32` operations against the distinct RSH1 input pairs before sampling; strict IR covers source order and both signs, while the actual RinGL→RinGPU→Aquamarine bridge reaches the black `(1, 1)` texel rather than either operand's red/green texel.
  - [x] Permit the bounded two-varying local result to feed further direct/finite-affine locals: `mixedUv = firstUv +/- secondUv; sampleUv = mixedUv + vec2(...)` (and the other finite `-`, `*`, `/` operations). The combine result is retained in RSH1 registers and each later affine stage reads its predecessor in source order; the current capacity-aware profile permits eight total local declarations when the final RSH1 budget fits. Strict IR verifies the result/register chain; the actual bridge samples a fourth 4×4 texel reachable only after the two-varying combine and local offset.
  - [x] Lower bounded varying-coordinate texture color operations with finite `vec4` literals: `+`, `-`, `*`, or nonzero `/` after one sample, or after a one-through-eight sample additive chain only when explicitly parenthesized. RinGL emits four `CONST_F32` plus component-wise RSH1 operations after the live RinGPU samples/adds and rejects layouts beyond the RSH1 instruction/register ceilings or any literal zero division component. The finite literal may also lead one sample or a parenthesized additive chain for every arithmetic operation; operand order is preserved for subtraction and division. RinGPU preflights every covered fragment, so a sampled zero divisor rejects the entire draw before its target changes. Strict IR verifies operand order and the actual bridge verifies both nonzero leading division and zero-divisor target preservation. Unparenthesized multi-call precedence and broader fragment expressions remain unsupported.
  - [x] Replace the fixed six-local parser ceiling with an eight-local candidate ceiling and capacity-aware RSH1 admission. RinGL counts local temporaries, every texture call and optional call-local offset, additive combines, stores, and any color operation before publishing IR; a safe eight-affine-local single-sample program is 45 instructions/40 registers, while eight locals plus eight call-local offsets is rejected before IR publication. Strict IR covers both outcomes; the actual two-varying RinGL→RinGPU→Aquamarine bridge carries the combined coordinate through seven additional local operations before sampling and color readback.
  - [x] Add a bounded three-`varying vec2` texture profile. Three sampler calls may select `firstUv`, `secondUv`, and `thirdUv` independently; the fragment RSH1 has six scalar inputs, and the third pair receives dedicated registers rather than aliasing either earlier pair. One `mixedUv = firstUv +/- secondUv`, `firstUv +/- thirdUv`, or `secondUv +/- thirdUv` local may feed subsequent direct/finite-affine local declarations in source order when the complete RSH1 shape fits its instruction/register budget; samples may still use every declared pair directly. Strict IR checks all three locations, a second/third-pair subtraction, and a following affine local/register chain. The RinGL→RinGPU→Aquamarine test executes a second/third-pair local then an affine offset that selects a distinct 2×2 texture texel while sampling the first pair directly, across direct and offset-`UNSIGNED_BYTE` indexed points, lines, line strips/loops, and triangle lists, strips, and fans; point/line/indexed-fan zero-divisor preflight leave the target unchanged, and every short line/strip/loop or strip/fan draw is a successful no-op.
  - [x] Add a bounded four-`varying vec2` texture profile. RinGL lowers four independently declared sampler coordinates into eight distinct fragment inputs and emits the matching 12-scalar vertex RSH1 output (`xyzw` plus four pairs). One named local may combine any two distinct four-UV pairs with `+` or `-` and feed the existing finite-affine chain; direct samples remain independently selectable. The private RinGPU/Aquamarine native route propagates the actual scalar count through clip, perspective interpolation, preflight, and submission for points, lines, and triangles without changing the public compact clip-vertex ABI. Strict IR verifies all four physical input pairs and a second/fourth-pair local without aliasing; focused bridge readback proves that all four sampled colors contribute through each native assembly.
  - [ ] Other nonconstant coordinate expressions, coordinates derived from more than four varyings or locals, broader four-UV expressions beyond one distinct-pair combine and its finite-affine chain, broader three-UV expressions beyond one distinct-pair combine followed by one remaining-pair `+/-` combine, other local vector expressions, more than eight locals, shapes exceeding the RSH1 capacity budget, different fragment expressions, more than eight calls, and broader varying-coordinate sampler paths remain unsupported.
- [ ] Expand varying support beyond the initial `vec2` perspective-interpolated profile.
  - [x] Implement the bounded `attribute vec2 position` + `attribute vec4 color` -> `varying vec4 vertexColor` -> `gl_FragColor = vertexColor` profile: parser/linker reflection, six scalar Float32 vertex inputs, four scalar perspective varyings, RSH1 lowering, and the RinGPU surface output path are covered by strict tests.
  - [x] Implement the bounded `attribute vec2 position` + `attribute vec3 color` -> `varying vec3 vertexColor` -> `gl_FragColor = vec4(vertexColor, 1.0)` profile: the RGB components use three perspective scalar varyings, while the fixed fourth RinGPU slot is explicitly produced as `1.0` and loaded by the fragment RSH1.
  - [x] Implement the bounded two-`vec2` profile: independent `vertexRG`/`vertexBA` declarations map to four distinct scalar perspective varyings and `gl_FragColor = vec4(vertexRG, vertexBA)`.
  - [x] Reuse the bounded two-`vec2` perspective interface for texture expressions: `texture2D(first, firstUv) + texture2D(second, secondUv)` assigns each sample to its declared varying pair and executes through RinGPU rather than treating the second coordinate as the first.
  - [x] Extend the RinGPU surface transport without changing its public compact clip-vertex V1 ABI: the private native route carries six finite scalar varyings through six-plane clipping, perspective interpolation, fragment preflight, and submission for the bounded three-`vec2` texture profile. Its matching 10-scalar vertex/6-scalar fragment RSH1 interface executes direct/indexed points, lines, line strips/loops, and triangle lists, strips, and fans; arbitrary scalar widths and general varying expressions remain unsupported on this V2 route.
  - [x] Extend that private native route from six to eight scalar varyings for the bounded direct four-`vec2` texture profile. A validated 12-output vertex/8-input fragment RSH1 pair carries four perspective coordinate pairs through the existing point, line, and triangle paths; mismatched shapes remain rejected before rasterization.
  - [ ] General multiple-varying combinations and expressions remain unsupported.

## Phase 4 — First hardware-rendered triangle

- [x] Create a default framebuffer representation backed by an embedding-supplied presentable RinGPU image.
- [x] Implement color clear through a RinGPU render-pass clear submission.
- [x] Build the first-slice pipeline-cache key from program, vertex layout, attachment format, and current immutable state.
- [x] Lazily create/cache RinGPU graphics pipelines with bounded eviction and lifetime cleanup.
- [x] Begin/end RinGPU render passes for the first clear and draw operations.
- [x] Implement first-slice `glDrawArrays(GL_TRIANGLES, ...)` translation.
- [x] Execute bounded `GL_POINTS` direct and indexed draws through a distinct
  native RinGPU point-list pipeline. The primitive topology participates in
  the RinGL pipeline-cache key, so a point draw cannot reuse a triangle
  pipeline.
- [x] Execute bounded `GL_LINES` direct and indexed draws through a distinct
  native RinGPU line-list pipeline. An incomplete pair is a successful no-op.
- [x] Execute bounded `GL_LINE_STRIP` direct and indexed draws through a
  distinct native RinGPU line-strip pipeline. Fewer than two vertices is a
  successful no-op.
- [x] Execute bounded `GL_LINE_LOOP` direct and indexed draws through a
  distinct native RinGPU line-loop pipeline. Fewer than two vertices is a
  successful no-op.
- [x] Execute bounded `GL_TRIANGLE_STRIP` direct and indexed draws through a
  distinct native RinGPU triangle-strip pipeline. Fewer than three vertices is
  a successful no-op.
- [x] Execute bounded `GL_TRIANGLE_FAN` direct and indexed draws through a
  distinct native RinGPU triangle-fan pipeline. Fewer than three vertices is
  a successful no-op.
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
- [x] Normalize level-zero `RGB`/`ALPHA`/`LUMINANCE`/`LUMINANCE_ALPHA` `UNSIGNED_BYTE` uploads into canonical RGBA8 storage, including default four-byte WebGL unpack-row alignment, and cover image/sub-image normalization through strict C11 tests.
  - [x] Track WebGL 1 `UNPACK_ALIGNMENT` (1/2/4/8) and apply it consistently
    to color, D32, and D32S8 `texImage2D`/`texSubImage2D` source rows before
    canonical shadow storage or RinGPU upload.
- [x] Implement robust CPU-side `texImage2D` zero initialization and bounded `texSubImage2D` updates.
  - [x] Add bounded `texImage2D`/`texSubImage2D` import entry points for
    untrusted byte spans. They validate the exact readable bytes under the
    current unpack alignment before allocating, replacing, or patching texture
    shadow storage; legacy raw-pointer entry points are documented as trusted
    native-only compatibility APIs.
- [x] Implement GLES-style texture filtering/wrap sampler state and invalidate realized samplers when it changes.
- [x] Implement sampler uniform locations and program-selected texture-unit state.
- [x] Lazily realize complete level-0 texture storage as CPU-visible RinGPU sampled RGBA8 images and upload the canonical shadow contents.
- [x] Lazily map texture filtering/wrap state to RinGPU sampler objects.
- [x] Add fake-RinGPU tests for image/sampler realization, cache hits, invalidation, and level-zero completeness.
- [x] Transition sampled images to `SHADER_READ`, create typed image/sampler bind groups, and bind them inside the render pass before draws.
- [x] Add a native-contract textured-draw mock test covering texture realization, resource transition, bind group creation, raster state, and draw ordering.
- [x] Add a varying-backed textured-triangle integration test with interleaved position/UV input and perspective-interpolated texture coordinates.
- [x] Execute level-zero color sampled-image/sampler bind groups through the RinOS RinGPU surface backend and resource-aware rasterizer; the integration test renders normalized RGB data with linear filtering and repeat/mirrored-repeat addressing. The bounded level-zero executor rejects non-1x1 textures whose min/mag filters differ because it has no derivative/LOD selection.

## Phase 6 — Framebuffers and fixed-function state

- [x] Implement framebuffer/renderbuffer object models (bounded lifecycle, binding, level-zero `COLOR_ATTACHMENT0` tracking, RGBA8 renderbuffer storage, and automatic detach on texture/renderbuffer deletion).
- [x] Expose a versioned bounded framebuffer-attachment query for `COLOR_ATTACHMENT0`, `DEPTH_ATTACHMENT`, `STENCIL_ATTACHMENT`, and `DEPTH_STENCIL_ATTACHMENT`. The query leaves caller output untouched on failure, reports a depth-stencil object only when both logical aspects share it, and supersedes the color-only compatibility shorthand for new embeddings.
- [x] Map supported RGBA8 color FBO texture/renderbuffer attachments to lazy RinGPU color targets, including image-state transitions, render-pass clear/draw targets, and `COPY_SOURCE` readback capability.
- [x] Implement framebuffer completeness validation for the supported single level-zero RGBA8 color-attachment combinations.
- [x] Implement viewport and scissor GL state, validation, defaults, and queries.
- [x] Implement face-culling/front-face GL state, validation, defaults, and queries.
- [x] Implement depth-test function/write-mask GL state, defaults, validation, dirty tracking, and queries.
- [x] Implement initial blend factor/equation and color-write-mask GL state, defaults, validation, dirty tracking, and queries.
- [x] Map supported blend, cull, front-face, and color-write state into native RinGPU graphics pipelines and include it in pipeline caching.
- [x] Execute the translated blend pipeline state in the RinGPU software
  surface backend. The descriptor path now accepts canonical disabled state
  and bounded enabled state; the executor consumes `ZERO`/`ONE`/source-alpha/
  destination-alpha factors, all five equations, and per-channel write masks.
  The focused RinGL-to-RinGPU-to-Aquamarine output test covers separate
  source-over, subtract/reverse-subtract/min/max, and masked channels.
- [x] Add the GLES/WebGL core `SRC_COLOR`/`ONE_MINUS_SRC_COLOR` and
  `DST_COLOR`/`ONE_MINUS_DST_COLOR` blend factors to RinGL state, native
  RinGPU pipeline translation, validation, and software execution. The
  focused bridge output test verifies component-wise source and destination
  factors rather than accidentally treating them as alpha factors.
- [x] Add `SRC_ALPHA_SATURATE` as a source-only factor: RGB uses
  `min(sourceAlpha, 1 - destinationAlpha)` and alpha uses one. RinGL and
  RinGPU reject it in destination slots; the focused bridge test covers the
  component result.
- [x] Add `CONSTANT_COLOR`/`ONE_MINUS_CONSTANT_COLOR` and
  `CONSTANT_ALPHA`/`ONE_MINUS_CONSTANT_ALPHA` with `ringl_blend_color()`.
  RinGL clamps finite values to `[0,1]`, keeps state unchanged on NaN/Inf,
  includes the RGBA value in the pipeline-cache key, and uses an additive V2
  native descriptor/callback only when a constant factor is active. V1
  bindings therefore cannot silently use a zero constant; a missing V2 path
  rejects the draw. The focused RinGL-to-RinGPU-to-Aquamarine test verifies
  both component constant color and scalar constant alpha output.
- [x] Expose a versioned snapshot of mutable clear values so an embedding can perform the WebGL default-buffer clear without overwriting application clear state.
- [x] Add a state-neutral default-framebuffer clear for trusted presentation
  embeddings: it forces WebGL's color/depth/stencil defaults without observing
  bound-FBO, scissor, or write-mask state, and reports its own failure without
  consuming the application's pending `glGetError()` value.
- [x] Map viewport/scissor state through dynamic RinGPU raster-state commands, including finite negative viewport origins and clipped scissor rectangles.
- [x] Add `depthRange` state with a versioned `RinGLDepthRangeV1` snapshot. Finite endpoints clamp independently to `[0,1]`, reversed ranges remain valid, NaN/Inf leave state unchanged, and draw translation carries the exact pair in native RinGPU raster state.
- [x] Carry `POLYGON_OFFSET_FILL` and finite `polygonOffset(factor, units)` state through RinGL's dynamic raster-state callback. RinGPU's V2 raster descriptor preserves the V1 disabled default, and the Aquamarine backend applies `m * factor + 2^-23 * units` only to filled-triangle depth before depth comparison/write; points and lines remain unchanged. Invalid non-finite inputs leave RinGL state unchanged. State and actual RinGL-to-RinGPU depth readback tests cover enabled and disabled behavior.
- [x] Execute finite `lineWidth` values in the inclusive `[1, 64]` range through RinGL's dynamic raster-state callback. RinGPU's V3 descriptor retains the V1/V2 one-pixel default, and Aquamarine rasterizes unique bounded coverage for line lists, strips, and loops with a deterministic half-open edge rule. Invalid values leave state unchanged. State and actual RinGL-to-RinGPU readback tests verify one- and two-pixel coverage.
- [x] Expose the current line width and the finite `[1, 64]` aliased range through the failure-atomic `RinGLLineWidthV1` snapshot, so browser embeddings can answer `LINE_WIDTH` and `ALIASED_LINE_WIDTH_RANGE` without guessing backend state.
- [x] Carry `SAMPLE_COVERAGE`, finite-clamped `sampleCoverage(value, invert)`, and a failure-atomic `RinGLSampleCoverageV1` snapshot through the dynamic raster-state callback. RinGPU's V4 suffix preserves V1--V3 full-coverage defaults, and the one-sample Aquamarine target executes the selected/inverted coverage bit before color/depth/stencil operations. State/core/surface and actual RinGL-to-RinGPU readback tests cover zero and inverted-zero coverage. `GENERATE_MIPMAP_HINT` with standard modes is an advisory no-op; derivative hints and multisample storage/resolve remain unsupported.
- [x] Expose linked active attribute and bounded-uniform reflection through failure-atomic `RinGLActiveInfoV1` records. The current profile reports float scalar/vector attributes, `sampler2D`, and direct `vec4` uniforms; unlinked programs and invalid indices leave caller storage unchanged. The `vec4` setter owns a distinct RSH1/RinGPU executable per linked program so values cannot leak through shared shader objects.
- [x] Preserve no-op semantics for zero-area viewport, all-channel color mask off, and `CULL_FACE` with `FRONT_AND_BACK` in the current color-only profile.
- [x] Map D32 clear and every GLES depth comparison (`NEVER`, `LESS`, `EQUAL`, `LEQUAL`, `GREATER`, `NOTEQUAL`, `GEQUAL`, `ALWAYS`) for an embedding-supplied default framebuffer through native RinGPU depth render passes.
- [x] Map D32 custom depth-renderbuffer FBO attachments with matching RGBA8 color attachments to lazy RinGPU images, depth render-pass clear/load, and all eight depth-tested draw predicates; strict C11 and the RinOS surface integration test cover completeness, dimension mismatch rejection, clear, and draw behavior.
- [x] Add the WebGL 1 `DEPTH_COMPONENT16` renderbuffer format. RinGL preserves its logical 16-bit internal format and `DEPTH_SIZE` query while realizing the matching FBO with the existing D32 RinGPU depth target; strict C11 FBO tests and the RinOS surface integration test cover completeness, clear, depth rejection, and depth-tested draw output.
- [x] Map a level-zero `DEPTH_COMPONENT32F`/`DEPTH_COMPONENT`/`FLOAT` texture to `DEPTH_ATTACHMENT`: lazy D32 RinGPU image creation/upload, framebuffer completeness, depth clear, and all depth-tested draws use the same target path as a D32 renderbuffer.
- [x] Execute a level-zero `DEPTH_COMPONENT32F` texture through the existing bounded `sampler2D`/`texture2D()` profile. RinGL realizes D32 with `SAMPLED` usage; the normal RinGPU sampled-image contract accepts D32 as well as color, and the Aquamarine executor snapshots each depth texel as deterministic `(depth, 0, 0, 1)`. The focused RinGL→RinGPU→Aquamarine bridge reads the actual 0.5 depth value as red 128; sampling an image attached to the active FBO is rejected before transition/render-pass submission.
- [x] Map a level-zero `DEPTH24_STENCIL8`/`DEPTH_STENCIL`/`UNSIGNED_INT_24_8` texture to `DEPTH_STENCIL_ATTACHMENT`: convert WebGL packed 24/8 input to RinGPU's F32/S8 storage, preserve it through lazy upload, and use the existing D32S8 clear, depth, and separate-face stencil paths.
- [x] Execute a level-zero `DEPTH24_STENCIL8` texture through the same bounded `sampler2D`/`texture2D()` profile. RinGL realizes D32S8 with `SAMPLED` usage, RinGPU permits it in the ordinary sampled-image state and binding contract, and Aquamarine snapshots only the native converted D32 depth plane as `(depth, 0, 0, 1)` without exposing stencil. The focused bridge uploads packed `0x8000005a` and reads its actual depth component as red 128; the shared color/depth attachment feedback-loop rejection runs before state transition or render-pass submission.
- [x] Execute `glClear` through the RinGPU render-pass ABI with WebGL lower-left scissor clipping, RGBA color write-mask preservation, depth-write-mask suppression, and the front stencil write mask. A requested depth/stencil clear with no matching attachment is a no-op. The core rejects noncanonical LOAD masks and out-of-bounds clear regions; the RinOS surface integration test checks direct BGRA and offscreen RGBA targets plus D32S8 storage.
- [ ] Implement stencil support after framebuffer/renderbuffer depth-stencil storage is in place.
  - [x] Add a bounded common-face `DEPTH24_STENCIL8` renderbuffer slice: lazy RinGPU D32S8 storage, clear, comparison/reference/read/write masks, and `KEEP`/`ZERO`/`REPLACE`/increment/decrement/invert operations execute before depth through the RinOS surface backend. An embedding-supplied default D32/S8 plane also realizes a direct D32S8 target. State/FBO unit tests and the RinGL-to-RinGPU integration test cover rejection, pass, write mask, and depth-fail behavior.
  - [x] Add bounded separate front/back stencil function, reference/read/write-mask, and operation state. RinGPU carries canonical common or independent face state into its native pipeline; the RinOS surface executor selects it from winding, honors front-face/cull state, and the integration test verifies distinct front/back writes plus back-face culling.
  - [x] Make `glClear(STENCIL_BUFFER_BIT)` use the front stencil write mask through a D32S8 RinGPU render pass. The core rejects noncanonical D32 or LOAD masks, while the surface executor preserves masked-off S8 bits; core-contract and integration tests cover both the descriptor and resulting pixels.
  - [x] Add bounded `STENCIL_ATTACHMENT` support for a matching D24S8 renderbuffer or level-zero D24S8 texture. RinGL tracks the logical depth and stencil aspects independently even though RinGPU executes the shared physical D32S8 image. Thus stencil clear/test execute, while a depth test, depth write, or depth clear cannot observe that image unless it was attached through `DEPTH_ATTACHMENT` or `DEPTH_STENCIL_ATTACHMENT`. Framebuffer unit tests cover completeness/detach; the actual RinGL→RinGPU→Aquamarine test keeps `DEPTH_TEST` at `NEVER` and still draws through a matching stencil value.
- [x] Realize WebGL 1 `STENCIL_INDEX8` renderbuffers as native RinGPU `S8_UINT` targets. RinGL reports the logical eight-bit stencil/no-depth format, accepts it only for `STENCIL_ATTACHMENT`, and disables depth comparison/write at the pipeline boundary because the native target has no depth plane. Strict C11 FBO tests and the actual RinGL→RinGPU→Aquamarine test cover query, completeness, stencil clear/test, an enabled `DEPTH_TEST = NEVER` remaining inert, and rejection of invalid `DEPTH_ATTACHMENT` use.
- [x] Keep `DEPTH_ATTACHMENT` and `STENCIL_ATTACHMENT` as independent custom-FBO slots and submit distinct D32/S8 renderbuffers through the versioned three-target RinGPU render pass. The combined attachment query returns an object only for an identical shared attachment; otherwise depth and stencil queries return their separate owners. The actual RinGL→RinGPU→Aquamarine test verifies completeness, queries, simultaneous depth/stencil draw, depth-fail stencil update, and RGBA readback.
  - [x] Permit either logical aspect of a D24S8 renderbuffer in a distinct three-target pass: the depth target may expose D32 or D24S8, and the stencil target may expose native S8 or D24S8. A D24S8 depth-only pass preserves its physical S8 plane with native LOAD/STORE while logical stencil stays detached. The offscreen surface stores D24S8 as checked F32-depth and byte-stencil planes rather than applying the eight-byte transfer pitch to depth. Strict FBO, RinGPU core-contract, actual RinGL→RinGPU→Aquamarine, and ASan/UBSan integration tests cover D24S8 depth-only and separate D32/D24S8 execution.
  - [x] Execute the complete bounded distinct-attachment matrix: D32/D24S8 depth renderbuffer or level-zero texture with native S8/D24S8 stencil renderbuffer or level-zero D24S8 texture. The actual RinGL→RinGPU→Aquamarine test verifies ownership, clear, depth-fail `REPLACE`, subsequent stencil match, and RGBA readback for every valid pair; ASan/UBSan covers the same path.
  - [ ] Add the remaining GLES/WebGL stencil attachment semantics.

## Phase 7 — Data movement, synchronization, and observability

- [x] Implement clear/copy paths that must end or split render passes.
  - [x] Add a bounded `copyTexSubImage2D` path from the current complete color
    target, including RGBA8 or packed RGB565/RGBA4/RGB5_A1 texture/renderbuffer
    FBOs, to a defined `RGBA`/`RGB`/`ALPHA`/`LUMINANCE`/`LUMINANCE_ALPHA` or
    native packed RGB565/RGBA4/RGB5_A1 texture level through fenced image
    readback. The copy validates FBO completeness and source/destination ranges
    before allocation, writes the destination only after the full
    default-BGRA/FBO-RGBA snapshot succeeds, preserves the canonical component
    expansion for RGB/alpha/luminance formats, and quantizes packed destination
    components directly. Explicit nonzero levels are supported; depth/stencil,
    multisample, and other unrepresented FBO copy semantics remain rejected.
  - [x] Add a bounded `copyTexImage2D` definition path from the same complete
    color targets. RGBA/RGB/ALPHA/LUMINANCE/LUMINANCE_ALPHA and native
    RGB565/RGBA4/RGB5_A1 storage is defined only after its canonical RGBA
    snapshot completes. Level zero replaces the base chain, while a nonzero
    definition requires a defined same-format base and exact mip dimensions.
    Canonical formats apply their component expansion and packed output
    quantizes directly into two-byte storage. The bound texture's old
    shadow/image remains intact for invalid source rectangles, incomplete FBOs,
    allocation failure, or readback failure. Zero-sized, depth/stencil, and
    multisample definitions remain unsupported.
- [x] Implement current immediate-submit `glFlush` semantics.
- [x] Implement `glFinish` using an optional versioned sync extension, RinGPU fences, fenced queue submission, and `ringpu_wait_fence()`.
- [x] Implement bounded current-color-framebuffer `RGBA/UNSIGNED_BYTE` `glReadPixels` logic through COPY_SOURCE transition, completion wait, RinGPU image readback, and BGRA-to-RGBA swizzle where required.
- [x] Add a fake-RinGPU synchronization/readback test covering monotonic fence values, waits, COPY_SOURCE transition, and pixel swizzle.
- [x] Enable readback on the RinOS WebGL surface color image by creating it with `COPY_SOURCE` usage and `CPU_READABLE` in addition to its existing present/color-target flags; the focused `rin_webgl_ringl_bridge_test` verifies clear, BGRA-to-RGBA readback, and present through the shared surface.
- [x] Convert the exact RinGPU `RINGL_RIN_GPU_ERROR_DEVICE_LOST` result from
  every v1 command callback and sync/readback callback into sticky context
  loss. The current context becomes unavailable to ordinary entry points,
  `ringl_get_error()` reports `CONTEXT_LOST_WEBGL` once, and later calls do not
  mutate RinGL state. Focused fake-backend tests cover both a command callback
  and readback loss; browser event/recovery wiring remains separate work.
- [x] Add broader tests for ordering across draws, copies, barriers, flushes, finish, and readbacks. The actual RinGL→RinGPU→Aquamarine test verifies draw/copy/readback/flush/finish ownership across source and copied FBOs, while the strict sync test records the complete command-list reset, image-transition barrier, render-pass, queue submit, fence wait, and readback event sequence for clear → copy → clear → flush → finish → readback.

## Phase 8 — OpenGL ES compatibility expansion

- [x] Inventory required OpenGL ES 2.0 entry points, enums, limits, and queries in `docs/gles2-api-status.md`; it audits every `gl2.h` entry point, all enum classes, fixed limits, and accepted query pnames against the public RinGL headers.
- [x] Track implementation status per API instead of claiming version support early. `docs/gles2-api-status.md` classifies every GLES 2.0 entry point as bounded, partial, or absent and defines the embedding rule that absent APIs must not synthesize success.
- [ ] Close GLES 2.0 semantic gaps found by conformance-style tests.
- [x] Implement WebGL generic attribute constants for disabled active arrays:
  active values use the default `(0, 0, 0, 1)` or `vertexAttrib[1-4]f` state as
  explicit Float32 RinGPU descriptors, while an embedding without the declared
  constant-input capability rejects the draw.
- [x] Implement independent vertex-buffer bindings. Active arrays are grouped
  by their captured `(buffer, effective stride)` into dense V2 bindings;
  direct and indexed draws use that V2 path only when the embedding advertises
  the capability and callbacks. Single-stream and all-constant layouts retain
  the V1 ABI. Layout and RinGPU/Aquamarine bridge tests cover distinct x/y
  buffers, a disabled generic value, and both draw forms.
- [x] Add a bounded 2D mip-chain path for unpacked RGBA8-normalized and native
  RGB565/RGBA4/RGB5_A1 color textures. `ringl_generate_mipmap()` creates a full
  deterministic clamped-2x2 chain atomically; packed levels average their
  stored 5/6/4-bit components before being repacked. Explicit nonzero-level
  `texImage2D`/`texSubImage2D` accept only an existing same-format base image
  with exact bounded mip dimensions. Generated and manual levels remain
  distinct: a base sub-image update drops only generated levels, while all
  contiguous levels are realized through the optional V2 RinGPU image/upload
  callbacks. The optional V5
  callback tail carries a nonzero color attachment's selected subresource
  through transition, render pass, direct/multi-buffer draw, and readback;
  V6 carries every color/depth/stencil mip selection for a depth pass. D32 and
  D24S8 exact-dimension manual levels use the same CPU-visible image chain;
  absent required callbacks yield `FRAMEBUFFER_UNSUPPORTED`, never level-zero
  output. The optional V7 bind-group tail carries the complete contiguous
  sampled chain only for mipmap minification filters, transitions every level
  to shader-read, and requires the backend to accept it rather than falling
  back to level zero. RinGPU/Aquamarine uses implicit fragment gradients plus
  sampler bias/min/max LOD for nearest/linear mip selection and independent
  min/mag texel filtering. Packed RGB565/RGBA4/RGB5_A1 levels use the same
  V5 color subresource callbacks for FBO clear, draw, and readback. Strict C11
  fake-backend and actual RinGPU bridge tests verify level dimensions, bytes,
  the native two-level descriptor, level-one color plus combined D24S8 FBO
  clear/depth-tested draw/readback, all three packed generated mip FBO
  clear/draw/readback paths, and distinct level-one sampled output from a
  three-level texture.
- [ ] Decide the boundary for OpenGL ES 3.x features.
- [ ] Add VAOs, instancing, additional texture formats, MRT, and other GLES 3.x features only after the underlying RinGPU contracts are ready.
  - [x] Add the WebGL 1 `OES_vertex_array_object` subset without admitting a
    GLES backend. RinGL owns a default VAO plus bounded named VAO descriptors
    for captured attribute-array state and `ELEMENT_ARRAY_BUFFER`; generic
    current attribute values remain context state as required by WebGL. Buffer
    deletion detaches matching descriptors in both active and inactive VAOs
    before object-name reuse. The focused C11 test covers switching, default
    restoration, current-value preservation, deletion, and stale-name
    rejection. Instancing and the remaining GLES 3.x VAO surface remain open.

## Phase 9 — WebGL-facing readiness

- [x] Keep WebGL validation/security policy outside the raw RinGL GL implementation.
  - [x] Raw RinGL accepts no WebGL-specific context flags and retains its
    complete native distinct depth/stencil profile. The Ladybird embedding
    queries the live raw attachments at its boundary, reports a distinct pair
    as `FRAMEBUFFER_UNSUPPORTED`, and rejects draw/read/copy/clear before
    native submission; an identical object and level remains valid.
- [x] Define a clean embedding API for browser contexts and surfaces.
  - The versioned RinGPU binding, default-framebuffer descriptor, and opt-in
    `ringl_aquamarine_surface` owner form the browser boundary. The embedding
    exposes only initial image-state setup, post-submit state synchronization,
    and a borrowed native view; clear/draw/readback/present remain RinGL
    commands. Explicit default-framebuffer depth/stencil aspect flags separate
    the WebGL contract from a shared physical D32/S8 allocation. Focused
    bridge tests prove full, depth-only, and stencil-only targets without a
    direct surface command path.
- [x] Ensure robust buffer/texture access independent of backend behavior.
  - Browser-facing buffer and texture imports now carry explicit source byte
    extents. `*_from_bytes` validates the whole source before allocating,
    replacing CPU shadow storage, or issuing a RinGPU callback; short buffers,
    rows, and subranges leave prior GL state intact. Ladybird uses only these
    bounded imports for `BufferSource`, typed-array texture, and converted
    image-source paths, while raw-pointer entry points are documented as
    trusted native-only compatibility APIs.
  - [x] Add a capacity-checked `readPixels` import/export boundary for
    browser-owned destinations. The bounded RGBA8 API rejects short spans
    before command submission, image-state transition, or destination writes;
    its raw-pointer predecessor is documented as trusted native-only.
- [ ] Ensure context loss can be propagated predictably to a browser implementation.
  - RinOS Ladybird WebGL 1 now translates sticky RinGL loss through command,
    present, and `isContextLost()` into a once-only `webglcontextlost` canvas
    event with the browser lost flag set before handler re-entry. A cancelled
    native loss queues only a fully initialized replacement RinGL context,
    invalidates old native object handles by generation, resets WebGL state,
    and then sends `webglcontextrestored`. `WEBGL_lose_context` destroys the
    same RinGL bridge/private surface but deliberately waits for explicit
    `restoreContext()` after the cancelled event; handler-time, uncancelled,
    native-loss, and duplicate restore requests produce `INVALID_OPERATION`.
    WebGL 2 and browser ISO/QEMU evidence remain unfinished, so this parent
    remains unchecked.
- [ ] Audit allocation limits and integer overflow paths for untrusted content.
- [ ] Add WebGL-oriented negative tests for malformed state and shader input.

## Phase 10 — Testing and conformance

- [ ] Add unit tests for every state transition and validation rule.
- [x] Add mock-RinGPU tests that inspect generated commands without requiring hardware for the first triangle path.
- [x] Add a native-contract mock test for textured resource/raster ordering.
- [x] Add RSH1 regression coverage for the initial varying-backed texture path.
- [x] Add mock synchronization/readback coverage for `finish`, complete-color-target `readPixels`, and bounded framebuffer-to-texture copy definitions.
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
