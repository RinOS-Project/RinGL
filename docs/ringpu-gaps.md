# RinGPU contracts currently blocking RinGL

This file records only gaps in the shared RinGPU/RinShader contract that block
RinGL from implementing GLES semantics cleanly. RinGL must not work around these
by adding GL-specific behavior to RinGPU or by inventing private RSH1 encodings.

## 1. Negative viewport origin

OpenGL ES permits `glViewport(x, y, width, height)` with negative `x` and `y`.
The viewport transform still uses that origin; clipping the resulting rectangle
is not equivalent because the NDC-to-window mapping itself changes.

The current public RinGPU `RinGpuViewportV1` validator requires
`viewport.x >= 0` and `viewport.y >= 0`. RinGL can safely map positive origins,
and a zero width/height can be implemented as a no-raster draw, but RinGL cannot
faithfully translate a negative viewport origin without inventing an extra
shader transform.

RinGPU should allow finite negative viewport `x`/`y` values and let the backend
clip rasterization to the active render target. Width and height may remain
strictly positive in the native contract because RinGL can preserve the GLES
zero-area case as an explicit draw no-op.

Until that contract changes, RinGL accepts and queries negative GL viewport
origins but fails affected submissions closed rather than silently changing the
viewport transform.

## Resolved native dependencies

The current OS-Core `main` branch now provides the other contracts previously
identified as blockers:

- `SAMPLE_IMAGE_2D_I32/F32` with explicit U/V coordinates and an R/G/B/A
  component selector in the instruction flags field;
- a native graphics pipeline descriptor combining vertex layout, position
  output, user varyings, depth, blend, color-write, cull and front-face state;
- explicit varying location/type/interpolation descriptors;
- dynamic viewport/scissor state through `ringpu_command_set_raster_state()`;
- native `UINT8` index format;
- CPU-visible image upload, sampled images, samplers and typed graphics
  bindings;
- fence completion waits through `ringpu_wait_fence()`;
- CPU-readable images and bounded image readback through
  `ringpu_readback_image()`.

RinGL should consume those contracts directly rather than duplicating them.

## Notes

The public readback contract currently covers images, which is sufficient for
GLES 2.0 `glReadPixels`. RinGL does not require a general buffer-download API for
its current GLES 2.0 milestone.

If another native dependency is discovered while implementing RinGL, add it
here with the exact GLES operation that cannot be represented and the exact
missing native contract. Do not add speculative feature requests.
