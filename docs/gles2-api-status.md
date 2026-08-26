# GLES 2.0 API inventory and status

This is the source-of-truth inventory for RinGL's GLES 2.0-shaped API. It was
audited against the 142 `gl*` entry points in the local GLES 2.0 `gl2.h`
registry and against `include/ringl/ringl.h` plus `ringl_sync.h`.

It deliberately distinguishes a real, bounded RinGL implementation from an
absent API. It does not claim GLES 2.0 conformance, nor does it authorize an
embedding to expose a WebGL feature that RinGL cannot execute.

[`known-incompatibilities.md`](known-incompatibilities.md) is the companion
product-exposure policy. It collects the cross-cutting limits implied by this
inventory and prohibits advertising a GLES/WebGL version from this bounded
profile.

Status meanings:

- **B** — a raw RinGL equivalent executes a stated bounded profile and rejects
  values outside it before submission.
- **P** — a dedicated/query subset exists, but it is not a general equivalent.
- **N** — no raw RinGL API exists; an embedding must not report success or
  synthesize a result.

## Entry points

| GLES 2.0 entry point | Status | RinGL boundary / restriction |
| --- | --- | --- |
| `glActiveTexture` | B | `ringl_active_texture`; eight `TEXTURE_2D` units. |
| `glAttachShader`, `glDetachShader`, `glCreateProgram`, `glDeleteProgram`, `glIsProgram`, `glLinkProgram`, `glUseProgram`, `glValidateProgram` | B | Matching program lifecycle and validation calls. |
| `glBindAttribLocation`, `glGetAttribLocation` | B | Matching linked-attribute APIs. |
| `glBindBuffer`, `glGenBuffers`, `glDeleteBuffers`, `glIsBuffer`, `glBufferData`, `glBufferSubData` | B | Array/element buffers; replacement upload is transactional. |
| `glBindFramebuffer`, `glGenFramebuffers`, `glDeleteFramebuffers`, `glIsFramebuffer`, `glCheckFramebufferStatus`, `glFramebufferTexture2D`, `glFramebufferRenderbuffer` | B | One complete bounded FBO profile; 2D attachments only, including RGBA/FLOAT and RGBA32F only after an embedding enables its WebGL Float-color policy. |
| `glBindRenderbuffer`, `glGenRenderbuffers`, `glDeleteRenderbuffers`, `glIsRenderbuffer`, `glRenderbufferStorage` | B | Bounded RGBA8/RGBA32F/native packed/D32/D24S8/S8 storage. |
| `glBindTexture`, `glGenTextures`, `glDeleteTextures`, `glIsTexture` | B | `TEXTURE_2D` only. |
| `glBlendColor`, `glBlendEquation`, `glBlendEquationSeparate`, `glBlendFunc`, `glBlendFuncSeparate` | B | Native RinGPU blend state; invalid factors/equations reject. Finite blend constants are clamped unless an embedding has enabled the Float-color gate and the current target is RGBA32F. `MIN`/`MAX` require the embedding's `EXT_blend_minmax` gate. |
| `glClear`, `glClearColor`, `glClearDepthf`, `glClearStencil`, `glColorMask` | B | Current complete target; color/depth/stencil masks follow the bounded FBO profile. |
| `glCompileShader`, `glCreateShader`, `glDeleteShader`, `glIsShader`, `glShaderSource` | B | GLSL ES source is compiled only by RinGL's bounded RSH1 profile; exact global `precision lowp`/`mediump`/`highp` declarations for `float`/`int`/`sampler2D` map to binary32 RSH1. No-varying shaders execute local `float`/`int`, scalar `bool`, `vec2`/`vec3`/`vec4`, `ivec2`/`ivec3`/`ivec4`, and `bvec2`/`bvec3`/`bvec4` values with same-type numeric arithmetic, single-component swizzles, explicit `int(...)`/`float(...)` conversion, a vertex `gl_PointSize` scalar, and fragment `gl_PointCoord`. Boolean values support scalar or component `if` selection, `not`, matching-width `equal`/`notEqual`, `any`/`all` on `bvec2`/`bvec3`/`bvec4`, and scalar `!`/`&&`/`^^`/`||` with normal precedence. These lower into scalar RSH1 i32 comparison/addition/multiply instructions, not an embedding command path; side-effecting condition expressions remain outside the profile. `gl_PointCoord` is emitted as RSH1 builtin Float32 X/Y (not a user varying), is supplied only for point-list fragments with GLES/WebGL upper-left orientation, and is accepted both in normal vector expressions and bounded `texture2D(sampler uniform, gl_PointCoord)`. Existing structural varying profiles also accept a finite literal, `uniform float`, a dedicated `attribute float` point-size input, or a profile color-attribute component optionally followed by one finite right-hand `+`, `-`, `*`, or nonzero `/` literal `gl_PointSize` assignment; uniform updates rebuild the program-owned vertex RSH1 module and scalar/color-attribute arithmetic executes from live vertex registers. A WebGL-enabled context additionally accepts the exact `GL_OES_standard_derivatives` directive and `dFdx`/`dFdy`/`fwidth` only for the documented finite varying/arithmetic profile. |
| `glCompressedTexImage2D`, `glCompressedTexSubImage2D` | P | `RINGL_ETC1_RGB8_OES`, linear RGB/RGBA DXT1/RGBA DXT3/RGBA DXT5, and their sRGB S3TC forms only. Exact block spans decode before the regular RinGL/RinGPU texture path; sRGB RGB becomes linear Float32 and alpha stays linear. Logical compressed images reject uncompressed mutation/generated mips and are never renderable FBO attachments. Unsupported formats, short spans, invalid subimage block alignment/ranges, and failed allocation/decode leave the existing texture unchanged. |
| `glCopyTexImage2D`, `glCopyTexSubImage2D` | B | Complete color target only; canonical, native packed, Float32, and binary16 2D storage with checked ranges. Copy snapshots use the fenced RinGPU readback boundary and convert into the defined destination representation only after the full source succeeds. A nonzero packed mip retains its exact WebGL packed upload type; an attached replacement texture re-realizes as a RinGPU color target. The sync trace executes the public clear→definition→copy→clear→finish→readback sequence and proves that an invalid source range adds no driver event. |
| `glCullFace`, `glFrontFace` | B | Current native raster-state profile. |
| `glDepthFunc`, `glDepthMask`, `glDepthRangef` | B | Finite `[0,1]` depth-range endpoints; reversed ranges remain valid. |
| `glDisable`, `glEnable`, `glIsEnabled` | B | Blend, cull, depth, scissor, polygon offset, sample coverage, and stencil only. |
| `glDisableVertexAttribArray`, `glEnableVertexAttribArray`, `glVertexAttribPointer` | B | Numeric array formats and checked captured buffers only. |
| `glDrawArrays`, `glDrawElements` | B | All seven listed primitive topologies; bounded RSH1/linker and validated buffers. |
| `glFinish`, `glFlush` | B | Immediate submission; `finish` uses the optional fenced-sync extension. |
| `glGenerateMipmap` | B | Bounded 2D canonical/native packed color chains only; logical `EXT_sRGB` textures reject rather than deriving an unspecified sRGB chain. |
| `glGetActiveAttrib`, `glGetActiveUniform` | P | Versioned `RinGLActiveInfoV1`; current linker exposes bounded attributes plus `sampler2D`, scalar/vector float, signed integer, and Boolean (`bool`/`bvec2`/`bvec3`/`bvec4`) uniforms, and vertex `mat2`/`mat3`/`mat4` uniforms. |
| `glGetAttachedShaders` | B | `ringl_get_attached_shaders` copies the pending vertex/fragment names in deterministic order, including delete-pending shaders retained by a program. |
| `glGetBooleanv`, `glGetFloatv` | P | Dedicated state snapshots exist; no generic typed getter. |
| `glGetBufferParameteriv` | P | `ringl_get_buffer_size` / `ringl_get_buffer_usage` only. |
| `glGetError` | B | `ringl_get_error`. |
| `glGetFramebufferAttachmentParameteriv` | P | Versioned attachment record plus logical `EXT_sRGB` color-encoding state; supported attachments and fields only. |
| `glGetIntegerv` | P | `ringl_get_integerv_bounded` and the target-sensitive `ringl_get_implementation_color_read_format_type` accept only the inventory below, including default drawing-buffer component/depth/stencil bit counts derived from real configured planes; `PACK_ALIGNMENT`, `IMPLEMENTATION_COLOR_READ_{FORMAT,TYPE}`, `MAX_RENDERBUFFER_SIZE`/`MAX_VIEWPORT_DIMS`, single-sample counts, and the executable `[1, 64]` point range are actual bounded profile values. |
| `glGetProgramiv`, `glGetShaderiv` | P | Versioned program record and dedicated shader-status/type calls; not every GLES pname. |
| `glGetProgramInfoLog`, `glGetShaderInfoLog`, `glGetShaderSource` | B | Caller-owned bounded copies. |
| `glGetRenderbufferParameteriv` | P | `RinGLRenderbufferInfoV1` supplies bounded storage metadata, including logical `SRGB8_ALPHA8_EXT` renderbuffers. |
| `glGetShaderPrecisionFormat` | B | `ringl_get_shader_precision_format` returns the executable RSH1 binary32 or signed-i32 profile through a validated versioned record. |
| `glGetString` | P | `ringl_get_string` returns only RinGL's static vendor, renderer, bounded-profile version, and RSH1 language-profile strings; extension strings remain unavailable. |
| `glGetTexParameteriv` | P | Integer values for `MIN_FILTER`, `MAG_FILTER`, `WRAP_S`, `WRAP_T` only. |
| `glGetTexParameterfv` | P | `ringl_get_tex_parameterf` exposes only the gated `TEXTURE_MAX_ANISOTROPY_EXT` value; it does not manufacture float views of enum-valued sampler state. |
| `glGetUniformiv` | P | `ringl_get_uniform_1i` for linked scalar `sampler2D`, `int`, or `bool`, plus `ringl_get_uniform_{2,3,4}i` for complete `ivec` or normalized `bvec` values; no arrays. |
| `glGetUniformfv` | P | `ringl_get_uniform_{1,2,3,4}f` for linked `float`/`vec2`/`vec3`/`vec4` locations, and `ringl_get_uniform_matrix{2,3,4}f` for bounded vertex square-matrix profiles. |
| `glGetUniformLocation` | P | Linked `sampler2D`, scalar/vector float, signed integer, scalar/vector Boolean, and bounded vertex `mat2`/`mat3`/`mat4` uniforms only. |
| `glGetVertexAttribfv`, `glGetVertexAttribiv`, `glGetVertexAttribPointerv` | P | Versioned attribute record/current-value copy; no generic GLES getter. |
| `glHint` | P | `GENERATE_MIPMAP_HINT` is advisory. `FRAGMENT_SHADER_DERIVATIVE_HINT` is stored/queryable only after the WebGL `OES_standard_derivatives` context gate; it does not claim a general driver-quality control API. |
| `glLineWidth`, `glPolygonOffset`, `glSampleCoverage`, `glScissor`, `glViewport` | B | Bounded native raster state; viewport dimensions above the 4096 image limit reject without mutating state. |
| `glPixelStorei` | P | `PACK_ALIGNMENT` and `UNPACK_ALIGNMENT` values 1, 2, 4, 8 only. |
| `glReadPixels` | P | Current complete color target, bounded `RGBA`/`UNSIGNED_BYTE` for UNORM/packed and logical sRGB storage, and `RGBA`/`FLOAT` only for real float storage; logical sRGB byte readback re-encodes RGB, retains `SRGB_ALPHA_EXT` alpha, and returns one for alpha-less `SRGB_EXT`. The byte-span API enforces PACK row padding, clips out-of-framebuffer rectangles, and preserves the corresponding destination bytes for untrusted callers. |
| `glReleaseShaderCompiler`, `glShaderBinary` | N | Shader compiler lifetime/binary shader formats are not implemented. |
| `glStencilFunc`, `glStencilFuncSeparate`, `glStencilMask`, `glStencilMaskSeparate`, `glStencilOp`, `glStencilOpSeparate` | B | Bounded native D24S8/S8 paths; remaining attachment semantics stay outside the profile. |
| `glTexImage2D`, `glTexSubImage2D` | B | 2D canonical/packed color plus bounded depth formats; exact canonical `FLOAT` color uploads use RGBA32F sampled storage with nearest-only completeness. Logical `SRGB_EXT`/`SRGB_ALPHA_EXT` accept exact unsigned-byte input only and decode RGB into linear RGBA32F storage. Byte-span variants protect untrusted imports. |
| `glTexParameterf` | P | `ringl_tex_parameterf` accepts only finite `TEXTURE_MAX_ANISOTROPY_EXT` after the context-local anisotropy gate; it preserves failure atomicity and clamps only the extension-defined upper bound. |
| `glTexParameterfv`, `glTexParameteriv` | N | Raw RinGL has no unbounded pointer-vector entry points; an embedding must validate a complete caller span and dispatch the corresponding scalar setter. |
| `glTexParameteri` | P | Four sampler pnames only; all accepted values map to RinGPU sampler state. |
| `glUniform1i` | P | Linked `sampler2D`, scalar `int`, or scalar `bool` location; Boolean writes normalize zero/non-zero to `0`/`1`; no arrays. |
| `glUniform1f`, `glUniform1fv`, `glUniform2f`, `glUniform2fv`, `glUniform3f`, `glUniform3fv`, `glUniform4f`, `glUniform4fv` | P | Finite scalar/vector values realize a program-owned RinGPU module only for stages that declare the updated uniform name, preserving independent sampler/varying modules. One unswizzled fragment `uniform vec2` can be the right-hand finite operand of the bounded varying-coordinate `+`, `-`, or `*` texture forms; its two values are atomically rematerialized as fragment RSH1 constants before the native RinGPU draw. The bounded vertex-colored texture material may sample one UV/image pair, or add two pairs, then apply a `vec4` tint and scalar opacity in RSH1 (`(texture2D(...) + texture2D(...)) * vertexColor * tint * opacity`); an embedding may map each `*fv` form only when its single non-array value is complete. |
| `glUniform1iv` | P | One linked scalar `sampler2D` or `int` value; arrays are not implemented. |
| `glUniformMatrix2fv`, `glUniformMatrix3fv` | P | One vertex-stage `uniform mat2`/`mat3` multiplied by a same-width `vec2`/`vec3` in the generic no-varying profile. RinGL materializes the column-major finite values as scalar RSH1 constants and executes the dot products in RinGPU; `transpose == false`, no uniform arrays, matrix arithmetic, or varying/texture-specialized matrix profiles. |
| `glUniformMatrix4fv` | P | One vertex-stage `uniform mat4` used as `mat4 * attribute vec4` for `gl_Position`, either in the no-varying vector-local/arithmetic profile or a transformed texture profile that copies one to eight declared `attribute vec2` values into matching `varying vec2` pairs for fragment `texture2D`; `attribute vec2` position may be explicitly constructed as `vec4(position, 0.0, 1.0)`. Column-major finite values only, `transpose == false`, no uniform arrays. |
| `glUniform2i`, `glUniform2iv`, `glUniform3i`, `glUniform3iv`, `glUniform4i`, `glUniform4iv` | P | One exact-width linked `ivec2`/`ivec3`/`ivec4` or `bvec2`/`bvec3`/`bvec4` value; Boolean components normalize non-zero to `1`, no arrays. Program-owned RSH1 replacement is atomic. |
| `glVertexAttrib1f`, `glVertexAttrib2f`, `glVertexAttrib3f`, `glVertexAttrib4f` | B | Current generic values for disabled arrays. |
| `glVertexAttrib1fv`, `glVertexAttrib2fv`, `glVertexAttrib3fv`, `glVertexAttrib4fv` | N | Raw pointer-vector forms are not exported; embeddings must bounds-check then use scalar setters. |

