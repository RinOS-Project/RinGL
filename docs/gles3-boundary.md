# RinGL GLES 3.x boundary

RinGL does not advertise or implement a GLES 3.x core profile. The raw API
remains a bounded GLES 2.0-shaped surface, and browser WebGL 2 remains
unavailable. In particular, RinGL must reject (or leave unexposed) `#version
300 es`, GLES 3 program interfaces, and core-only objects or enums rather than
turning a partial implementation into a successful version query.

The following extensions are independent, explicitly gated slices and do not
change that boundary:

- `OES_vertex_array_object` captures the bounded VAO state documented in
  `TODO.md`.
- `ANGLE_instanced_arrays` uses the bounded instanced draw entry points and
  RinGPU instance limits.
- `WEBGL_draw_buffers` exposes the bounded four-color-target MRT contract.
- `EXT_sRGB`, packed texture formats, and shader texture LOD expose only their
  individually documented RinGL/RinGPU profiles.

Core GLES 3 VAOs, unrestricted instancing, integer vertex attributes, 3D or
array textures, transform feedback, uniform buffers, multisample resolve, and
the remaining GLES 3 shader language stay outside the contract until RinGPU
has corresponding typed resources and end-to-end tests. A future expansion
must add a new versioned boundary and capability gate; it must not broaden the
current GLES 2/WebGL 1 identity implicitly.
