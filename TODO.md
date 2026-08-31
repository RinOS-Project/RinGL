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
- [x] Lower same-basic-type GLSL `vec2`/`vec3`/`vec4` and
  `ivec2`/`ivec3`/`ivec4` scalar constructors as RSH1 register splats. A
  single Float or i32 source register now initializes every target component;
  normal vector component lists remain exact-width and typed, and an explicit
  scalar `float(...)`/`int(...)` conversion remains the only Float/i32 bridge.
  The strict IR regression verifies Float and i32 splats plus emitted
  `I32_TO_F32`, rejects `vec2(int)` as a mixed-basic-type construction, and
  the real Ladybird→RinGL→RinGPU→private-Aquamarine regression reads back
  `(64, 64, 64, 255)` from both constructor paths. No browser or direct-surface
  vector expansion is introduced.
- [x] Lower the common Float GLSL expression builtins `min`, `max`, `clamp`,
  `mix`, and `dot` through RinGL rather than a browser-side calculation or a
  direct surface backend. `min`/`max`/`clamp` emit RinShader's scalar F32
  min/max opcodes, while `mix` and `dot` retain source-ordered scalar
  arithmetic. The lowerer accepts only matching Float scalar/vector overloads
  (with a scalar mix weight and scalar min/max/clamp bounds where GLSL permits
  them), rejects integer/matrix/mismatched-width forms before module
  publication, and the strict IR plus real RinGL→RinGPU→private-Aquamarine
  bridge regression verify both bytecode and `(116, 64, 143, 255)` readback.
- [x] Lower the bounded Float rounding/remainder and threshold/shaping suite
  `floor`, `ceil`, `fract`, `mod`, `abs`, `sign`, `step`, and `smoothstep`
  through RinGL and generic RinGPU. RSH1 opcode 59 executes finite binary32
  floor without a hosted libm or embedding callback; ceil/fract/mod use it
  with ordered arithmetic, while abs/sign/step/smoothstep use the existing
  scalar min/max/arithmetic/comparison operations. Only defined matching
  Float scalar/vector overloads are admitted. Integer, matrix, and
  mismatched-width calls are rejected before module publication; zero divisors
  and degenerate smoothstep edges fail backend preflight rather than changing a
  target. RinShader validation, strict RinGL IR, and the real
  Ladybird→RinGL→RinGPU→private-Aquamarine bridge verify opcode admission and
  the `(191, 128, 255, 40)` rounding/shaping readback.
- [x] Lower the bounded Float geometric suite `sqrt`, `inversesqrt`, `length`,
  `distance`, `normalize`, `cross`, `reflect`, `faceforward`, and `refract`
  through RinGL and generic RinGPU. RSH1 opcode 60 executes finite binary32
  square root without hosted libm or an embedding callback; all vector forms
  scalarize through existing arithmetic/comparison operations. `cross` accepts
  vec3 only; directional forms require matching vec2/vec3/vec4 operands, and
  `refract` uses scalar Float eta. Invalid type/width combinations are rejected
  before publication, while undefined zero normalize/inversesqrt cases fail
  preflight rather than receiving a fabricated result. Strict RinGL IR plus the
  Ladybird→RinGL→RinGPU→private-Aquamarine bridge execute the suite and verify
  `(255, 153, 204, 255)` and `(255, 153, 255, 255)` readbacks.
- [x] Lower the bounded Float trigonometric suite `radians`, `degrees`, `sin`,
  `cos`, `tan`, `asin`, `acos`, and one/two-argument `atan` through RinGL and
  generic RinGPU. Angle conversion uses scalar multiplication; RSH1 61--66
  execute the finite trigonometric operations without hosted libm or an
  embedding callback, while `tan` emits executable sin/cos division. Matching
  Float scalar/vector overloads are admitted, with matching widths required
  for `atan(y, x)`. Sin/cos/tan are bounded to `[-1024, 1024]` radians;
  asin/acos domains and atan(0,0) fail lowering or preflight instead of
  returning a fabricated result. Scientific Float literals are accepted only
  as finite binary32 values. Strict RinGL IR, RinShader validation, and the real
  Ladybird→RinGL→RinGPU→private-Aquamarine bridge verify the suite with
  `(128, 128, 255, 255)` readback.
- [x] Lower the bounded Float exponential suite `exp`, `log`, `exp2`, `log2`,
  and `pow` through RinGL and generic RinGPU. RSH1 67--69 execute finite
  binary32 base-two operations, while `exp`/`log` use scalar conversion
  factors and `pow` is a real binary backend operation. Matching Float
  scalar/vector overloads are admitted, with matching widths required for
  `pow`. `exp2` and the derived `exp`/`pow` result exponent are bounded to
  `[-126, 127]`; log and pow bases must be strictly positive. Domain/range
  failure is fail-closed before target publication. Strict RinGL IR, RinShader
  validation, and the real Ladybird→RinGL→RinGPU→private-Aquamarine bridge
  verify `exp(log(2))`, `log(exp(1))`, `exp2`, `log2`, and `pow` with
  `(128, 128, 255, 255)` readback.