## Enums, object formats, and limits

The public header intentionally exposes only tokens which have a defined RinGL
meaning. The 304 GLES 2.0 registry macros were audited by class rather than
inventing unused aliases.

| GLES enum class | Status | Inventory boundary |
| --- | --- | --- |
| Scalars, primitive modes, compare/stencil operations, blend factors/equations, face/cull, clear-mask bits | B | Public `RINGL_*` tokens map to validated state and RinGPU descriptors. |
| Buffer targets/usages and numeric vertex types | B | `ARRAY_BUFFER`/`ELEMENT_ARRAY_BUFFER`; supported scalar source formats are documented by `ringl_vertex_attrib_pointer`. |
| 2D texture targets, canonical `ALPHA`/`RGB`/`RGBA`/`LUMINANCE`/`LUMINANCE_ALPHA`, native RGB565/RGBA4/RGB5_A1, logical `SRGB_EXT`/`SRGB_ALPHA_EXT`, filters and wraps | P | `TEXTURE_2D` only. `SRGB8_ALPHA8_EXT` is renderbuffer storage, not a texture token. ETC1, four linear S3TC DXT, and four sRGB S3TC DXT formats are accepted only by dedicated bounded uploads and expanded to RGB/RGBA/linear Float32; no cube map, other compressed formats, 3D, array, or immutable storage. |
| Depth/stencil and renderbuffer formats | P | D16/D32/D24S8/S8 are bounded FBO formats; multisample/resolve and remaining attachment semantics are absent. |
| Shader/program type and status tokens | P | Current GLSL/RSH1 subset exposes scalar/vector attributes, samplers, scalar/vector float, signed-integer, and Boolean uniforms. The no-varying profile executes `not`, matching-width `equal`/`notEqual`, `any`/`all`, and scalar `!`/`&&`/`^^`/`||` for bounded Boolean conditions, but not multi-component Boolean swizzles or side-effecting/general control-flow expressions. Bounded vertex `mat2 * vec2`, `mat3 * vec3`, and `mat4 * vec4` position profiles are available. The transformed texture profile remains `mat4` only: up to eight direct UV pairs (sixteen scalar fragment inputs), or one/two UV pairs plus an RGBA varying used by exact one-sample or two-sample-add `texture2D(...) * vertexColor` materials with optional `vec4` tint and scalar opacity; no general uniform type coverage. |
| Query tokens | P | Only the exact `ringl_get_integerv_bounded` and dedicated-record names below are accepted. |
| Cube-map, other compressed-texture, shader-binary, precision, implementation/vendor/renderer/version/extension, multisample, and GLES 3.x tokens | N | They are not declared as successful RinGL capabilities. |

