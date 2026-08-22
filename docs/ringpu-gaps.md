# RinGPU contracts currently blocking RinGL

This file records only gaps in the shared RinGPU/RinShader contract that block
RinGL from implementing GLES semantics cleanly. RinGL must not work around these
by adding GL-specific behavior to RinGPU or by inventing private RSH1 encodings.

## 1. 2D RGBA texture sampling in RinShader

GLES 2.0 `texture2D(sampler2D, vec2)` consumes two floating-point coordinates
and returns an RGBA `vec4`.

RSH1 v1 currently exposes `SAMPLE_IMAGE_F32` with one scalar `source0` register
and one scalar destination register. The validator treats `resource` as the
sampled-image slot and `immediate` as the sampler slot. The existing software
executor consequently implements a one-dimensional scalar sample.

RinGL needs an additive, validator-visible RinShader contract that can represent
at least:

- a 2D floating-point coordinate (`u`, `v`);
- one sampled-image resource binding and one sampler binding;
- four floating-point result components (`r`, `g`, `b`, `a`);
- fragment-stage validation with deterministic resource reflection.

The exact ABI shape belongs to RinShader/RinGPU. RinGL only requires that the
operation be unambiguous and versioned. Once available, the existing RinGL
`sampler2D` uniform metadata, texture-unit state, sampled-image realization and
sampler realization can lower directly to it.

## 2. General vertex-to-fragment varying contract

A normal GLES textured shader needs a path such as:

```glsl
attribute vec2 position;
attribute vec2 texCoord;
varying vec2 uv;

void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    uv = texCoord;
}
```

with the fragment shader consuming `varying vec2 uv`.

The current RinGPU graphics link profiles are positional and include special
fixed output/input-count cases, but do not yet provide a general contract that
separates the vertex position builtin from arbitrary user varyings and defines
how those varyings are interpolated into fragment inputs.

RinGL needs a versioned contract that provides at least:

- clip-space position as a dedicated vertex-stage builtin/output semantic;
- user vertex outputs with stable locations;
- matching fragment inputs with stable locations;
- perspective interpolation for floating-point varyings in the first profile;
- pipeline-link validation that checks the user-varying interface independently
  from the position builtin.

RinGL should not encode `gl_Position` and user varyings into an undocumented
positional output convention.

## 3. `UNSIGNED_BYTE` index format

GLES 2.0 `glDrawElements` requires `UNSIGNED_BYTE` and `UNSIGNED_SHORT` index
formats. RinGPU currently publishes only native `UINT16` and `UINT32` index
formats.

RinGL currently robustly supports the native two formats and deliberately does
not send byte indices through a mismatched RinGPU format. A RinGPU `UINT8` index
format would let RinGL map the GLES operation directly. Alternatively, if the
RinGPU project explicitly prefers translation layers to expand byte indices,
that policy should be documented as part of the contract so RinGL can implement
and test a bounded transient-index conversion path.

## 4. Viewport, scissor and rasterizer state

RinGL still needs GLES viewport/scissor/culling semantics. The current public
RinGPU graphics descriptors and draw commands do not expose a native viewport,
scissor rectangle, face-culling mode or front-face winding contract.

Before RinGL can make those GL states affect submitted rendering, RinGPU needs a
versioned rasterization-state contract. It may be immutable pipeline state,
dynamic command state, or a combination, but it should cover at least:

- viewport rectangle and depth range;
- scissor enable/rectangle;
- cull enable and front/back selection;
- clockwise/counter-clockwise front-face definition.

RinGL can track these states before that contract exists, but must not pretend
they affect rendering until the backend boundary supports them.

## Already available and not blockers

The current public RinGPU API already exposes the pieces RinGL needs for level-0
RGBA8 texture object realization: CPU-visible images, `ringpu_upload_image`,
`SHADER_READ` image state, sampler objects, typed graphics bindings, graphics
resource binding commands, and sampled-image/sampler resource kinds in
RinShader. Those should be used directly rather than duplicated in RinGL.