- [x] Implement the bounded WebGL `OES_standard_derivatives` slice through
  RinGL rather than a browser-local or Aquamarine-direct backend. An acquired
  extension object enables a context-local gate; exact fragment
  `#extension GL_OES_standard_derivatives : enable`/`require` source lowers
  `dFdx`/`dFdy`/`fwidth` on finite float/vecN varying/arithmetic values to RSH1
  56/57/58. Parser/lower/state and real RinGPU/Aquamarine executor regressions
  cover it. Texture-sample derivatives, derivatives through the bounded
  conditional form, and fine/coarse variants
  remain explicitly unsupported and are rejected.
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
  - [x] Execute bounded vertex `uniform mat2 * vec2` and `uniform mat3 * vec3`
    through program-owned RSH1 modules, with active-uniform reflection,
    locations, typed getters, finite column-major `transpose == false` setters,
    and failure-atomic replacement. The generic profile has no varyings; the
    matching bounded matrix-array path permits constant-indexed `mat2`/`mat3`/
    `mat4` elements while preserving the existing transformed texture profile.
  - [x] Lower bounded vertex `matrixCompMult` for matching Float `mat2`, `mat3`,
    and `mat4` values into one scalar `MUL_F32` per column-major component. The
    generic no-varying path accepts scalar-diagonal, scalar/vector component,
    and same-dimension-copy constructors plus initialized local matrices and
    program-owned matrix uniforms; matching `matN * vecN` then executes the
    result on RinGPU. IR coverage includes all three dimensions and invalid
    mixed dimensions, while the real bridge covers uniform mat2/mat3 and local
    mat4 values. Matrix arrays, cross-dimension conversion, general
    matrix/matrix arithmetic, and the specialized varying/texture profile stay
    unsupported.
  - [x] Lower bounded scalar `if`/`else` whose two branches each assign the
    complete stage output. Matching Float or i32 scalar comparisons emit their
    real RSH1 comparison, an i32 zero test, and forward `JUMP_IF`/`JUMP`; the
    generic RinGPU executor runs those instructions. Missing `else`, vector or
    mixed-type conditions, local mutation, nested branches, partial outputs,
    and loops remain rejected. Strict IR covers Float/i32 branches and invalid
    forms; the Ladybird→RinGL→RinGPU bridge reads both red and blue targets.
  - [x] Lower standalone fragment `discard;` to RSH1 `DISCARD` and terminate
    the generic RinGPU fragment before output validation or depth/stencil/color
    publication. Strict IR covers output-less fragment discard and vertex
    rejection; the Ladybird→RinGL→RinGPU bridge keeps a cleared green target.
  - [x] Add bounded fragment conditional discard: one matching scalar
    comparison and mandatory `else` may have exactly one `discard;` branch
    when the other branch writes the complete `gl_FragColor` vector. RinGL
    emits its ordinary forward RSH1 `JUMP_IF`/`JUMP` and terminal `DISCARD`,
    so the generic RinGPU executor owns the decision and skips output,
    depth/stencil, and color publication only on the discarded path. Both
    discard branches, vertex discard, partial outputs, local mutation, nested
    branches, loops, and general control flow remain rejected. Strict IR
    covers both branch orders and rejection; the private-surface product test
    verifies discard preserves cleared green while the output path writes red.
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
  - [x] Lower bounded varying-coordinate texture color operations with finite `vec4` literals or one declared `uniform vec4`: `+`, `-`, `*`, or nonzero `/` after one sample, or after a one-through-eight sample additive chain only when explicitly parenthesized. A uniform operand may use a full-width, read-only `xyzw`/`rgba`/`stpq` selector chain; it may also lead a single sample, and RinGL permutes its four emitted constants rather than adding a vector instruction. RinGL emits four `CONST_F32` plus component-wise RSH1 operations after the live RinGPU samples/adds and rejects layouts beyond the RSH1 instruction/register ceilings or any literal zero division component. The finite literal may also lead one sample or a parenthesized additive chain for every arithmetic operation; operand order is preserved for subtraction and division. RinGPU preflights every covered fragment, so a sampled zero divisor rejects the entire draw before its target changes. Strict IR verifies operand order and the actual bridge verifies both nonzero leading division and zero-divisor target preservation. Unparenthesized multi-call precedence, partial-width uniform selectors, and broader fragment expressions remain unsupported.
  - [x] Replace the fixed six-local parser ceiling with an eight-local candidate ceiling and capacity-aware RSH1 admission. RinGL counts local temporaries, every texture call and optional call-local offset, additive combines, stores, and any color operation before publishing IR; a safe eight-affine-local single-sample program is 45 instructions/40 registers, while eight locals plus eight call-local offsets is rejected before IR publication. Strict IR covers both outcomes; the actual two-varying RinGL→RinGPU→Aquamarine bridge carries the combined coordinate through seven additional local operations before sampling and color readback.
  - [x] Accept full-width, read-only coordinate selector chains on a bounded `texture2D()` argument: `.xy`/`.yx`, `.rg`/`.gr`, and `.st`/`.ts` (including repeated/chained components) retain a `vec2` and are lowered as the exact two scalar RSH1 sample sources. Partial, mixed-family, and out-of-range selectors reject before lowering; no host-side coordinate rewrite occurs. Parser and strict IR tests prove the source-register permutation, and the actual RinGL→RinGPU→Aquamarine bridge reads the green `(0.75, 0.25)` texel through `uv.yx` rather than the original blue `(0.25, 0.75)` texel.
  - [x] Add a bounded three-`varying vec2` texture profile. Three sampler calls may select `firstUv`, `secondUv`, and `thirdUv` independently; the fragment RSH1 has six scalar inputs, and the third pair receives dedicated registers rather than aliasing either earlier pair. One `mixedUv = firstUv +/- secondUv`, `firstUv +/- thirdUv`, or `secondUv +/- thirdUv` local may feed subsequent direct/finite-affine local declarations in source order when the complete RSH1 shape fits its instruction/register budget; samples may still use every declared pair directly. Strict IR checks all three locations, a second/third-pair subtraction, and a following affine local/register chain. The RinGL→RinGPU→Aquamarine test executes a second/third-pair local then an affine offset that selects a distinct 2×2 texture texel while sampling the first pair directly, across direct and offset-`UNSIGNED_BYTE` indexed points, lines, line strips/loops, and triangle lists, strips, and fans; point/line/indexed-fan zero-divisor preflight leave the target unchanged, and every short line/strip/loop or strip/fan draw is a successful no-op.
  - [x] Add a bounded four-`varying vec2` texture profile. RinGL lowers four independently declared sampler coordinates into eight distinct fragment inputs and emits the matching 12-scalar vertex RSH1 output (`xyzw` plus four pairs). One named local may combine any two distinct four-UV pairs with `+` or `-` and feed the existing finite-affine chain; direct samples remain independently selectable. The private RinGPU/Aquamarine native route propagates the actual scalar count through clip, perspective interpolation, preflight, and submission for points, lines, and triangles without changing the public compact clip-vertex ABI. Strict IR verifies all four physical input pairs and a second/fourth-pair local without aliasing; focused bridge readback proves that all four sampled colors contribute through each native assembly.
  - [x] Extend direct `varying vec2` texture coordinates from four to the declared eight-pair/sixteen-scalar RinGL/RinGPU capacity. The transformed vertex path emits 14/16/18/20 outputs for five through eight pairs; the fragment lowerer receives distinct 10/12/14/16 scalar inputs and direct calls select every physical pair. Pipeline-cache ABI validation accepts the verified `vertex_outputs = fragment_inputs + 4` shape (or `+5` with `gl_PointSize`) instead of four hard-coded cases. Strict RSH1 tests inspect all eight input pairs/resources, and the native textured-draw test links an eight-pair program, uses `eighthUv`, creates all sixteen varying descriptors, binds a real image/sampler, and submits a draw.
  - [x] Accept one declared fragment `uniform vec2` as either operand of a direct sampler-coordinate `+`, `-`, `*`, or `/` operation, and as the right operand of bounded source-order local-coordinate `+`, `-`, `*`, or `/` operations. Direct sampler calls and the existing source-order local-coordinate chains materialize the finite pair as two RSH1 Float32 constants; its full-width read-only `xy`/`yx`, `rg`/`gr`, or `st`/`ts` selector emits the matching component permutation. Left `-` and `/` spellings emit `uniform - varying` and `uniform / varying` source order rather than silently reversing operands. `ringl_uniform_2f()` atomically publishes the replacement fragment module before the native RinGPU pipeline/draw. A zero default or later zero divisor component remains linkable but generic RinGPU preflight rejects the draw before target publication; zero literal divisors still reject during lowering. Strict IR verifies the zero-default module and both source orders; the native textured-draw test verifies updated constants and RSH1 `DIV`, while the actual RinGL→RinGPU→Aquamarine bridge samples blue only through `offset.yx / uv`. Uniform-led local initializers, partial/mixed uniform selectors, and general vector-coordinate expressions remain unsupported.
  - [x] Add generic fragment `sampler2D` lowering for accepted RinGL `vec2` expressions. Active samplers are compacted to actual adjacent RSH1 image/sampler resource pairs and reflected by source declaration index; every `texture2D()` emits four scalar `SAMPLE_IMAGE_2D_F32` instructions using the generic coordinate registers. When a historic coordinate shape matcher cannot admit a source, local/vector arithmetic, generic varyings, swizzles, and numeric uniform rebuilds stay in RinGL→RinGPU rather than a host-side sample path. Strict lowering tests inspect the inactive-declaration mapping and resource pair; the Ladybird-facing bridge executes `min(offset.yx / uv, vec2(...))` and reads the selected blue texel. The bounded legacy profiles retain their stable layouts for forms they already support.
  - [x] Execute GLSL ES 1 `texture2DProj(sampler2D, vec3|vec4)` through that same generic RinGL→RinGPU path. RinGL lowers homogeneous `xy / z` or `xy / w` into two live `DIV_F32` instructions before the four real `SAMPLE_IMAGE_2D_F32` operations; accepted locals, varyings, swizzles, arithmetic, and uniform divisors therefore never use a Ladybird/Aquamarine coordinate shortcut. Literal zero denominators reject during lowering, while dynamic zero values retain RinGPU's target-preserving preflight failure behavior. Parser/IR regression checks the live division and resource pair; the Ladybird-facing bridge uses `q = 0.5` to select the projected white 2×2 texel rather than the unprojected red texel.
  - [x] Execute GLSL ES `texture2D(sampler2D, vec2, float bias)` and `texture2DProj(sampler2D, vec3|vec4, float bias)` without converting either into an explicit-LOD shortcut. Each RGBA component carries a live Float bias register plus packed real image/sampler bindings in `SAMPLE_IMAGE_2D_BIAS_F32`; projective coordinates first retain their normal live homogeneous divisions. RinGPU preserves implicit derivative/anisotropic footprint selection, combines shader and sampler bias before min/max LOD clamping, and samples the actual mip chain. Verifier regression rejects malformed packed resources or bias registers. The Ladybird-facing bridge reads level-zero red at bias zero, atomically rebuilds the same projected fragment module for bias one, then reads level-one blue through the generic RinGL→RinGPU→private Aquamarine route.
  - [x] Execute bounded `uniform sampler2D name[N]` arrays (`1 <= N <= 8`, including all sampler elements) when every `texture2D(name[index], ...)` index is a decimal compile-time constant in range. RinGL expands linked locations as `name[0]` through `name[N-1]`, aliases `getUniformLocation("name")` to element zero, reports one active `name[0]` uniform of size `N`, and maps the selected element to a real RSH1 image/sampler pair. `ringl_uniform_1iv()` validates and atomically updates only one contiguous in-array range; dynamic/out-of-range indices reject before publication.
  - [x] Execute bounded scalar `uniform float name[N]` arrays (`1 <= N <= 8`) through the same generic RSH1/RinGPU artifact path. Reflection exposes one `name[0]` record of size `N`, the base name aliases its zero element, every GLSL read requires an in-range decimal constant index, and `ringl_uniform_1fv()` preflights finite values and one complete contiguous range before atomically publishing both stage replacements. Parser/lowerer tests reject dynamic indices; program tests verify reflection, aliases, range rollback, and non-finite rejection.
  - [x] Execute the bounded `EXT_shader_texture_lod` slice through the same RinGL→RinGPU path. After the extension object enables the context and the fragment source declares `GL_EXT_shader_texture_lod`, `texture2DLodEXT(sampler2D, accepted vec2, accepted scalar float)` and `texture2DProjLodEXT(sampler2D, accepted vec3|vec4, accepted scalar float)` emit four `SAMPLE_IMAGE_2D_LOD_F32` component instructions. Projective forms first preserve their live homogeneous divisions. `texture2DGradEXT` and `texture2DProjGradEXT` use accepted `vec2` dPdx/dPdy expressions, materialize dU/dX, dU/dY, dV/dX, dV/dY as four consecutive live Float32 registers, and emit four `SAMPLE_IMAGE_2D_GRAD_F32` instructions that the generic backend executes as an explicit real mip footprint. Packed resources and all live parameter registers are verifier-checked. The backend applies sampler min/max LOD clamps without an Aquamarine/GLES fallback. Uniform and bounded local LOD/gradient expressions work through normal atomic module rebuilds; cube/3D/array/shadow forms remain outside this bounded slice.
  - [x] Execute bounded constant-indexed numeric `uniform name[N]` arrays through the same RinGL→RinGPU module path: scalar `int`/`bool` and `vec`/`ivec`/`bvec` have at most eight elements per type, while vertex `mat2`/`mat3`/`mat4` have at most four. Reflection coalesces each declaration as `name[0]` of size `N`; base-name location aliases, complete span setters, Boolean normalization, 64-register matrix-array storage, and replacement rollback are covered by parser/program regressions.
  - [ ] GLSL ES texture semantics outside the generic accepted expression grammar remain unsupported: dynamic uniform indexing, cube/3D/array/shadow forms, loops/functions, storage side effects, unsupported control flow/types, and modules exceeding the 128-instruction/96-register RSH1 ceiling fail before publication. This does not claim complete GLSL ES/WebGL texture conformance.