Implemented fixed limits are `MAX_VERTEX_ATTRIBS = 16`,
`MAX_TEXTURE_SIZE = 4096`, and `MAX_TEXTURE_IMAGE_UNITS =
MAX_COMBINED_TEXTURE_IMAGE_UNITS = 8`. They are real bounded limits, not a
claim of the GLES-required minimums or of conformance.

## Query inventory

`ringl_get_integerv_bounded()` is the only generic integer query entry. It
requires enough caller-owned elements and leaves its output unchanged on
failure. It accepts these exact pnames:

- four values: `COLOR_WRITEMASK`, `VIEWPORT`, `SCISSOR_BOX`;
- object/binding and limits: `ARRAY_BUFFER_BINDING`,
  `ELEMENT_ARRAY_BUFFER_BINDING`, `ACTIVE_TEXTURE`, `TEXTURE_BINDING_2D`,
  `FRAMEBUFFER_BINDING`, `RENDERBUFFER_BINDING`, `CURRENT_PROGRAM`,
  `PACK_ALIGNMENT`, `UNPACK_ALIGNMENT`, `MAX_TEXTURE_SIZE`,
  `MAX_TEXTURE_IMAGE_UNITS`,
  `MAX_COMBINED_TEXTURE_IMAGE_UNITS`, `MAX_VERTEX_ATTRIBS`;
