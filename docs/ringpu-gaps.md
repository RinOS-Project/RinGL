# RinGPU contracts currently blocking RinGL

This file records only gaps in the shared RinGPU/RinShader contract that block
RinGL from implementing GLES semantics cleanly. RinGL must not work around these
by adding GL-specific behavior to RinGPU or by inventing private RSH1 encodings.

## 1. RGBA result contract for 2D texture sampling

GLES 2.0 `texture2D(sampler2D, vec2)` consumes two floating-point coordinates
and returns an RGBA `vec4`.

RinShader now exposes additive `SAMPLE_IMAGE_2D_I32` and
`SAMPLE_IMAGE_2D_F32` opcodes. Their validator-visible input/resource contract
solves the coordinate side of the previous gap: `source0` and `source1` are the
U/V coordinates, `resource` names the sampled image, and `immediate` names the
sampler.

The remaining ambiguity is the result. The current instruction still has one
scalar `destination` register and the validator defines only that one register.
There is no component selector and no published rule that the destination names
four consecutive RGBA registers. RinGL therefore still cannot map GLSL
`texture2D()`'s `vec4` result without inventing semantics.

RinGL needs the shared contract to define one of the following, or an equivalent
unambiguous versioned representation:

- one sample instruction that writes four defined floating-point RGBA result
  registers; or
- a component-selection mechanism that lets four scalar sample instructions
  retrieve R/G/B/A deterministically.

Once that result contract is published, the existing RinGL sampler uniform
metadata, texture-unit state, sampled-image realization and sampler realization
can lower directly to it.

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

## 3. Viewport, scissor and rasterizer state

RinGL now tracks GLES viewport/scissor/culling/front-face state and exposes the
corresponding queries. The current public RinGPU graphics descriptors and draw
commands do not expose a native viewport, scissor rectangle, face-culling mode
or front-face winding contract, so RinGL deliberately does not claim that these
states affect submitted rendering yet.

RinGPU needs a versioned rasterization-state contract. It may be immutable
pipeline state, dynamic command state, or a combination, but it should cover at
least:

- viewport rectangle and depth range;
- scissor enable/rectangle;
- cull enable and front/back selection;
- clockwise/counter-clockwise front-face definition.

## 4. Completion wait semantics for `glFinish`

RinGPU exposes fence creation, a signal fence/value on queue submission, and a
fence value query. RinGL still needs an explicit contract for waiting until all
previously submitted GPU work has completed before `glFinish` can return.

If `ringpu_fence_value()` is defined to expose device-completed rather than only
accepted/published work, RinGPU should document the allowed bounded wait/poll
pattern and device-loss behavior. Otherwise RinGPU needs a wait primitive, for
example a versioned fence-wait API with timeout/device-loss semantics.

RinGL must not implement `glFinish` by assuming that successful queue submission
itself means GPU execution completion.

## 5. CPU readback for `glReadPixels` and observable buffer reads

The public API has CPU-to-GPU upload paths, but RinGL has not found a public
GPU-to-CPU image/buffer readback primitive. GLES requires framebuffer readback
through `glReadPixels`, and later compatibility work may require observable
buffer readback as well.

RinGPU needs a bounded native readback contract, either through CPU-readable
resources plus map/invalidate semantics or explicit buffer/image download APIs.
It should define completion synchronization, row/slice pitch handling for
images, bounds checking, cache synchronization, and device-loss behavior.

RinGL should not read backend-private allocation cookies or rely on a software
backend's host pointer to emulate this on hardware paths.

## Already available and not blockers

The current public RinGPU API already exposes the pieces RinGL needs for level-0
RGBA8 texture object realization: CPU-visible images, `ringpu_upload_image`,
`SHADER_READ` image state, sampler objects, typed graphics bindings, graphics
resource binding commands, and sampled-image/sampler resource kinds in
RinShader. It also exposes the first depth and blend pipeline contracts.

RinGPU now also exposes an additive native `UINT8` index format, so RinGL maps
GLES `UNSIGNED_BYTE` element indices directly rather than expanding them through
a private transient conversion path.
