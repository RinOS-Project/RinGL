# RinGPU contracts currently blocking RinGL

This file records only gaps in the shared RinGPU/RinShader contract that block
RinGL from implementing GLES semantics cleanly. RinGL must not work around these
by adding GL-specific behavior to RinGPU or by inventing private RSH1 encodings.

## Current status

There are no currently known RinGPU/RinShader contract blockers for the RinGL
GLES 2.0 milestone.

The current OS-Core `main` branch provides the contracts previously identified
as blockers:

- `SAMPLE_IMAGE_2D_I32/F32` with explicit U/V coordinates and an R/G/B/A
  component selector in the instruction flags field;
- a native graphics pipeline descriptor combining vertex layout, position
  output, user varyings, depth, blend, color-write, cull and front-face state;
- explicit varying location/type/interpolation descriptors;
- dynamic viewport/scissor state through `ringpu_command_set_raster_state()`;
- finite negative viewport X/Y origins, with attachment clipping performed by
  the backend rather than changing the viewport transform;
- native `UINT8` index format;
- CPU-visible image upload, sampled images, samplers and typed graphics
  bindings;
- optional CPU-visible multi-mip 2D image creation and per-level upload via
  the RinGL V2 callback tail, mapped directly to `mip_levels` and `mip_level`
  in the public RinGPU image ABI;
- fence completion waits through `ringpu_wait_fence()`;
- CPU-readable images and bounded image readback through
  `ringpu_readback_image()`.

RinGL should consume those contracts directly rather than duplicating them.
Remaining GLES behavior is RinGL implementation work unless another concrete
native-boundary gap is discovered.

## Notes

The public readback contract currently covers images, which is sufficient for
GLES 2.0 `glReadPixels`. RinGL does not require a general buffer-download API for
its current GLES 2.0 milestone.

If another native dependency is discovered while implementing RinGL, add it
here with the exact GLES operation that cannot be represented and the exact
missing native contract. Do not add speculative feature requests.