- [ ] Expand varying support beyond the initial `vec2` perspective-interpolated profile.
  - [x] Implement the bounded `attribute vec2 position` + `attribute vec4 color` -> `varying vec4 vertexColor` -> `gl_FragColor = vertexColor` profile: parser/linker reflection, six scalar Float32 vertex inputs, four scalar perspective varyings, RSH1 lowering, and the RinGPU surface output path are covered by strict tests. The fragment value may now use a full-width, read-only `xyzw`/`rgba`/`stpq` selector chain (for example `vertexColor.stpq.bgra`); RinGL composes it into scalar RSH1 source registers for direct color output and the bounded `texture2D(colorTexture, uv) * vertexColor * tint` material, including the optional `uniform vec4` tint, and actual RinGL→RinGPU→Aquamarine draws read back the reordered colors.
  - [x] Implement the bounded `attribute vec2 position` + `attribute vec3 color` -> `varying vec3 vertexColor` -> `gl_FragColor = vec4(vertexColor, 1.0)` profile: the RGB components use three perspective scalar varyings, while the fixed fourth RinGPU slot is explicitly produced as `1.0` and loaded by the fragment RSH1. A full-width read-only selector chain such as `vertexColor.bgr` reorders only the real RGB slots; alpha remains the explicit `1.0` compatibility value.
  - [x] Implement the bounded two-`vec2` profile: independent `vertexRG`/`vertexBA` declarations map to four distinct scalar perspective varyings and `gl_FragColor = vec4(vertexRG, vertexBA)`.
  - [x] Reuse the bounded two-`vec2` perspective interface for texture expressions: `texture2D(first, firstUv) + texture2D(second, secondUv)` assigns each sample to its declared varying pair and executes through RinGPU rather than treating the second coordinate as the first.
  - [x] Extend the RinGPU surface transport without changing its public compact clip-vertex V1 ABI: the private native route carries six finite scalar varyings through six-plane clipping, perspective interpolation, fragment preflight, and submission for the bounded three-`vec2` texture profile. Its matching 10-scalar vertex/6-scalar fragment RSH1 interface executes direct/indexed points, lines, line strips/loops, and triangle lists, strips, and fans; arbitrary scalar widths and general varying expressions remain unsupported on this V2 route.
  - [x] Extend that private native route from six to eight scalar varyings for the bounded direct four-`vec2` texture profile. A validated 12-output vertex/8-input fragment RSH1 pair carries four perspective coordinate pairs through the existing point, line, and triangle paths; mismatched shapes remain rejected before rasterization.
  - [ ] General multiple-varying combinations and expressions remain unsupported.
    The generic assignment path now links matching `varying float`, `vec2`,
    `vec3`, and `vec4` declarations up to RinGPU's real 28-scalar
    perspective-interpolant budget (the other four of 32 scalar outputs are
    clip `xyzw`). Whole-vector assignments and non-overlapping writable
    `xyzw`/`rgba`/`stpq` selectors store exact scalar outputs; a component map
    requires every declared slot before link publication. The fragment lowerer
    materializes every declared input, including unused declarations, so the
    native linker validates a complete typed interface. Missing components fail
    linking, while repeated, mixed-family, or out-of-range writable selectors
    fail compilation. The product bridge executes seven `vec4` varyings and
    reads the seventh color back; it also executes a `bgr` component write,
    `bgra` fragment read, and explicit alpha write. An eighth varying is
    rejected during link rather than truncating its 32nd component. General
    The fixed `gl_Position` and `gl_FragColor` outputs likewise accept
    non-overlapping writable `xyzw`/`rgba`/`stpq` selectors and emit the exact
    RSH1 output slots in source order; once either is selector-written, all four
    clip/color components are required during RinGL→RinGPU module publication.
    The public bridge readback covers swapped position `xy`, explicit `zw`,
    reordered fragment `bgr`, and alpha stores; an incomplete `rgb` output
    fails link instead of receiving a fabricated component. General varying
    expressions, generic texture use, and broader linkage semantics remain
    unsupported.
  - [x] Extend enabled `GL_EXT_draw_buffers` literal `gl_FragData[0..3]` writes
    with non-overlapping writable `xyzw`/`rgba`/`stpq` selectors. Each selector
    emits the selected attachment's real scalar RSH1 stores in source order;
    existing MRT finalization supplies explicit zero stores for components the
    source omitted. Parser/lowerer regressions reject repeated selectors, and
    the public bridge renders red, green, blue, and yellow through reordered
    component selectors into four actual attachments. This does not extend
    dynamic indices, WebGL 2, or broader GLSL expressions.

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
- [x] Execute vertex `gl_PointSize` for the generic no-varying profile and all
  existing structural varying profiles with a finite literal, `uniform float`,
  a dedicated `attribute float` point-size input, or profile color-attribute
  component assignment optionally followed by one finite right-hand arithmetic
  literal. RinGL reserves the scalar
  immediately after clip `xyzw`,
  shifts user varying outputs above it, advertises the executable `[1, 64]`
  range, and the generic RinGPU backend finite-checks then clamps it before
  square point coverage. Updating an active scalar float atomically rebuilds
  the program-owned vertex module; a scalar input or color attribute component
  is stored from its vertex RSH1 register with a real RSH1 arithmetic opcode.
  Strict IR and the real
  RinGL→generic-RinGPU→caller-owned-Aquamarine-storage bridge test verify a
  `varying vec4` colored point changing from three-pixel to one-pixel coverage
  through uniform, scalar-attribute, and color-attribute values; broader varying point-size
  expressions remain unsupported.
