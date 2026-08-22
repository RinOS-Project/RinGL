# Initial GLSL ES profile

RinGL begins with a deliberately bounded GLSL ES source profile. The goal is to get a correct vertex/fragment pipeline into RinShader IR before expanding language coverage.

## Source model

The first frontend target is GLSL ES 1.00-style vertex and fragment shader source. RinGL stores shader source independently of compilation and keeps the source language distinct from Aquamarine Shader Language. GLSL ES should lower directly to RinShader IR rather than being translated to Aquamarine source text.

The initial parser/compiler slice should support only what is required for the first hardware triangle:

- `void main()` entry points;
- scalar `float` declarations and arithmetic;
- vertex input attributes represented by the current scalar `FLOAT32` RinGL vertex profile;
- a vertex-position output path sufficient to produce clip-space position;
- a fragment color output path sufficient to produce one RGBA color target;
- constants and simple assignments;
- diagnostics for unsupported syntax instead of silently accepting it.

Vectors, matrices, uniforms, varyings beyond the first required link path, texture sampling, derivatives, loops, user functions, precision edge cases, and broader GLSL ES built-ins are incremental work unless they become necessary for the first triangle.

## Compiler boundary

```text
GLSL ES source
    |
lexer/parser + semantic validation
    |
RinGL shader IR/frontend state
    |
lower directly
    v
RinShader IR (RSH1)
    |
RinShader validation
    v
RinGPU shader module
```

Compilation failure must remain a shader-object result and must not submit malformed IR to RinGPU. Link-time validation is responsible for vertex/fragment interface compatibility.

## Source limits

The bootstrap source-storage implementation caps an individual shader source at 16 MiB. This is an implementation safety limit, not a claimed GLES conformance limit. Browser/WebGL embedding may impose tighter limits.

## Compatibility claims

RinGL must not advertise GLSL ES 1.00 or OpenGL ES 2.0 conformance merely because this profile uses their syntax as a target. Version claims are deferred until the required language, API semantics, limits, and conformance tests are implemented.
