# Initial GLSL ES profile

RinGL begins with a deliberately bounded GLSL ES source profile. The goal is to get a correct vertex/fragment pipeline into RinShader IR before expanding language coverage.

## Source model

The first frontend target is GLSL ES 1.00-style vertex and fragment shader source. RinGL stores shader source independently of compilation and keeps the source language distinct from Aquamarine Shader Language. GLSL ES lowers directly to RinShader IR rather than being translated to Aquamarine source text.

The current first-triangle slice supports:

- `void main()` entry points;
- scalar `float`/`int` declarations and same-type arithmetic;
- same-basic-type `vec2`/`vec3`/`vec4` and `ivec2`/`ivec3`/`ivec4`
  constructors: an exact scalar/vector component list, a same-width matching
  vector copy, or one scalar splat. A splat aliases one scalar RSH1 register
  across the target components; Float/i32 conversion stays explicit and scalar
  through `int(...)` or `float(...)`;
- read-only one-through-four-component vector/ivector swizzles using exactly
  one of the `xyzw`, `rgba`, or `stpq` alphabets, including repeated and
  chained selectors such as `.stpq.bgra`; matching generic vertex `varying`
  `vec2`/`vec3`/`vec4` declarations may also use one writable, non-repeating
  selector per assignment (for example `color.rgb = source.bgr; color.a =
  0.5`), which maps directly to scalar RSH1 output stores;
- component-wise `+`, `-`, `*`, and `/` with same-width vectors or one scalar
  broadcast across a vector;
- generic no-varying vertex and fragment `mat2`/`mat3`/`mat4` values:
  scalar-diagonal,
  scalar/vector-component, and matching-dimension copy constructors; initialized
  local matrices and matrix uniforms; `matrixCompMult(matN, matN)` with
  matching Float dimensions; and the resulting `matN * vecN`. Up to four
  matrix-array elements of each type may be selected with an in-range decimal
  constant in either stage. RinGL retains
  column-major elements and lowers every component product to scalar RSH1
  `MUL_F32`, so RinGPU executes the operation rather than an embedding. Matrix
  dynamic indexing, cross-dimension conversion, arbitrary matrix arithmetic,
  and the specialized varying/texture profile remain unsupported;
- scalar Float or i32 `if` conditions with exactly one comparison and a
  mandatory `else`. Each branch normally writes one complete
  `gl_Position`/`gl_FragColor` `vec4`; in a fragment shader, exactly one branch
  may instead contain terminal `discard;` when the opposite branch writes the
  complete `gl_FragColor`. RinGL emits the original comparison, an i32 zero
  test, and forward RSH1 `JUMP_IF`/`JUMP`, which the generic RinGPU backend
  executes. The discarded path terminates before output/depth/stencil/color
  publication. Both-discard branches, nested or local-mutating branches,
  vector/mixed-type conditions, partial outputs, and loops remain unsupported;
- standalone fragment `discard;`: RinGL emits RSH1 `DISCARD`, and the generic
  RinGPU backend terminates the fragment before output validation or
  depth/stencil/color publication. Vertex discard, conditional discard outside
  the exact scalar if/else form above, loops, and general control flow remain
  unsupported;
- Float `min`, `max`, `clamp`, `mix`, and `dot`: min/max/clamp use the shared
  scalar RSH1 min/max opcodes, while mix/dot expand to ordered scalar
  arithmetic. Only GLSL's matching Float scalar/vector overloads are admitted;
  integer, matrix, scalar-vector `dot`, and mismatched-width calls fail before
  RSH1 publication;
- Float `floor`, `ceil`, `fract`, `mod`, `abs`, `sign`, `step`, and
  `smoothstep`: `floor` uses scalar RSH1 opcode 59, executed by generic RinGPU
  without a hosted math-library or embedding callback. `ceil`/`fract`/`mod`
  lower to floor plus source-ordered arithmetic; `abs`/`sign`/`step` and
  `smoothstep` lower to scalar min/max, arithmetic, and comparisons. The
  profile admits the defined Float scalar/vector overloads only. Integer,
  matrix, and mismatched-vector forms fail before RSH1 publication; a zero
  `mod` divisor or equal `smoothstep` edges is not rounded into success and
  fails the backend preflight without publishing a target;
