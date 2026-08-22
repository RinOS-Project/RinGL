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

The texture path is intentionally narrow: it accepts exactly one sampler and
one sample operation, and the varying path recognizes the canonical
position/UV textured-triangle form. Vector arithmetic, vector locals,
matrices, other uniform types, additional varying types, texture expressions,
derivatives, loops, user functions, precision edge cases, and broader GLSL ES
built-ins remain incremental work.

## Vertex input mapping

The current native RinGPU vertex profile exposes scalar `FLOAT32` attributes. RinGL expands a GL `size=2` floating-point attribute into two consecutive native scalar locations at offsets `base` and `base + 4`. A tightly packed vec2 therefore has an effective stride of 8 bytes.

For the bootstrap profile, enabled GL attributes are resolved in ascending attribute-index order and flattened into consecutive RSH1 input locations. The first triangle uses one vec2 at GL attribute index 0. Full GLSL attribute-location linking/reflection is later compatibility work.

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
