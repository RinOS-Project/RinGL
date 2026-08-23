# GLES 2.0 API inventory and status

This is the source-of-truth inventory for RinGL's GLES 2.0-shaped API. It was
audited against the 142 `gl*` entry points in the local GLES 2.0 `gl2.h`
registry and against `include/ringl/ringl.h` plus `ringl_sync.h`.

It deliberately distinguishes a real, bounded RinGL implementation from an
absent API. It does not claim GLES 2.0 conformance, nor does it authorize an
embedding to expose a WebGL feature that RinGL cannot execute.

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
| `glBindFramebuffer`, `glGenFramebuffers`, `glDeleteFramebuffers`, `glIsFramebuffer`, `glCheckFramebufferStatus`, `glFramebufferTexture2D`, `glFramebufferRenderbuffer` | B | One complete bounded FBO profile; 2D texture attachments only. |
| `glBindRenderbuffer`, `glGenRenderbuffers`, `glDeleteRenderbuffers`, `glIsRenderbuffer`, `glRenderbufferStorage` | B | Bounded RGBA8/native packed/D32/D24S8/S8 storage. |
| `glBindTexture`, `glGenTextures`, `glDeleteTextures`, `glIsTexture` | B | `TEXTURE_2D` only. |
| `glBlendColor`, `glBlendEquation`, `glBlendEquationSeparate`, `glBlendFunc`, `glBlendFuncSeparate` | B | Native RinGPU blend state; invalid factors/equations reject. |
| `glClear`, `glClearColor`, `glClearDepthf`, `glClearStencil`, `glColorMask` | B | Current complete target; color/depth/stencil masks follow the bounded FBO profile. |
| `glCompileShader`, `glCreateShader`, `glDeleteShader`, `glIsShader`, `glShaderSource` | B | GLSL ES source is compiled only by RinGL's bounded RSH1 profile; exact global `precision lowp`/`mediump`/`highp` declarations for `float`/`int`/`sampler2D` map to binary32 RSH1, and no-varying shaders execute local `vec2`/`vec3`/`vec4` values, same-width `+`/`-`, unary `-`, and scalar-broadcast `*`/`/`. |
| `glCompressedTexImage2D`, `glCompressedTexSubImage2D` | N | Compressed texture storage is not implemented. |
| `glCopyTexImage2D`, `glCopyTexSubImage2D` | B | Complete color target only; canonical/packed 2D formats and checked ranges. |
| `glCullFace`, `glFrontFace` | B | Current native raster-state profile. |
| `glDepthFunc`, `glDepthMask`, `glDepthRangef` | B | Finite `[0,1]` depth-range endpoints; reversed ranges remain valid. |
| `glDisable`, `glEnable`, `glIsEnabled` | B | Blend, cull, depth, scissor, polygon offset, sample coverage, and stencil only. |
| `glDisableVertexAttribArray`, `glEnableVertexAttribArray`, `glVertexAttribPointer` | B | Numeric array formats and checked captured buffers only. |
| `glDrawArrays`, `glDrawElements` | B | All seven listed primitive topologies; bounded RSH1/linker and validated buffers. |
| `glFinish`, `glFlush` | B | Immediate submission; `finish` uses the optional fenced-sync extension. |
| `glGenerateMipmap` | B | Bounded 2D canonical/native packed color chains only. |
| `glGetActiveAttrib`, `glGetActiveUniform` | P | Versioned `RinGLActiveInfoV1`; current linker exposes bounded attributes, `sampler2D`, and direct `vec4` uniforms. |
| `glGetAttachedShaders` | N | Raw RinGL has no attached-shader list API. |
| `glGetBooleanv`, `glGetFloatv` | P | Dedicated state snapshots exist; no generic typed getter. |
| `glGetBufferParameteriv` | P | `ringl_get_buffer_size` / `ringl_get_buffer_usage` only. |
| `glGetError` | B | `ringl_get_error`. |
| `glGetFramebufferAttachmentParameteriv` | P | Versioned attachment record; supported attachments and fields only. |
| `glGetIntegerv` | P | `ringl_get_integerv_bounded` accepts only the inventory below. |
| `glGetProgramiv`, `glGetShaderiv` | P | Versioned program record and dedicated shader-status/type calls; not every GLES pname. |
| `glGetProgramInfoLog`, `glGetShaderInfoLog`, `glGetShaderSource` | B | Caller-owned bounded copies. |
| `glGetRenderbufferParameteriv` | P | `RinGLRenderbufferInfoV1` supplies bounded storage metadata. |
| `glGetShaderPrecisionFormat` | N | Precision-format reflection is not implemented in raw RinGL. |
| `glGetString` | N | Vendor/renderer/version/extensions strings are not fabricated. |
| `glGetTexParameteriv` | P | Integer values for `MIN_FILTER`, `MAG_FILTER`, `WRAP_S`, `WRAP_T` only. |
| `glGetTexParameterfv` | N | No floating tex-parameter getter. |
| `glGetUniformiv` | P | `ringl_get_uniform_1i` for linked scalar `sampler2D` locations only. |
| `glGetUniformfv` | P | `ringl_get_uniform_{1,2,3,4}f` for linked `float`/`vec2`/`vec3`/`vec4` locations, and `ringl_get_uniform_matrix4f` for the bounded vertex `mat4` profile. |
| `glGetUniformLocation` | P | Linked `sampler2D`, `float`, `vec2`, `vec3`, `vec4`, and bounded vertex `mat4` uniforms only. |
| `glGetVertexAttribfv`, `glGetVertexAttribiv`, `glGetVertexAttribPointerv` | P | Versioned attribute record/current-value copy; no generic GLES getter. |
| `glHint` | P | Tracks accepted GLES hint enums; no general driver-quality control. |
| `glLineWidth`, `glPolygonOffset`, `glSampleCoverage`, `glScissor`, `glViewport` | B | Bounded native raster state. |
| `glPixelStorei` | P | `UNPACK_ALIGNMENT` values 1, 2, 4, 8 only. |
| `glReadPixels` | P | Current complete color target, tightly packed `RGBA`/`UNSIGNED_BYTE`; bounded byte-span API required for untrusted callers. |
| `glReleaseShaderCompiler`, `glShaderBinary` | N | Shader compiler lifetime/binary shader formats are not implemented. |
| `glStencilFunc`, `glStencilFuncSeparate`, `glStencilMask`, `glStencilMaskSeparate`, `glStencilOp`, `glStencilOpSeparate` | B | Bounded native D24S8/S8 paths; remaining attachment semantics stay outside the profile. |
| `glTexImage2D`, `glTexSubImage2D` | B | 2D canonical/packed color plus bounded depth formats; byte-span variants protect untrusted imports. |
| `glTexParameterf`, `glTexParameterfv`, `glTexParameteriv` | N | Raw RinGL only has integer `ringl_tex_parameteri`; an embedding may accept a float only after exact integer conversion. |
| `glTexParameteri` | P | Four sampler pnames only; all accepted values map to RinGPU sampler state. |
| `glUniform1i` | P | Linked `sampler2D` locations only. |
| `glUniform1f`, `glUniform1fv`, `glUniform2f`, `glUniform2fv`, `glUniform3f`, `glUniform3fv`, `glUniform4f`, `glUniform4fv` | P | Finite scalar/vector values realize a program-owned RinGPU module for linked `uniform float`/`vec2`/`vec3`/`vec4` source; an embedding may map each `*fv` form only when its single non-array value is complete. |
| `glUniform1iv` | P | A linked scalar `sampler2D` value only; sampler arrays and integer GLSL uniforms are not implemented. |
| `glUniformMatrix4fv` | P | One vertex-stage `uniform mat4` used as `mat4 * attribute vec4` for `gl_Position`; this may feed the bounded no-varying vector-local/arithmetic profile. Column-major finite values only, `transpose == false`, no uniform arrays. |
| `glUniform2i`, `glUniform2iv`, `glUniform3i`, `glUniform3iv`, `glUniform4i`, `glUniform4iv`, `glUniformMatrix2fv`, `glUniformMatrix3fv` | N | Integer and remaining matrix uniform storage/lowering are not implemented. |
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
| 2D texture targets, canonical `ALPHA`/`RGB`/`RGBA`/`LUMINANCE`/`LUMINANCE_ALPHA`, native RGB565/RGBA4/RGB5_A1, filters and wraps | P | `TEXTURE_2D` only; no cube map, compressed formats, 3D, array, or immutable storage. |
| Depth/stencil and renderbuffer formats | P | D16/D32/D24S8/S8 are bounded FBO formats; multisample/resolve and remaining attachment semantics are absent. |
| Shader/program type and status tokens | P | Current GLSL/RSH1 subset exposes scalar/vector attributes, samplers, scalar/vector uniforms, and the bounded vertex `mat4 * vec4` position profile; no general uniform type coverage. |
| Query tokens | P | Only the exact `ringl_get_integerv_bounded` and dedicated-record names below are accepted. |
| Cube-map, compressed-texture, shader-binary, precision, implementation/vendor/renderer/version/extension, multisample, and GLES 3.x tokens | N | They are not declared as successful RinGL capabilities. |

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
  `UNPACK_ALIGNMENT`, `MAX_TEXTURE_SIZE`, `MAX_TEXTURE_IMAGE_UNITS`,
  `MAX_COMBINED_TEXTURE_IMAGE_UNITS`, `MAX_VERTEX_ATTRIBS`;
- raster/depth/stencil/blend state: `CULL_FACE_MODE`, `FRONT_FACE`,
  `DEPTH_FUNC`, `DEPTH_WRITEMASK`, front/back stencil function/reference/masks
  and operations, blend source/destination/equation RGB/alpha, and
  `SAMPLE_COVERAGE_INVERT`.

Dedicated versioned records provide depth range, line width/range, sample
coverage, blend color, clear values, renderbuffer metadata, framebuffer
attachment metadata, program status, active names, and vertex-attribute state.
Every other GLES query must remain unavailable until it has an equally explicit
RinGL representation and test coverage.

## Embedding rule

Ladybird/WebGL code may adapt an API only when this inventory marks it **B** or
when it applies the listed **P** restriction before calling RinGL. **N** means
the API is unavailable: it must produce the WebGL/GLES validation error or stay
unexposed, never return a synthetic success value. This rule keeps browser
validation policy above raw RinGL while preserving RinGL's reusable native GL
boundary.