- Float geometric `sqrt`, `inversesqrt`, `length`, `distance`, `normalize`,
  `cross`, `reflect`, `faceforward`, and `refract`: scalar/vector values lower
  to ordinary scalar RSH1 arithmetic, comparisons, and opcode 60 `sqrt`, which
  generic RinGPU executes with its freestanding finite binary32 square root.
  `cross` is vec3-only; `reflect`, `faceforward`, and `refract` require matching
  vec2/vec3/vec4 operands, and `refract` requires scalar Float eta. Integer,
  matrix, and mismatched-width forms fail before RSH1 publication. Invalid
  square-root domains and zero normalization/inverse-square-root inputs fail
  backend preflight rather than substituting a host result;
- Float trigonometric `radians`, `degrees`, `sin`, `cos`, `tan`, `asin`,
  `acos`, and one/two-argument `atan`: angle conversion scalarizes to ordinary
  Float multiplication; RSH1 opcodes 61--66 execute finite sin/cos/atan and
  inverse-trigonometric operations in generic RinGPU, while `tan` expands to
  sin/cos/division. All matching Float scalar/vector overloads are accepted;
  two-argument `atan(y, x)` requires matching widths. Sin/cos/tan retain a
  finite `[-1024, 1024]` radian execution domain, asin/acos require `[-1, 1]`,
  and atan(0,0) fail lowering or backend preflight rather than manufacturing a
  result; scientific Float literals lower only when finite binary32, so an
  overflowing literal fails before module publication;
- Float exponential `exp`, `log`, `exp2`, `log2`, and `pow`: `exp` and `log`
  scale executable `exp2`/`log2`; RSH1 opcodes 67--69 execute the base-two
  and power operations in generic RinGPU. Matching Float scalar/vector
  overloads are accepted, with matching widths required for `pow`. `exp2`
  and the derived `exp`/`pow` result exponent are bounded to `[-126, 127]`;
  `log`/`log2` inputs and `pow` bases must be strictly positive. Domain/range
  failure rejects target publication;
- vertex `attribute float` and `attribute vec2` inputs;
- `vec2(...)` and `vec4(...)` constructors, including their matching-basic-type
  single-scalar splats;
- scalar or `vec4` writes to the stage output (`gl_Position` / `gl_FragColor`);
- constants and simple assignments;
- context-gated fragment `#extension GL_OES_standard_derivatives : enable` or
  `require`, with `dFdx`, `dFdy`, and `fwidth` over float/vecN values formed
  from fragment varyings and the supported arithmetic;
- context-gated fragment `#extension GL_EXT_shader_texture_lod : enable` or
  `require`, with `texture2DLodEXT(sampler2D, accepted vec2, accepted scalar
  float)`. Every RGBA component is a real `SAMPLE_IMAGE_2D_LOD_F32` RSH1
  operation carrying the live Float32 LOD register and packed image/sampler
  bindings; generic RinGPU selects the explicit mip with sampler min/max-LOD
  clamping. This form does not use derivatives or an embedding-side sampler.
  `texture2DGradEXT`, cube/3D/array/shadow texture forms, and unsupported
  scalar expressions still reject before module publication;
- a canonical `varying vec2` texture-coordinate path with one through eight
  `texture2D()` calls over one through eight `uniform sampler2D` declarations,
  where each coordinate is either the shared varying or that varying plus/minus
  one finite literal `vec2` offset;
- generic fragment `texture2D(sampler2D, vec2)` lowering for coordinates made
  from other accepted generic Float `vec2` values (locals, matching varyings,
  swizzles, arithmetic, and numeric uniforms). Each active sampler is reflected
  as an adjacent RSH1 image/sampler pair and each lookup emits four scalar
  samples; the generic 128-instruction/96-register RSH1 budget is enforced
  before module publication. `sampler2D name[N]` accepts a positive decimal
  `N` only within the eight-element total cap, and each lookup needs an
  in-range decimal literal or `const int` initialized with an integer literal;
  every selected element has its own real resource pair. The same bounded
  constant-index rule applies to bounded numeric
  uniform arrays (eight scalar/vector elements per type; four vertex matrices).
  Dynamic indexing, non-2D/gradient forms, and over-budget or otherwise
  unsupported expressions reject;
- generic fragment `texture2DProj(sampler2D, vec3|vec4)` lowering: the first
  two Float components are divided by the last (`xy / z` or `xy / w`) through
  two live RSH1 `DIV_F32` instructions, then use the ordinary four-component
  sampled image/sampler lookup. Accepted projected expressions share the
  generic local/varying/swizzle/arithmetic/uniform path. A literal zero
  denominator rejects while lowering, while a dynamic zero reaches normal
  RinGPU preflight and leaves the target unpublished; cube, gradient, shadow,
  and other texture forms remain unsupported;
- a constant-coordinate-only texture profile: one declared `sampler2D` may be
  sampled one through eight times, while a multi-declaration program uses each
  of one through eight declared samplers exactly once; results are added
  left-to-right for `gl_FragColor`;
