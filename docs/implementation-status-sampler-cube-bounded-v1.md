# Bounded samplerCube implementation status

Date: 2026-10-09

## Source implementation

The bounded core cube path is connected in RinGL and RinGPU:

- `samplerCube` declarations retain a distinct reflected sampler type. An
  active `textureCube(sampler, vec3)` emits four
  `SAMPLE_IMAGE_CUBE_F32` component operations with the live Float32 direction
  registers and the selected image/sampler resource pair.
- A RinGL cube texture owns six faces, requires square matching level-zero
  dimensions and matching storage formats, and realizes the faces through the
  versioned RinGPU image-array callbacks. Sampling validates the cube wrap
  requirements and every mip level required by the selected minification
  filter. The represented color profile also includes cube-face color FBO
  attachments and bounded mip generation.
- RinGPU's validator recognizes the packed image/sampler binding pair for
  `SAMPLE_IMAGE_CUBE_F32`, and its software executor calls the filtered cube
  sampler. This stays on the generic RinGL→RinGPU route.
- The Aquamarine software surface permits six-layer images and returns generic
  multi-layer images to the software backend instead of rejecting them in the
  external surface-image callback. Its adapter, queue, and RinGL-created
  command lists advertise presentation capability so the same bridge can
  submit the default framebuffer.

`tests/cube_map_test.c` retains source-level checks for cube image factory
failure cleanup, six face uploads, framebuffer face identity, sampler
reflection, and the four RSH1 cube-sample opcodes. The root host regression
`tests/rin_webgl_cube_sampler_product_test.c` now also submits a real cube
sample draw and reads its result through the RinGL→RinGPU bridge. It uses
unique colors for every face and mip; the 2×2 draw reads +Z level zero as
`(17,34,51,255)`, then reads +Z level one as `(19,83,201,255)` after enabling
nearest-mipmap selection.

## Remaining work

The bounded end-to-end regression is complete. This does not claim complete
GLES/WebGL cube-map conformance. Other remaining texture exclusions include
3D/array/shadow samplers and general conformance coverage.

## Verification in this continuation

On the Windows 10 host with GCC 13.2.0, both
`rin_webgl_cube_sampler_product_test.c` and
`rin_webgl_ringl_bridge_test.c` completed with PASS through
`scripts/run_host_tests.py`. The full bridge run also verifies presentation
after adding `PRESENT` to the software surface adapter/queue capabilities and
to RinGL's command-list capability mask.

The runner reports are `valid: false` because the shared root was already
dirty when each run started. They record successful test process results, not
clean-tree provenance. No physical GPU, QEMU, or broader GLES/WebGL
conformance evidence is claimed.