- [x] Execute fragment `gl_PointCoord` as RSH1 Float32 builtin X/Y values for
  native point-list fragments. RinGL supports normal vector use and bounded
  `texture2D(sampler uniform, gl_PointCoord)` lowering without fabricating a user
  varying; generic RinGPU supplies the GLES/WebGL immutable upper-left
  coordinate from the unclipped point center. IR and caller-owned Aquamarine
  product tests verify the builtin bytecode and 3×3 point gradient. Non-point
  execution is rejected rather than receiving invented coordinates.
- [x] Execute fragment `gl_FragCoord` as four RSH1 Float32 builtins over the
  generic RinGPU triangle, line, and point raster paths. The backend supplies
  lower-left window `x/y`, pre-polygon-offset window `z`, and interpolated
  reciprocal clip `w`, together with finite lower-left `dFdx`/`dFdy` inputs;
  it never aliases a user varying or a top-down storage coordinate. The
  caller-owned WebGL bridge regression verifies all four components from an
  actual clip-W-two triangle and the exact bounded
  `texture2D(sampler, gl_FragCoord.xy / vec2(finite, finite))` profile. The
  lowerer emits builtin X/Y loads plus real RSH1 divisions before sampling;
  other texture-coordinate special forms remain outside the bounded profile.
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
- [x] Add bounded WebGL 1 `OES_texture_half_float` sampled textures: exact
  `HALF_FLOAT_OES` binary16 uploads are decoded with unaligned-safe reads into
  the existing RGBA32F shadow/RinGPU image; short source spans leave old
  storage unchanged. A separate context-local linear-filter gate keeps the
  base extension nearest-only. This intentionally does not mark half-float
  FBOs complete: `EXT_color_buffer_half_float` RGBA16F output, renderbuffer,
  attachment-query, and readback precision semantics remain a later item.
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
- [x] Realize level-zero `RGBA`/`RGB`/`ALPHA`/`LUMINANCE`/`LUMINANCE_ALPHA`
  `FLOAT` textures as complete native RGBA32F color FBOs after the Float-color
  gate. RinGL keeps the legacy logical channels over physical RGBA storage:
  RGB alpha is one, ALPHA RGB is zero, LUMINANCE is replicated across RGB, and
  LUMINANCE_ALPHA keeps alpha. Clear, fragment output, readback, CopyTex, and
  partial logical color masks use the same conversion without a direct
  Aquamarine command path; the product bridge regression executes all five
  formats plus independent LUMINANCE_ALPHA alpha masking.
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
  RinGL normally clamps finite values to `[0,1]` and keeps state unchanged on
  NaN/Inf. After the private `WEBGL_color_buffer_float` gate, it retains finite
  out-of-range components only in an RGBA32F pipeline key and clamps them again
  for fixed-point targets. It uses an additive V2 native descriptor/callback
  only when a constant factor is active. V1 bindings therefore cannot silently
  use a zero constant; a missing V2 path rejects the draw. The focused
  RinGL-to-RinGPU-to-Aquamarine test verifies component constant color, scalar
  constant alpha, and unclamped Float-target output.