- a matched, perspective-interpolated `varying vec2` between the initial
  vertex and fragment profiles;
- generic assignment from matching vertex `varying vec2`/`vec3`/`vec4`
  declarations to matching fragment declarations, with up to 28 scalar
  perspective components in one linked interface;
- diagnostics for unsupported syntax instead of silently accepting it.

RinShader RSH1 remains scalar. Vector values are flattened by RinGL into consecutive scalar F32 I/O slots. For example, one `attribute vec2 position` occupies input slots 0 and 1, while `gl_Position = vec4(position, 0.0, 1.0)` stores four scalar outputs. This keeps vector source semantics above the stable RSH1 instruction ABI.

The generic varying route reserves vertex output slots 0--3 for clip `xyzw`
and maps matching `vec2`/`vec3`/`vec4` declarations densely from output slot 4
to fragment input slot 0. It accepts at most 28 scalar interpolants, exactly
matching RinGPU's native varying budget; a larger interface fails linking
before a pipeline can be created. The fragment lowerer emits a typed scalar
input load for every declared component, even if source expressions do not use
that declaration, so RSH1 reflection and RinGPU's complete-interface
validation cannot disagree. A vertex varying may be written as a whole vector
or by non-overlapping writable selector assignments. RinGL records the exact
component initialization map and only links when every declared component has
an RSH1 store; an unwritten component fails linking instead of reaching native
pipeline creation with an untyped output slot. Repeated writable components,
mixed selector alphabets, and out-of-range components fail compilation. This
does not claim general varying expressions or generic texture-coordinate
support.

The fixed `gl_Position` (vertex) and `gl_FragColor` (fragment) outputs accept
non-overlapping writable `xyzw`/`rgba`/`stpq` selectors. RinGL emits one
`STORE_OUTPUT_F32` for each selected component in source order, rather than
forming a host-side vector or calling an embedding renderer. Once such a
selector is used, all four clip/color components must be covered before the
RinGL→RinGPU module is published; an incomplete output fails link and cannot
receive a synthetic default component. Repeated, mixed-family, and out-of-range
selectors still fail compilation. `gl_PointSize` and `gl_FragDepthEXT` remain
scalar whole-output forms. With `GL_EXT_draw_buffers` enabled, indexed literal
`gl_FragData[0..3]` also accepts the same selectors and stores to the selected
attachment's scalar slots; the existing MRT finalization emits zero stores for
each component the source did not write before publishing its 16-output ABI.

The bounded perspective-color profiles apply the same read-only selector rule
to their `varying vec3`/`vec4` fragment values. A full-width selector is
composed directly into the scalar input registers, so `vertexColor.stpq.bgra`
and `vec4(vertexColor.bgr, 1.0)` do not require a backend-specific vector
instruction. The generic vertex-varying route above additionally supports
non-overlapping component writes; partial-width structural material selectors
remain outside these profiles.

The bounded `texture2D(colorTexture, uv) * vertexColor * tint` material uses
the same four-component permutation for a full-width `varying vec4` selector
and for its optional `uniform vec4` tint, before component-wise texture
modulation. It does not introduce a general vector operation or broaden the
accepted material grammar.

The general bounded varying-coordinate texture chain applies the same
full-width rule to its sole `uniform vec4` color operand. Thus
`texture2D(texture, uv) * tint.stpq.bgra` is scalar-constant permutation, not
a general uniform-vector expression; for one sample the same selector may lead
the product. Partial-width selectors remain rejected.

The first visible-triangle shader can therefore take the standards-shaped form:

```glsl
attribute vec2 position;
void main() {
    gl_Position = vec4(position, 0.0, 1.0);
}
```

with a constant fragment color such as:

```glsl
void main() {
    gl_FragColor = vec4(1.0, 0.25, 0.0, 1.0);
}
```

