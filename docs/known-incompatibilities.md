# RinGL known incompatibilities and exposure policy

RinGL is a compatibility translation layer with a bounded executable profile.
It is not an implementation claim for GLES 2.0, WebGL 1, OpenGL ES 3.x, or
WebGL 2. The complete 142-entry GLES 2.0 inventory in
[`gles2-api-status.md`](gles2-api-status.md) is the normative API-level
source: every row is classified as bounded (**B**), partial (**P**), or absent
(**N**). This document is the normative cross-category consequence of that
inventory for product and embedding policy.

An embedding may expose a call only when it can enforce the corresponding B or
P restriction before invoking RinGL. It must not turn an N entry into a
synthetic success result, fabricate an extension string, or advertise a GLES
or WebGL version based on a subset of similarly named functions.

## Version and conformance status

- No GLES, OpenGL ES, or WebGL conformance version is advertised.
- WebGL 2 and every OpenGL ES 3.x feature are unavailable.
- There is no completed Khronos conformance-suite result, hardware matrix, or
  QEMU browser-runtime evidence. Host regressions establish only the stated
  bounded behavior.
- The Ladybird route is
  `OpenGLContext -> rin_webgl_ringl_bridge -> RinGL -> RinGPU -> private
  ringl_aquamarine_surface -> ringpu_software_backend`. A direct Aquamarine
  WebGL renderer, a `rin_webgl_ringpu_surface` backend, and an embedding that
  replaces RinGL validation are not supported designs.

## API and query boundary

The inventory's P and N rows are incompatibilities, not deferred success
paths. In particular:

- shader compiler release, shader binaries, generic pointer-vector texture and
  vertex-attribute setters, generic typed state getters, unrestricted program,
  shader, texture, framebuffer, renderbuffer, uniform, and attribute queries
  are unavailable or limited to the explicitly documented records;
- identity strings are the four bounded RinGL/RSH1 strings only; extension
  strings and unknown query pnames are not invented;
- accepted limits are executable bounds (`MAX_TEXTURE_SIZE = 4096`, eight
  texture units, and `MAX_VERTEX_ATTRIBS = 16`), not a claim that all GLES
  minimums or query semantics are implemented;
- only the documented `PACK_ALIGNMENT`/`UNPACK_ALIGNMENT` values, sampler
  pnames, and WebGL-gated extension tokens are accepted; and
- an N/P or out-of-profile request must receive the defined validation error
  without a backend submission or a partially changed GL object.

## Resources, framebuffer, and rasterization

- Texture support is 2D only. Cube maps, 3D/array/immutable textures, general
  compressed formats, and unrestricted mip semantics are unavailable. ETC1,
  the documented S3TC formats, and logical sRGB are dedicated bounded paths;
  they do not imply general compressed/sRGB texture support.
- Color, depth, and stencil formats are restricted to the inventory's
  RGBA/canonical, native packed, D16/D32/D24S8/S8 matrix. Multisample storage,
  resolve, cube-face attachments, and attachment combinations outside the
  verified matrix are unavailable.
- `readPixels`, `copyTexImage2D`, and `copyTexSubImage2D` require the
  documented complete color target. Depth/stencil and multisample copy/read
  semantics are not implemented. Browser callers must use the capacity-aware
  byte-span APIs; legacy pointer-only entry points are for trusted native
  callers only.
- Draws support only the listed primitive modes, validated attribute layouts,
  and explicit RinGPU bindings. General vertex pulling, integer WebGL 2
  attributes, and unsupported resource layouts are unavailable.
- The bounded depth/stencil paths implement only their documented D24S8/S8
  behavior. Multisampled, cube, and unverified attachment semantics must not
  be exposed as working stencil support.

## GLSL ES and shader execution

RSH1 executes the finite profiles specified in
[`glsl-es-profile.md`](glsl-es-profile.md); it is not a general GLSL ES
compiler. Notable exclusions include general declarations/types/arrays,
arbitrary constructors and swizzles, unrestricted built-ins, arbitrary
texture-coordinate expressions, general varying combinations, general matrix
arithmetic, vertex pulling, loops, recursion, and general control flow.

`if`/`else`, Boolean operations, `discard`, derivatives, point size,
matrices, texture samples, and varying interpolation are available only in the
exact structural forms documented by that profile. A source program outside
those forms must fail compilation/linking without publishing partial RSH1 or
submitting a RinGPU command.

The generic no-varying matrix form permits `mat2`/`mat3`/`mat4` uniforms in
both vertex and fragment stages, including arrays of at most four elements
selected by an in-range decimal constant. These are program-owned scalar RSH1
constants and `matN * vecN` products; contiguous matrix uploads replace the
affected linked stage module atomically. Dynamic indexing, cross-dimension or
matrix/matrix arithmetic, and matrix use in the specialized varying/texture
profile remain unavailable and must be rejected before RinGPU submission.

## RinGPU and browser integration limits

The versioned RinGPU binding is a native execution contract, not an OpenGL
driver. A missing optional operation-table capability must make the affected
operation fail before mutation; RinGL must not emulate it by changing
unrelated state or falling back to an older direct renderer. The authoritative
ABI and ownership rules are in [`ringpu-integration.md`](ringpu-integration.md).

Context loss/restoration, final Ladybird product linking, browser JavaScript
execution, compositing/front-buffer contracts, and ISO/QEMU evidence remain
separate unfinished work. A green RinGL host test does not elevate any of
these limits into a product compatibility claim.

## Change rule

Any change that makes an API, enum, extension, or shader form observable must
update the API inventory and this document in the same change. It must state
the new executable boundary, negative behavior, and test evidence. Until that
happens, the feature remains unavailable rather than being reported as a
successful GLES/WebGL capability.