- [x] Gate `EXT_blend_minmax` behind the acquired WebGL extension object. The
  context rejects `MIN`/`MAX` before `ringl_enable_webgl_blend_minmax()` and
  preserves prior blend state; after the gate, the existing native RinGPU V2
  pipeline executes minimum/maximum blending through Aquamarine. This is an
  extension capability, not a direct-surface bypass.
- [x] Expose a versioned snapshot of mutable clear values so an embedding can perform the WebGL default-buffer clear without overwriting application clear state.
- [x] Add a state-neutral default-framebuffer clear for trusted presentation
  embeddings: it forces WebGL's color/depth/stencil defaults without observing
  bound-FBO, scissor, or write-mask state, and reports its own failure without
  consuming the application's pending `glGetError()` value.
- [x] Report the actual default drawing-buffer `RED_BITS`/`GREEN_BITS`/
  `BLUE_BITS`/`ALPHA_BITS` and logical `DEPTH_BITS`/`STENCIL_BITS` through the
  bounded integer-query API. RinGL derives component widths from the configured
  native color format and depth/stencil visibility from the explicit-aspect
  contract that controls draw/clear behavior; absent logical planes report zero
  rather than being inferred from private storage.
- [x] Map viewport/scissor state through dynamic RinGPU raster-state commands, including finite negative viewport origins and clipped scissor rectangles.
- [x] Add `depthRange` state with a versioned `RinGLDepthRangeV1` snapshot. Finite endpoints clamp independently to `[0,1]`, reversed ranges remain valid, NaN/Inf leave state unchanged, and draw translation carries the exact pair in native RinGPU raster state.
- [x] Carry `POLYGON_OFFSET_FILL` and finite `polygonOffset(factor, units)` state through RinGL's dynamic raster-state callback. RinGPU's V2 raster descriptor preserves the V1 disabled default, and the Aquamarine backend applies `m * factor + 2^-23 * units` only to filled-triangle depth before depth comparison/write; points and lines remain unchanged. Invalid non-finite inputs leave RinGL state unchanged. State and actual RinGL-to-RinGPU depth readback tests cover enabled and disabled behavior.
- [x] Execute finite `lineWidth` values in the inclusive `[1, 64]` range through RinGL's dynamic raster-state callback. RinGPU's V3 descriptor retains the V1/V2 one-pixel default, and Aquamarine rasterizes unique bounded coverage for line lists, strips, and loops with a deterministic half-open edge rule. Invalid values leave state unchanged. State and actual RinGL-to-RinGPU readback tests verify one- and two-pixel coverage.
- [x] Expose the current line width and the finite `[1, 64]` aliased range through the failure-atomic `RinGLLineWidthV1` snapshot, so browser embeddings can answer `LINE_WIDTH` and `ALIASED_LINE_WIDTH_RANGE` without guessing backend state.
- [x] Carry `SAMPLE_COVERAGE`, finite-clamped `sampleCoverage(value, invert)`, and a failure-atomic `RinGLSampleCoverageV1` snapshot through the dynamic raster-state callback. RinGPU's V4 suffix preserves V1--V3 full-coverage defaults, and the one-sample Aquamarine target executes the selected/inverted coverage bit before color/depth/stencil operations. State/core/surface and actual RinGL-to-RinGPU readback tests cover zero and inverted-zero coverage. `GENERATE_MIPMAP_HINT` with standard modes is an advisory no-op; derivative hints and multisample storage/resolve remain unsupported.
- [x] Expose linked active attribute and bounded-uniform reflection through failure-atomic `RinGLActiveInfoV1` records. The current profile reports float scalar/vector attributes, `sampler2D`, direct `vec4` uniforms, and bounded vertex `mat2`/`mat3`/`mat4` uniforms; unlinked programs and invalid indices leave caller storage unchanged. Numeric setters own distinct RSH1/RinGPU executables per linked program so values cannot leak through shared shader objects.
- [x] Preserve no-op semantics for zero-area viewport, all-channel color mask off, and `CULL_FACE` with `FRONT_AND_BACK` in the current color-only profile.
- [x] Map D32 clear and every GLES depth comparison (`NEVER`, `LESS`, `EQUAL`, `LEQUAL`, `GREATER`, `NOTEQUAL`, `GEQUAL`, `ALWAYS`) for an embedding-supplied default framebuffer through native RinGPU depth render passes.
- [x] Map D32 custom depth-renderbuffer FBO attachments with matching RGBA8 color attachments to lazy RinGPU images, depth render-pass clear/load, and all eight depth-tested draw predicates; strict C11 and the RinOS surface integration test cover completeness, dimension mismatch rejection, clear, and draw behavior.
- [x] Add the WebGL 1 `DEPTH_COMPONENT16` renderbuffer format. RinGL preserves its logical 16-bit internal format and `DEPTH_SIZE` query while realizing the matching FBO with the existing D32 RinGPU depth target; strict C11 FBO tests and the RinOS surface integration test cover completeness, clear, depth rejection, and depth-tested draw output.
- [x] Map a level-zero `DEPTH_COMPONENT32F`/`DEPTH_COMPONENT`/`FLOAT` texture to `DEPTH_ATTACHMENT`: lazy D32 RinGPU image creation/upload, framebuffer completeness, depth clear, and all depth-tested draws use the same target path as a D32 renderbuffer.
- [x] Execute a level-zero `DEPTH_COMPONENT32F` texture through the existing bounded `sampler2D`/`texture2D()` profile. RinGL realizes D32 with `SAMPLED` usage; the normal RinGPU sampled-image contract accepts D32 as well as color, and the Aquamarine executor snapshots each depth texel as deterministic `(depth, 0, 0, 1)`. The focused RinGL→RinGPU→Aquamarine bridge reads the actual 0.5 depth value as red 128; sampling an image attached to the active FBO is rejected before transition/render-pass submission.
- [x] Map a level-zero `DEPTH24_STENCIL8`/`DEPTH_STENCIL`/`UNSIGNED_INT_24_8` texture to `DEPTH_STENCIL_ATTACHMENT`: convert WebGL packed 24/8 input to RinGPU's F32/S8 storage, preserve it through lazy upload, and use the existing D32S8 clear, depth, and separate-face stencil paths.
- [x] Execute a level-zero `DEPTH24_STENCIL8` texture through the same bounded `sampler2D`/`texture2D()` profile. RinGL realizes D32S8 with `SAMPLED` usage, RinGPU permits it in the ordinary sampled-image state and binding contract, and Aquamarine snapshots only the native converted D32 depth plane as `(depth, 0, 0, 1)` without exposing stencil. The focused bridge uploads packed `0x8000005a` and reads its actual depth component as red 128; the shared color/depth attachment feedback-loop rejection runs before state transition or render-pass submission.
- [x] Execute `glClear` through the RinGPU render-pass ABI with WebGL lower-left scissor clipping, RGBA color write-mask preservation, depth-write-mask suppression, and the front stencil write mask. A requested depth/stencil clear with no matching attachment is a no-op. The core rejects noncanonical LOAD masks and out-of-bounds clear regions; the RinOS surface integration test checks direct BGRA and offscreen RGBA targets plus D32S8 storage.
- [ ] Complete the remaining GLES/WebGL stencil attachment semantics. The bounded core already owns common and separate front/back state, default and custom D32/S8/S8 targets, logical depth/stencil aspect separation, clear masks, and native draw execution; the open work is limited to the broader semantics listed below.
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
    target, including RGBA8/packed RGB565/RGBA4/RGB5_A1 and native
    RGBA32F/RGBA16F texture/renderbuffer FBOs, to a defined
    `RGBA`/`RGB`/`ALPHA`/`LUMINANCE`/`LUMINANCE_ALPHA`, Float32, binary16, or
    native packed RGB565/RGBA4/RGB5_A1 texture level through fenced image
    readback. The copy validates FBO completeness and source/destination ranges
    before allocation, validates a complete Float snapshot as finite, writes the
    destination only after snapshot completion, preserves canonical component
    expansion for RGB/alpha/luminance formats, retains finite Float values,
    saturates finite binary16 conversion, and quantizes fixed-point/packed
    destinations directly. Explicit nonzero levels are supported; depth/stencil,
    multisample, and other unrepresented FBO copy semantics remain rejected.
  - [x] Add a bounded `copyTexImage2D` definition path from the same complete
    color targets. RGBA/RGB/ALPHA/LUMINANCE/LUMINANCE_ALPHA, Float32, binary16,
    and native RGB565/RGBA4/RGB5_A1 storage is defined only after its canonical
    RGBA snapshot completes. Level zero replaces the base chain, while a
    nonzero definition requires a defined same-format base and exact mip
    dimensions. Canonical formats apply their component expansion, Float32
    retains finite components, binary16 follows finite saturating conversion,
    and packed output quantizes directly into two-byte storage. The bound
    texture's old shadow/image remains intact for invalid source rectangles,
    incomplete FBOs, allocation failure, non-finite Float source, or readback
    failure. Zero-sized, depth/stencil, and multisample definitions remain
    unsupported.
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
- [x] Expose WebGL's `drawingBufferFormat` from the effective context attributes: an alpha-capable buffer reports `RGBA8`, while an opaque buffer reports `RGB8`. The value describes the WebGL buffer rather than the embedding's physical channel order, so RinGL's private BGRA presentation storage remains unobservable. `drawingBufferColorSpace`, `unpackColorSpace`, drawing-buffer reallocation, and non-sRGB display remain separate unsupported work.
- [ ] Close GLES 2.0 semantic gaps found by conformance-style tests.
  - [x] Add bounded typed `glGetBooleanv`/`glGetFloatv` adapters for every
    accepted RinGL query and tracked floating state. Capability, integer,
    vector, clear, blend, depth-range, line-width, polygon-offset, and
    sample-coverage values convert only after complete span validation; null or
    short output records an error without mutation. Legacy unbounded forms
    delegate to the bounded implementation. Broader conformance semantics and
    unsupported query pnames remain open.
  - [x] Add bounded typed texture-parameter and vertex-attribute query
    adapters. Filter/wrap enum pnames, extension-gated anisotropy (integer and
    Float32), descriptor
    fields, and the four-component current generic attribute value now require
    a complete caller span and publish only after validation. Attribute pointer
    queries remain unavailable because returning raw host pointers would break
    the embedding ABI; unsupported pnames continue to report an error.
  - [x] Add a bounded `glGetBufferParameteriv` adapter for the represented
    `BUFFER_SIZE` and `BUFFER_USAGE` pnames. It validates target, binding,
    pname, and the complete one-element output span before publishing a value;
    an oversized future buffer profile is rejected instead of truncating to
    `GLint`. Short/null outputs and unbound buffers leave the destination
    unchanged. Broader buffer-query pnames and conformance semantics remain
    open.
  - [x] Add a bounded `glGetFramebufferAttachmentParameteriv` adapter for the
    represented custom-FBO attachments. Object type/name, texture level, the
    2D-only cube-face sentinel, and logical `EXT_sRGB` color encoding require
    a complete one-element output span and publish only after the existing
    versioned attachment record validates. Depth/stencil color-encoding
    queries, unknown pnames, and unavailable default-FBO semantics remain
    rejected rather than synthesized.
  - [x] Add a bounded `glGetRenderbufferParameteriv` adapter for represented
    renderbuffer metadata. Width/height, internal format, component bit sizes,
    and the zero-sample profile are returned only after complete one-element
    span, target, binding, and pname validation. Future values that cannot fit
    in `GLint` leave the caller unchanged; multisample storage remains absent.
  - [x] Add bounded `glGetShaderiv`/`glGetProgramiv` adapters for the status,
    count, log/source-length, and active-name maximum pnames represented by
    RinGL. Compile/type, link/validate, attached shader, active
    attribute/uniform values, and bounded lengths use complete one-element
    spans and existing versioned records. Shader `DELETE_STATUS` now reports
    the pending bit while an attached shader remains retained; program
    delete-pending lifetime and other unrepresented pnames remain unavailable
    rather than returning fabricated values.
  - [x] Preserve attached-shader query lifetime for `DELETE_STATUS`: a shader
    marked by `glDeleteShader` remains queryable until its final program
    release, then the numeric handle is reclaimed. `shader_test.c` covers
    live false, retained pending true, and post-release invalidation without
    exposing a stale object.
