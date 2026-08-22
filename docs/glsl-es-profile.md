# Initial GLSL ES profile

RinGL begins with a deliberately bounded GLSL ES source profile. The goal is to get a correct vertex/fragment pipeline into RinShader IR before expanding language coverage.

## Source model

The first frontend target is GLSL ES 1.00-style vertex and fragment shader source. RinGL stores shader source independently of compilation and keeps the source language distinct from Aquamarine Shader Language. GLSL ES lowers directly to RinShader IR rather than being translated to Aquamarine source text.

The current first-triangle slice supports:

- `void main()` entry points;
- scalar `float` declarations and arithmetic;
- vertex `attribute float` and `attribute vec2` inputs;
- `vec2(...)` and `vec4(...)` constructors;
- scalar or `vec4` writes to the stage output (`gl_Position` / `gl_FragColor`);
- constants and simple assignments;
- one `uniform sampler2D` and one `texture2D()` call with either constant
  `vec2` coordinates or the initial `varying vec2` texture-coordinate path;
- a constant-coordinate-only texture profile: one declared `sampler2D` may be
  sampled one through eight times, while a multi-declaration program uses each
  of one through eight declared samplers exactly once; results are added
  left-to-right for `gl_FragColor`;
- a matched, perspective-interpolated `varying vec2` between the initial
  vertex and fragment profiles;
- diagnostics for unsupported syntax instead of silently accepting it.

RinShader RSH1 remains scalar. Vector values are flattened by RinGL into consecutive scalar F32 I/O slots. For example, one `attribute vec2 position` occupies input slots 0 and 1, while `gl_Position = vec4(position, 0.0, 1.0)` stores four scalar outputs. This keeps vector source semantics above the stable RSH1 instruction ABI.

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

The normal texture path is intentionally narrow: it accepts exactly one sampler
and one sample operation, and the varying path recognizes the canonical
position/UV textured-triangle form. The constant-coordinate profile permits one
through eight calls over one through eight declarations in an exact
left-to-right `texture2D(a, vec2(...)) + ...` assignment. It compacts only
active declarations, in declaration order, into image/sampler pairs (`[0, 1]`,
then `[2, 3]`, and so on); every repeated call references the corresponding
pair. RinGL uses the saved active-declaration map to make the matching selective
typed bind group, so unused declarations need no fabricated resource use. The
profile has a formulaic maximum of 85 instructions and 80 registers, below
RinGL's 96-register ceiling and RinGPU's public 256-register limit.
Nonconstant coordinates in this profile, other arithmetic, vector locals,
matrices, other uniform types, additional varying types, derivatives, loops,
user functions, precision edge cases, and broader GLSL ES built-ins remain
incremental work.

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
