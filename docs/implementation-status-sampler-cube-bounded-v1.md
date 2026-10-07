# Bounded samplerCube implementation status

Date: 2026-10-07

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
- RinGPU's validator recognizes the packed image/sampler binding pair, and its
  software executor calls the filtered cube sampler. This stays on the generic
  RinGL→RinGPU route.

The existing `tests/cube_map_test.c` source contract checks cube image factory
failure cleanup, six face uploads, framebuffer face identity, sampler
reflection, and the four RSH1 cube-sample opcodes. It does not submit a real
cube sample draw or verify the resulting pixel/mip choice through the
RinGL→RinGPU bridge.

## Remaining work

The RinGL TODO therefore retains an unchecked end-to-end sample/readback and
mip-selection regression plus runtime evidence. The source path must not be
treated as complete GLES/WebGL cube-map conformance. Other remaining texture
exclusions include 3D/array/shadow samplers and general conformance coverage.

## Verification in this continuation

No tests or builds were run. Source and existing test registration were read
only; this status records implementation scope and the missing runtime proof.