- [x] Expose the executable RSH1 shader precision profile through a versioned
  `ringl_get_shader_precision_format()` query. All accepted float precision
  classes report IEEE-754 binary32; accepted integer classes report the signed
  i32 GLES range. Invalid stage/token/header inputs leave the caller record
  unchanged, and Ladybird's WebGL `getShaderPrecisionFormat()` now consumes
  this RinGL result rather than duplicating precision constants.
- [x] Expose the actual bounded-profile identity through
  `ringl_get_string(VENDOR|RENDERER|VERSION|SHADING_LANGUAGE_VERSION)`.
  The static strings identify RinGL/RSH1 without claiming a host driver or
  full GLES conformance; unknown names fail with `INVALID_ENUM`, and Ladybird
  forwards only these RinGL-owned values for WebGL `getParameter()`.
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
- [x] Decide the boundary for OpenGL ES 3.x features. `docs/gles3-boundary.md`
  fixes RinGL as a bounded GLES 2.0-shaped raw API with no GLES 3 core or
  WebGL 2 advertisement; OES/ANGLE/WEBGL extension slices remain independent
  gates and do not imply core-version support.
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
  - [x] Execute `WEBGL_compressed_texture_etc1`,
    `WEBGL_compressed_texture_s3tc`, and
    `WEBGL_compressed_texture_s3tc_srgb` through dedicated bounded upload
    APIs: ETC1 plus linear/sRGB RGB/RGBA DXT1, RGBA DXT3, and RGBA DXT5 exact
    block spans decode before the ordinary RinGL/RinGPU texture path. sRGB RGB
    channels become deterministic linear Float32 values while alpha remains
    linear; a decoded image remains logically compressed, so uncompressed
    mutation, generated mipmaps, and color-FBO rendering are rejected. Short,
    misaligned, out-of-range, or failed decode input leaves the existing
    texture unchanged. Focused texture/framebuffer tests and the
    RinGL→RinGPU→private-Aquamarine bridge verify real upload, linear sRGB
    sample output, alpha, and readback; other compressed formats remain
    unimplemented.
  - [x] Execute the WebGL 1 `EXT_sRGB` slice through the browser's RinGL route:
    `SRGB_EXT`/`SRGB_ALPHA_EXT` exact unsigned-byte texture input and
    `SRGB8_ALPHA8_EXT` renderbuffer storage retain logical sRGB metadata while
    using linear RGBA32F RinGPU storage. RGB is decoded on input and re-encoded
    for byte readback; `SRGB_ALPHA_EXT` alpha stays linear while alpha-less
    `SRGB_EXT` keeps alpha one; `generateMipmap` rejects logical
    sRGB textures. Focused sRGB, texture, framebuffer, and sync tests cover
    token scope, FBO metadata, physical storage, and transfer round-trip.