The normal texture path is intentionally narrow, and the varying path recognizes
the canonical position/UV textured-triangle form. The constant-coordinate
profile permits one through eight calls over one through eight declarations in
an exact left-to-right `texture2D(a, vec2(...)) + ...` assignment. It compacts only
active declarations, in declaration order, into image/sampler pairs (`[0, 1]`,
then `[2, 3]`, and so on); every repeated call references the corresponding
pair. RinGL uses the saved active-declaration map to make the matching selective
typed bind group, so unused declarations need no fabricated resource use. The
profile has a formulaic maximum of 85 instructions and 80 registers, below
RinGL's 96-register ceiling and RinGPU's public 256-register limit.
The varying-coordinate multi-sampler extension accepts one through eight calls
over one through eight sampler declarations, one shared `varying vec2`, and an
exact left-to-right addition. Each call may use `uv`, `uv + vec2(u, v)`, or
`uv - vec2(u, v)` where both decimal/exponent-form literals are finite. Calls
may repeat an active sampler and inactive declarations are compacted in
declaration order into dense image/sampler pairs; the saved map creates
bindings for only those declarations. The direct-coordinate maximum is 69
instructions and 64 registers; with an offset on every call it is 101
instructions and 68 registers. Coordinates derived from locals or different
varyings, other expressions, and larger chains are not yet accepted.
Nonconstant coordinates in this profile, general swizzle writes outside the
documented generic vertex-varying lvalue form, implicit float/integer
conversion, vector constructors with mixed scalar types,
matrices beyond the documented bounded `matrixCompMult`/matrix-vector
vertex/fragment forms, dynamic uniform-array indexing (bounded scalar/vector
integer/Boolean arrays use at most eight elements per type and matrices at
most four),
additional varying types, loops, user functions,
general/nested control flow, precision edge cases, and
broader GLSL ES built-ins remain incremental work.

## `OES_standard_derivatives` boundary

The extension is deliberately two-gated.  The source must begin with the exact
fragment directive `#extension GL_OES_standard_derivatives : enable` or
`require`, and the current RinGL context must have been enabled by its WebGL
embedding after `getExtension("OES_standard_derivatives")`.  A directive alone
does not grant the capability.  `dFdx`, `dFdy`, and `fwidth` lower
component-wise to RSH1 opcodes 56, 57, and 58; `fwidth(x)` is evaluated as
`abs(dFdx(x)) + abs(dFdy(x))` by the executor.

The bounded profile accepts finite scalar/vector values obtained from fragment
varyings and supported arithmetic. It does not implement derivatives through
texture samples, the bounded conditional form, or `dFdxFine`/`dFdyFine`/coarse
variants. Those forms fail compilation/linking before a draw is submitted;
they are not mapped to a zero derivative or to a browser-local fallback.

## Vertex input mapping

RinGL expands a GL `size=2` attribute into two consecutive scalar RinGPU
inputs at offsets `base` and `base + component-size`. A tightly packed vec2
therefore has an effective stride of 8 bytes for `FLOAT` input.

`ringl_bind_attrib_location()` records a generic vertex-array index by name
for the program's next successful link. Link assigns every remaining active
attribute the first unused generic index, and `ringl_get_attrib_location()`
reports that linked value. At draw time RinGL uses the linked index to select
the matching `vertexAttribPointer` state, then flattens only the active shader
components into dense RSH1 locations. This keeps the generic GL namespace out
of the RinGPU ABI while ensuring that an unrelated enabled array is not
silently fetched.

Each enabled active array retains the buffer and effective stride captured by
its `vertexAttribPointer` call. RinGL groups equal `(buffer, stride)` pairs
into dense V2 bindings, so direct and indexed draws can source independent
active arrays without copying or rebinding their storage. A disabled active
array is not fetched: its current generic value (initially `(0, 0, 0, 1)`,
then updated by `vertexAttrib[1-4]f`) is lowered to explicit constant Float32
scalar descriptors. An embedding must advertise
`RINGL_RIN_GPU_VERTEX_INPUT_CONSTANT_FLOAT32` and, when more than one stream
is needed, `RINGL_RIN_GPU_VERTEX_INPUT_MULTI_BUFFER` with matching V2
callbacks; otherwise the draw is rejected with `INVALID_OPERATION`. This keeps
Float32 bits from being mistaken for byte offsets and does not silently flatten
distinct streams. General vertex pulling remains unsupported.

## Compiler boundary

```text
GLSL ES source
    |
lexer/parser + semantic validation
    |
RinGL scalar/vector frontend values
    |
flatten vector I/O + lower
    v
RinShader IR (RSH1 scalar slots)
    |
RinShader validation
    v
RinGPU shader module
```

Compilation failure must remain a shader-object result and must not submit malformed IR to RinGPU. Link-time validation is responsible for vertex/fragment interface compatibility as that interface grows.

## Source limits

The bootstrap source-storage implementation caps an individual shader source at 16 MiB. This is an implementation safety limit, not a claimed GLES conformance limit. Browser/WebGL embedding may impose tighter limits.

## Compatibility claims

RinGL must not advertise GLSL ES 1.00 or OpenGL ES 2.0 conformance merely because this profile uses their syntax as a target. Version claims are deferred until the required language, API semantics, limits, and conformance tests are implemented.