- default drawing-buffer component/plane counts: `RED_BITS`, `GREEN_BITS`,
  `BLUE_BITS`, `ALPHA_BITS`, `DEPTH_BITS`, `STENCIL_BITS`. Component counts
  come from the configured native color format; depth/stencil counts are zero
  when the explicit browser-facing aspect contract hides that logical plane;
- `WEBGL_draw_buffers` (only after its RinGL gate):
  `MAX_DRAW_BUFFERS_WEBGL`, `MAX_COLOR_ATTACHMENTS_WEBGL`, and
  `DRAW_BUFFER0_WEBGL` through `DRAW_BUFFER3_WEBGL`. The returned mapping is
  stored per framebuffer; a zero-length custom list and default `[NONE]` use
  a zero effective color-write mask without suppressing a real depth/stencil
  pass. A directive-free `gl_FragColor` program rejects an active nonzero slot
  before backend submission when any color channel is writable; a zero channel
  mask preserves its ordinary ABI for depth/stencil work. A `gl_FragData`
  program uses the MRT ABI even with just one active attachment;
- raster/depth/stencil/blend state: `CULL_FACE_MODE`, `FRONT_FACE`,
  `DEPTH_FUNC`, `DEPTH_WRITEMASK`, front/back stencil function/reference/masks
  and operations, blend source/destination/equation RGB/alpha, and
  `SAMPLE_COVERAGE_INVERT`.

Dedicated versioned records provide depth range, line width/range, sample
coverage, blend color, clear values, shader precision, renderbuffer metadata,
framebuffer attachment metadata, program status, active names, and
vertex-attribute state. `ringl_get_string()` separately provides only the four
explicit core identity strings; it does not fabricate extension support.
Every other GLES query must remain unavailable until it has an equally explicit
RinGL representation and test coverage.

## Embedding rule

Ladybird/WebGL code may adapt an API only when this inventory marks it **B** or
when it applies the listed **P** restriction before calling RinGL. **N** means
the API is unavailable: it must produce the WebGL/GLES validation error or stay
unexposed, never return a synthetic success value. This rule keeps browser
validation policy above raw RinGL while preserving RinGL's reusable native GL
boundary.