- [ ] Ensure context loss can be propagated predictably to a browser implementation.
  - RinOS Ladybird WebGL 1 now translates sticky RinGL loss through command,
    present, and `isContextLost()` into a once-only `webglcontextlost` canvas
    event with the browser lost flag set before handler re-entry. A cancelled
    native loss queues only a fully initialized replacement RinGL context;
    logical `0×N`/`N×0` canvases use the same private 1×1 physical target
    normalization as initial creation, so recovery does not turn a valid
    zero-sized canvas into a permanently lost context. The replacement
    invalidates old native object handles by generation, resets WebGL state,
    and then sends `webglcontextrestored`. `WEBGL_lose_context` destroys the
    same RinGL bridge/private surface but deliberately waits for explicit
    `restoreContext()` after the cancelled event; handler-time, uncancelled,
    native-loss, and duplicate restore requests produce `INVALID_OPERATION`.
    WebGL 2 and browser ISO/QEMU evidence remain unfinished, so this parent
    remains unchecked.
- [ ] Audit allocation limits and integer overflow paths for untrusted content.
  - [x] Bound per-context CPU shadow bytes for browser-facing buffer data and
    transactional sub-data replacement at 512 MiB. Oversized requests are
    rejected before allocation/backend submission, and delete or failed
    replacement releases the reservation. `tests/buffer_test.c` covers the
    pre-allocation rejection and unchanged zero-sized buffer. Texture,
    shader/program, temporary decode, and hardware/QEMU resource accounting
    remain open.
  - [x] Bound persistent texture and generated-mipmap CPU shadows through the
    same per-context 512 MiB budget. Base-level, explicit-mip, copy-image, and
    mipmap generation paths reserve before allocation and release on rollback,
    replacement, generated-level discard, delete, and context teardown.
    `tests/texture_test.c` fills the budget without allocating it, verifies an
    upload is rejected before allocation, and confirms the reservation can be
    released. Shader/program, temporary decode, and hardware/QEMU accounting
    remain open.
  - [x] Bound shader source snapshots, lowered RSH1 shader modules, and
    program-owned uniform RSH1 modules to the same per-context 512 MiB budget.
    Source length scanning is bounded, replacement is failure-atomic, and
    compile/lower failure, relink replacement, delete, and context teardown
    release the exact owned byte count. `tests/shader_test.c` and the root
    WebGL negative test verify source replacement is rejected before allocation
    under a full reservation. Temporary parser/lowerer workspace, shader
    diagnostics, and hardware/QEMU accounting remain open.
  - [x] Bound temporary compressed-texture decode buffers, copy-image
    snapshots, and readback native/tight/clipped staging to the same per-context
    512 MiB budget. Each candidate reserves its exact byte count before
    allocation and releases it on decode/conversion/readback failure and
    success; focused texture and sync regressions verify full-budget rejection
    and that successful readback leaves the persistent balance unchanged.
    Parser/lowerer workspace, diagnostic strings, backend-owned resources, and
    hardware/QEMU accounting remain open.
  - [x] Bound the varying-profile compacted-source workspace to the same
    per-context budget. The lowerer rejects a full reservation before malloc
    and releases its normalized source on profile success or rejection;
    `tests/shader_ir_test.c` verifies both paths. Other parser/lowerer
    workspaces, diagnostic strings, backend-owned resources, and hardware/QEMU
    accounting remain open.
  - [x] Bound LUMINANCE/LUMINANCE_ALPHA pipeline-cache fragment rewrites to the
    same per-context budget. The cache admits the exact temporary RSH1 copy
    before allocation and releases it on every validation, backend, and success
    path; `tests/pipeline_cache_test.c` fills the budget and verifies the cache
    miss is rejected before module or pipeline creation, then succeeds after
    release. Other parser/lowerer workspaces, diagnostics, backend-owned
    resources, and hardware/QEMU accounting remain open.
  - [x] Bound private Aquamarine bind-group metadata and decoded Float32
    sampled-mip snapshots to a 512 MiB per-surface budget. The exact RGBA-F32
    byte count is reserved before each snapshot allocation and released on
    conversion failure, bind-group rollback, and destruction; metadata is
    accounted for by the same owner. Compute resources remain explicitly
    unsupported, while other backend resources and hardware/QEMU accounting
    remain open.
  - [x] Extended the built generic software-backend budget to every dynamic
    backend-owned CPU allocation: buffer/image/shader shadows, resource
    metadata, sampler and pipeline metadata, graphics/compute bind groups, and
    compute writable-buffer shadows. Each exact size is admitted before
    allocation and released on validation, dispatch rollback, destruction, and
    backend teardown paths. The historical direct Aquamarine source is not in
    the current CMake/source manifest; GPU-driver accounting and hardware/QEMU
    evidence remain open.
  - [x] Charge parser and RSH1 lowerer reflection/instruction workspaces to
    the same per-context budget. Compile, shader lowering, link-time interface
    parsing, and program-owned uniform rebuilds reserve their complete bounded
    result (including the fixed diagnostic buffer) before touching source and
    release it on every success or rejection path. Full-budget regressions now
    reject before parsing/lowering and leave compile/link/module state unchanged;
    fixed-size object info logs remain bounded storage rather than an
    unaccounted dynamic allocation. Diagnostic text from future dynamically
    allocated consumers and hardware/QEMU accounting remain open.
  - [x] Charge the fixed pipeline-cache metadata itself to the context budget.
    Cache creation reserves the complete bounded entry table before `calloc`
    and releases that reservation on allocation failure and context teardown;
    `tests/pipeline_cache_test.c` fills the budget and verifies a cache miss
    fails before module or pipeline creation, then succeeds after release.
    Dynamic cache growth, backend-owned resources, and hardware/QEMU
    accounting remain open.
- [x] Add WebGL-oriented negative tests for malformed state and shader input.
  `tests/ringl_webgl_negative_test.c` covers failure-atomic descriptor queries,
  invalid viewport/scissor/depth/blend values, and malformed GLSL compile
  input. The broader allocation-limit audit and hardware/conformance evidence
  remain open below.

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
  - [x] Add a bounded malformed-source corpus to the strict RinGL IR test.
    Unbalanced constructors and overflow literals are rejected at compile or
    lowering, unsupported loops cannot publish an RSH1/module, and every case
    leaves the executable handles empty. Differential comparison against an
    independent GLSL compiler remains open.
- [x] Add API trace tests for representative GL sequences. `ringl-sync-test`
  drives the public clear→texture definition→`copyTexSubImage2D`→clear→
  `finish`→`readPixels` sequence and asserts the exact RinGPU command,
  transition, fence, wait, and readback order. An out-of-range source copy
  must return `INVALID_VALUE` before adding any backend event, so invalid GL
  state cannot become an unobserved driver-side mutation.
- [ ] Run an appropriate GLES conformance suite when the implementation is mature enough.
- [x] Document every known incompatibility before advertising a supported
  GL/GLES version. `docs/known-incompatibilities.md` makes the audited
  B/P/N inventory's product consequence explicit: no GLES/WebGL version is
  advertised, each partial/absent entry remains unavailable, and WebGL 2,
  conformance, hardware/QEMU, general GLSL, and unsupported resource/FBO
  semantics cannot be promoted by a synthetic embedding result.

## RinGPU/RinShader dependencies

See `docs/ringpu-gaps.md`. There are currently no known native-contract blockers for the GLES 2.0 milestone; new entries should be added only for concrete representational gaps.

## Optional software backend

- [ ] Expose compute-pipeline and compute-bind-group resources through the
  private Aquamarine/RinGL surface API. The built surface already delegates to
  the generic `ringpu_software_backend`, which validates compute storage
  bindings and executes bounded synchronous dispatches; this item remains open
  because RinGL has no public/private surface entry points or browser policy
  for admitting compute resources yet.
- [ ] Evaluate whether `OS-Core/libs/aquamarine` can be evolved into a useful RinGL software backend.
- [ ] Keep the existing Aquamarine software graphics library distinct from Aquamarine Shader Language.
- [ ] Do not make the GL frontend depend on software-rasterizer-specific types.
- [ ] If implemented, keep hardware and software backends behaviorally aligned through shared GL validation/state tests.

## First milestone definition of done

The first milestone is complete when a RinOS process can create a RinGL context, compile/link a minimal vertex and fragment shader, upload a vertex buffer, clear the default framebuffer, issue `glDrawArrays(GL_TRIANGLES, ...)`, and present the result through RinGPU, with validation and integration tests covering the same path.
