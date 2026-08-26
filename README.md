# RinGL

RinGL is the RinOS OpenGL/OpenGL ES compatibility and translation layer.

Its job is to preserve OpenGL-style state and object semantics at the API boundary, then translate resolved draw, resource, synchronization, and presentation work into the native RinGPU contract used by RinOS.

> [!IMPORTANT]
> RinGL is an early-stage project. The initial implementation target is a bounded OpenGL ES 2.0-style graphics path. It is not currently a conformant OpenGL or OpenGL ES implementation.

## Building

Meson remains the standalone test build. RinGL also provides a CMake `RinGL::RinGL`
static target with the identical translation-unit list, warning policy, public
headers, and transitive C math dependency. This lets the Ladybird `AK_OS_RINOS`
target embed the tested RinGL implementation instead of depending on an
unresolved external archive at final link time.

## Architecture

```text
OpenGL ES application                 Browser / WebGL implementation
          |                                      |
          +------------------+-------------------+
                             |
                           RinGL
                             |
             +---------------+----------------+
             |                                |
       GL state tracker                  GLSL ES frontend
       GL object model                        |
       validation/errors                 RinShader IR
       pipeline cache                        |
       command encoder                       |
             +---------------+----------------+
                             |
                           RinGPU
                             |
                      driver backend
                             |
                            GPU
```

RinGL is deliberately above RinGPU. RinGPU remains the Rin-native explicit GPU boundary; it does not expose OpenGL objects or OpenGL's implicit state machine. RinGL owns that compatibility state and resolves it into validated RinGPU objects and commands.

A typical draw path is expected to look like this:

```text
glUseProgram / glBindBuffer / glEnable / ...
                    |
              update GLContext
                    |
              glDrawElements
                    |
          resolve current GL state
                    |
       lookup/create RinGPU pipeline
                    |
        encode render pass + bindings
                    |
          encode RinGPU indexed draw
                    |
                 submit
```

Calls that only mutate OpenGL state should normally update the current `GLContext` and mark derived state dirty. RinGPU command generation is deferred until an operation such as a draw, clear, transfer, readback, flush, or present requires the state to become observable.

## Responsibilities

RinGL owns the OpenGL-facing parts of the stack:

- OpenGL ES entry points and context lifecycle;
- GL object namespaces and lifetime rules for buffers, textures, shaders, programs, framebuffers, renderbuffers, vertex arrays, and related objects;
- OpenGL state tracking, validation, and error reporting;
- conversion of implicit GL state into explicit RinGPU resources, pipelines, render passes, bindings, transitions, barriers, and submissions;
- caching immutable RinGPU objects derived from mutable GL state;
- GLSL ES compilation/linking through a RinShader-compatible frontend;
- GL-visible synchronization and readback semantics;
- a reusable GLES-facing layer suitable for a future WebGL implementation.

RinGL does **not** move OpenGL entry points into the kernel, expose vendor GPU ABIs, or redefine RinGPU around GL concepts.

## Shader path

Aquamarine Shader Language and GLSL ES are separate source frontends. RinGL should not need to translate GLSL text into Aquamarine source text.

The intended shader path is:

```text
Aquamarine source -> aqc ----------+
                                  |
                                  v
                              RinShader IR -> RinGPU
                                  ^
                                  |
GLSL ES source -> RinGL GLSL frontend + linker
```

This keeps RinShader IR as the common validated shader boundary while allowing each source language to preserve its own semantics.

The generic WebGL-facing varying route remains inside that same boundary:
matching `float`/`vec2`/`vec3`/`vec4` declarations are flattened into scalar RSH1
interpolants (one slot per `float` declaration), and a vertex shader may initialize vector declarations as a whole vector or
through non-overlapping `xyzw`/`rgba`/`stpq` lvalue selectors. RinGL writes
those selected components directly to the native output slots and refuses to
link if any declared component is unwritten; it does not ask Aquamarine or a
browser embedding to repair a partial interface.

## WebGL standard derivatives

`OES_standard_derivatives` is a context-local WebGL capability, not a separate
surface backend.  A browser embedding enables it only after returning the
extension object, then RinGL accepts the exact fragment-source directive
`#extension GL_OES_standard_derivatives : enable` or `require`.  `dFdx`,
`dFdy`, and `fwidth` lower to scalar RSH1 derivative opcodes and execute through
the same generic RinGPU software backend as every other RinGL draw; the
Aquamarine surface is caller-owned storage only. The implemented profile covers finite float/vecN values formed
from fragment varyings and arithmetic; it rejects texture-sample derivative
expressions, general control flow, and fine/coarse variants rather than
inventing their semantics. General control flow and derivatives within the bounded
conditional profile remain rejected. `FRAGMENT_SHADER_DERIVATIVE_HINT` is observable only after
the same context gate.  This is a bounded WebGL slice, not an OpenGL ES
conformance claim.

## Bounded fragment discard

The GLSL ES frontend executes `discard;` as a terminal RSH1 instruction in the
generic RinGPU backend. It also supports one WebGL-useful conditional form: an
`if` with a bounded scalar Boolean expression and a mandatory `else` may
discard on exactly one fragment branch when the other writes the complete
`gl_FragColor` vector. The branch remains native RSH1 `JUMP_IF`/`JUMP` control
flow; the private Aquamarine surface only owns caller-provided storage and does
not choose the condition or emulate alpha testing. Both-discard branches,
nested control flow, loops, and partial output writes are rejected before a
module can be published.

## WebGL compressed textures

`RINGL_ETC1_RGB8_OES`, the four linear S3TC DXT formats, and the four sRGB
S3TC DXT formats are bounded WebGL-facing compressed-texture slices, not
native compressed RinGPU storage.
`ringl_compressed_tex_image_2d_from_bytes()` and
`ringl_compressed_tex_sub_image_2d_from_bytes()` accept exactly the format's
block span, decode to normal RGB/RGBA before using the ordinary RinGL texture
upload path, and preserve the old definition on source, alignment, range,
allocation, or decode failure. sRGB DXT turns only RGB through the IEC
61966-2-1 EOTF into Float32 before sampling; alpha remains linear. The
decoded resource retains its logical compressed format: it cannot receive an
uncompressed update, generated mip chain, or renderable FBO attachment. A
browser exposes the formats only after its corresponding ETC1, S3TC, or
sRGB-S3TC extension object is acquired; RinGL/RinGPU and the private
Aquamarine embedding therefore receive only normal texture resources. Other
compressed formats remain unavailable.

## WebGL `EXT_sRGB`

`RINGL_SRGB_EXT` and `RINGL_SRGB_ALPHA_EXT` are logical WebGL texture formats
for exact `UNSIGNED_BYTE` image/subimage input. `RINGL_SRGB8_ALPHA8_EXT` is a
logical renderbuffer-storage format only; it is intentionally not accepted by
the texture upload APIs. RinGL decodes sRGB RGB through IEC 61966-2-1 into
linear RGBA32F RinGPU storage. `SRGB_ALPHA_EXT` retains linear alpha, whereas
alpha-less `SRGB_EXT` keeps the physical alpha one and never exposes it. Its
framebuffer metadata therefore reports the logical sRGB encoding and
unsigned-byte component type, and byte readback re-encodes only RGB. Mipmap generation for a
logical sRGB texture rejects instead of creating a chain with unspecified
transfer semantics. A browser must gate these tokens through its acquired
`EXT_sRGB` object; RinGL itself remains on the RinGPU adapter/private
Aquamarine embedding route.

## RinGPU translation model

OpenGL exposes mutable, implicit state while RinGPU is intentionally explicit. RinGL therefore maintains a context-side state machine and derives backend objects lazily.

For example, blend state, depth state, the linked program, vertex layout, attachment formats, and other draw-relevant state can form a pipeline-cache key. Changing `glEnable(GL_BLEND)` should invalidate the derived pipeline state rather than immediately emit a GPU command. The blend key includes the RGBA `ringl_blend_color()` value after target-aware normalization: it retains finite values outside `[0,1]` only after the browser's Float-color gate and only for an RGBA32F target; fixed-point targets use the clamped value. Constant blend factors use an additive native-pipeline V2 descriptor and optional callback tail, preserving the V1 descriptor for existing RinGPU embeddings; an embedding without that V2 path rejects the draw instead of substituting a zero blend constant.

OpenGL also has no explicit render-pass API. RinGL is expected to open and close RinGPU render passes around compatible framebuffer operations and end them when an incompatible operation requires it.

## Software rendering

RinOS already contains the `libs/aquamarine` 2D/3D software graphics library. It is distinct from Aquamarine Shader Language.

A future RinGL software backend may reuse or evolve that library, but the primary RinGL architecture should not depend on the current fixed-function software rasterizer. The hardware path is:

```text
RinGL -> RinGPU -> driver backend -> GPU
```

A software fallback can remain a separate backend choice rather than leaking software-rasterizer details into the GL API layer.

## Initial target

The first useful milestone is intentionally small:

```text
create context
  -> compile/link minimal GLSL ES shaders
  -> create/upload vertex buffer
  -> configure vertex attributes
  -> clear a framebuffer
  -> draw one non-indexed triangle
  -> present through RinGPU
```

After that vertical slice is stable, indexed drawing, textures, framebuffer objects, blending/depth/stencil, synchronization, readback, and broader GLES compatibility can be added incrementally.

## WebGL 1 vertex arrays and instancing

RinGL implements the state required by WebGL 1
`OES_vertex_array_object`: a default unnamed VAO and bounded generated VAO
names capture each generic attribute-array descriptor and the
`ELEMENT_ARRAY_BUFFER` binding. The current generic attribute values stay on
the context, so binding a VAO cannot alter `vertexAttrib[1-4]f` values. A
deleted buffer is detached from active and inactive VAOs before its numeric
name can be recycled.

The same descriptor now captures a per-attribute divisor. A zero divisor
selects the vertex index; a nonzero divisor selects
`floor(instance / divisor)`. `ringl_draw_arrays_instanced()` and
`ringl_draw_elements_instanced()` retain that metadata in the V2 RinGPU vertex
binding layout—even when there is only one source buffer—so it cannot be lost
through the legacy one-buffer draw ABI. The private Aquamarine embedding
preflights every instance's vertex fetch before rasterization and executes the
actual direct and indexed instance loops. This enables the bounded WebGL 1
`ANGLE_instanced_arrays` route; it is not a claim of GLES 3.x instancing or
complete WebGL conformance.

## Current packed-color framebuffer slice

`RINGL_RGB565`, `RINGL_RGBA4`, and `RINGL_RGB5_A1` renderbuffers realize as
native two-byte `RIN_GPU_FORMAT_*_UNORM` color images, not RGBA8 substitutes.
RinGL reports their logical 5/6/5/0, 4/4/4/4, and 5/5/5/1 component sizes, uses
each physical format in its pipeline key, and returns WebGL RGBA/UNSIGNED_BYTE
readback through private native staging. The RinGPU software and
Aquamarine-surface backends preserve 16-bit storage for upload, clear, draw,
sampling, and readback.

## Default drawing-buffer query slice

`ringl_get_integerv_bounded()` reports `RED_BITS`, `GREEN_BITS`, `BLUE_BITS`,
and `ALPHA_BITS` from the configured native default color format. It also
reports `DEPTH_BITS` and `STENCIL_BITS` only when that logical plane is exposed
by the default framebuffer's explicit-aspect contract. A physical D32/S8
allocation used to supply a stencil-only WebGL drawing buffer therefore reports
zero depth bits and eight stencil bits; a hidden physical plane never becomes a
browser-visible capability merely because RinGL owns its storage.

## Fixed WebGL capability query slice

RinGL, rather than the Ladybird embedding, owns the fixed profile values for
`MAX_RENDERBUFFER_SIZE`, `MAX_VIEWPORT_DIMS`, `SAMPLE_BUFFERS`, `SAMPLES`, and
`ALIASED_POINT_SIZE_RANGE`. Renderbuffers and drawable images are bounded to
4096×4096; default-framebuffer installation and viewport mutation reject larger
dimensions before they can change state. The generic RinGPU backend is
single-sample, and native point-list draws clamp the vertex `gl_PointSize`
output to the advertised `[1, 64]` pixel range.
Fragment `gl_PointCoord` is emitted as two RSH1 Float32 builtins, so the
generic RinGPU point rasterizer supplies its GLES/WebGL fixed upper-left
coordinate from the original point center instead of a synthetic varying.
`ringl_get_compressed_texture_format_count()` authoritatively reports the
nine executable ETC1/linear-S3TC/sRGB-S3TC formats; the browser still filters
that ceiling through acquired extension objects before publishing
`COMPRESSED_TEXTURE_FORMATS`.

## PACK_ALIGNMENT readback slice

`RINGL_PACK_ALIGNMENT` is tracked separately from `RINGL_UNPACK_ALIGNMENT` and
accepts only the WebGL 1 values 1, 2, 4, and 8. `ringl_read_pixels_to_bytes()`
computes the required destination span from the tight RGBA row size plus
alignment padding between rows; the final row has no trailing padding. Readback
always fills a private tight staging buffer first, then publishes just the pixel
bytes into the caller's aligned rows. Consequently both a short output span and
a native readback failure leave all caller bytes, including padding, unchanged.
The browser embedding delegates both the `getParameter(PACK_ALIGNMENT)` query
and `readPixels` destination layout to this RinGL boundary.

Readback does not reinterpret packed UNORM color bytes as `FLOAT`: packed
RGBA8/BGRA8 and 16-bit packed targets accept `RGBA/UNSIGNED_BYTE` only.
`RGBA/FLOAT` remains limited to real RinGL float color targets, so a mismatched
readback returns an error with the destination untouched.

Rectangles that extend beyond a framebuffer are clipped by RinGL, not by the
browser. RinGL stages the requested logical rows, reads only their in-bounds
intersection, and then publishes the result. Bytes corresponding to
out-of-bounds pixels, as well as PACK padding, remain untouched.

`ringl_get_implementation_color_read_format_type()` exposes the pair actually
accepted by that target: `RGBA/UNSIGNED_BYTE` for UNORM and packed color
storage, and `RGBA/FLOAT` only for native float color storage. Ladybird passes
this query through for WebGL's implementation color-read parameters.
The sample coverage value enum is kept distinct from `SAMPLE_BUFFERS`, avoiding
an accidental query alias at the public boundary.

## Current packed-color texture slice

`texImage2D`/`texSubImage2D` accept WebGL 1 `RGB`/
`UNSIGNED_SHORT_5_6_5`, `RGBA`/`UNSIGNED_SHORT_4_4_4_4`, and
`RGBA`/`UNSIGNED_SHORT_5_5_5_1`. They preserve native two-byte RGB565, RGBA4,
and RGB5_A1 shadow storage and realize matching RinGPU sampled images; they do
not expand through RGBA8. The Aquamarine surface backend unpacks the native
texels only for the shader sample table, preserving RGBA4/RGB5_A1 alpha while
making RGB565 alpha opaque. RinGL mock-backend tests verify physical format,
upload, and sub-image bytes; the actual RinGL-to-RinGPU bridge verifies
`texture2D()` RGBA output for all three formats.

## Current bounded texture-coordinate extension

The shared perspective-UV texture profile now executes one through eight
left-to-right `texture2D()` calls with a coordinate of `uv` and finite
component-wise `+`, `-`, `*`, or nonzero `/` `vec2` literals. Every operation
is lowered directly to public RSH1 arithmetic before its real RinGPU sample;
it is not folded into a host-side shortcut. A coordinate may also use a
full-width read-only `xy`/`yx`, `rg`/`gr`, or `st`/`ts` selector chain; RinGL
uses that exact scalar-register permutation for the sample rather than
rewriting a coordinate on the host. Partial-width, mixed-family, and
out-of-range coordinate selectors are rejected. The bounded two-UV profile maps
`firstUv` and `secondUv` to separate RSH1 perspective input pairs and also
supports `firstUv + secondUv` and `firstUv - secondUv` as sampler coordinates.
One declared fragment `uniform vec2` may be the right operand of the bounded
coordinate `+`, `-`, `*`, or `/` forms, including direct sampler calls and the
existing source-order local-coordinate chains. It may also be the left operand
of a direct sampler-coordinate `+`, `-`, `*`, or `/` expression; `-` and `/`
preserve uniform-minus-varying and uniform-divided-by-varying operand order in
RSH1 rather than normalizing them to their opposite operations. Its full-width read-only
`xy`/`yx`, `rg`/`gr`, or `st`/`ts` selector is materialized as the exact RSH1
constant permutation; partial-width and mixed-family selectors are rejected.
RinGL materializes the live, finite pair as the same two RSH1 Float32 constants
as a literal; a public `ringl_uniform_2f()` update stages and publishes a
replacement fragment module before the next native RinGPU draw. A zero default
or later zero uniform divisor is rejected by generic RinGPU fragment preflight
before target publication, while a zero literal divisor is rejected during
lowering. It never routes the coordinate through an Aquamarine renderer or a
Ladybird-side texture shortcut. Uniform-led local initializers and general
vector expressions remain outside this profile.
The same bounded two-UV operation may be named first as
`vec2 mixedUv = firstUv +/- secondUv;` and then passed to `texture2D()`.
That result can also feed a direct/finite-affine local chain of up to eight
declared values, while preserving source-order RSH1 arithmetic. RinGL admits a
shape only after its actual instruction and register use is within the public
RSH1 limits; eight locals are not a promise that every eight-call/offset form
will fit.
One sampled RGBA result may additionally use a finite `vec4` literal with
component-wise `+`, `-`, `*`, or nonzero `/` before `gl_FragColor` is stored.
An additive multi-sample chain is also supported when explicitly parenthesized
before that color operation; unparenthesized multi-sample precedence is rejected
rather than guessed. RinGL emits RSH1 constants and arithmetic instructions,
checks the normal RSH1 limits and literal zero divisors, and does not claim
general fragment-expression support. A finite literal may lead one sample or a
parenthesized additive chain for every arithmetic operation, retaining the
literal as the left RSH1 operand for subtraction and division. RinGPU
preflights covered fragments and rejects the complete draw before target writes
if a sampled divisor component is zero.
Up to eight fragment-local values may be chained from the shared UV; each
initializer is lowered in source order before samples and the complete shape is
rejected before IR publication when it exceeds an RSH1 limit. This remains a
deliberately narrow GLSL ES subset: other local vector expressions, more than
eight locals, more than eight UV varyings, local expressions spanning five or
more UV inputs, broader local expressions spanning three UV inputs, and more
than eight calls are still
unsupported.

The bounded three-UV texture profile similarly permits one local `vec2`
constructed with `+` or `-` from any two distinct declared UV pairs. That
live RSH1 result may then feed the same direct/finite-affine local chain as the
two-UV profile, subject to the complete instruction/register budget; samples
may also use every declared pair directly. The lowerer retains all six scalar
perspective inputs and executes every stage in RSH1, rather than selecting or
folding a coordinate on the host. One combine result may additionally combine
once with the remaining declared pair; other general three-UV local expressions
remain unsupported.

A bounded four-UV texture profile additionally maps `firstUv` through
`fourthUv` to eight distinct scalar fragment inputs. Its matching vertex RSH1
stage emits 12 outputs (`xyzw` plus all four pairs), and the native
RinGPU/Aquamarine transport clips and perspective-interpolates every scalar
without changing the public compact clip-vertex V1 ABI. Direct samples may use
each pair independently. One local may combine any two distinct four-UV pairs
with `+` or `-`, and may feed the existing finite-affine local chain; broader
four-UV expressions remain deliberately outside this profile.

The native scalar contract also carries five through eight direct `varying
vec2` texture coordinates. The transformed vertex path writes 14, 16, 18, or
20 outputs (`xyzw` plus the declared pairs), while the fragment path receives
10, 12, 14, or 16 distinct inputs. A program-link/pipeline/bind-group/draw
regression uses the eighth pair and a real image/sampler binding, so the
extension does not collapse a coordinate onto an earlier pair or fall back to
a declaration-only path. Local combinations spanning five or more pairs remain
outside this bounded profile.

See [TODO.md](TODO.md) for the implementation roadmap.

[The GLES 2.0 API inventory](docs/gles2-api-status.md) records every registry
entry point as bounded, partial, or absent, together with enum/query/limit
coverage. It is the authoritative boundary for embeddings and does not claim a
GLES version merely because a similarly named RinGL function exists.

[Known incompatibilities and exposure policy](docs/known-incompatibilities.md)
consolidates the inventory into the product-facing rule: RinGL does not
advertise a GLES or WebGL version, and every unavailable or partial feature
stays unavailable until its executable boundary and regression coverage exist.

## Repository layout

The exact layout may evolve, but the intended separation is roughly:

```text
include/ringl/       public GL/GLES-facing headers
src/context/         current-context and state tracking
src/objects/         GL object namespaces and lifetime
src/shader/          GLSL ES frontend, linker, reflection
src/translate/       GL -> RinGPU translation
src/backend/         RinGPU integration and optional backend hooks
tests/               unit and integration tests
```

## Design rules

1. Keep OpenGL compatibility policy in user space.
2. Treat RinGPU as the native explicit GPU boundary, not as an OpenGL ABI.
3. Validate GL-visible behavior before backend submission whenever possible.
4. Never pass application GL handles directly to RinGPU drivers.
5. Prefer lazy derivation and caching over rebuilding immutable GPU state on every GL call.
6. Keep shader source languages separate while sharing RinShader IR and validation.
7. Build conformance incrementally; do not claim a GL/GLES version until its required behavior is implemented and tested.

## Status

### Current Aquamarine/RinGPU embedding

`ringl_aquamarine_surface` binds RinGPU core to the generic
`ringpu_software_backend` V3. Its acquire callback validates caller-owned BGRA
default-color and optional planar D32/S8 storage and binds only those
role-tagged default images externally. Textures, offscreen FBOs, and multi-mip
images stay in generic backend-private storage. There is no `AqSurface`
renderer or direct Aquamarine clear/draw/present fallback.

The bounded first-triangle and textured-triangle translation paths are
implemented and covered by strict C11 mock-RinGPU tests. Level-zero RGBA8
texture/renderbuffer color attachments are completeness-checked, realized as
RinGPU color targets, used by clear/draw render passes, and read back through
`COPY_SOURCE`. A matching WebGL 1 `DEPTH_COMPONENT16` renderbuffer or
`DEPTH_COMPONENT32F` renderbuffer, or a level-zero
`DEPTH_COMPONENT32F`/`DEPTH_COMPONENT`/`FLOAT` texture can be attached as
`DEPTH_ATTACHMENT`; the pair is rejected on an invalid attachment or dimension
mismatch. `DEPTH_COMPONENT16` keeps its logical 16-bit query format while
sharing the existing lazy D32 RinGPU depth-target execution path. The RinOS
surface integration test verifies a depth-only clear while preserving color
and depth-tested triangle output from both custom renderbuffer formats. The
RinGPU depth pipeline supports every GLES comparison predicate: `NEVER`,
`LESS`, `EQUAL`, `LEQUAL`, `GREATER`, `NOTEQUAL`, `GEQUAL`, and `ALWAYS`.
A trusted embedding can query the default color image's post-submit
state, allowing its caller-owned presentation surface to remain synchronized
across RinGL `present()` and later content updates. When that embedding
supplies a D32 target, RinGL executes default-framebuffer depth clear and all
eight depth-tested draw predicates through a RinGPU depth render pass.

`ringl_buffer_sub_data()` provides the bounded `glBufferSubData` storage
update used by browser buffer uploads. It stages a complete replacement
RinGPU buffer before swapping the object, so a failed allocation or upload
leaves the previously visible buffer contents and CPU validation shadow
unchanged.

The public `RINGL_BUFFER_SIZE` and `RINGL_BUFFER_USAGE` enums name the
WebGL-facing buffer queries served by `ringl_get_buffer_size()` and
`ringl_get_buffer_usage()`. A bound buffer is required; target validation and
the existing `INVALID_ENUM`/`INVALID_OPERATION` behavior remain at the RinGL
boundary instead of being duplicated by browser embeddings.

Browser-owned readback destinations use `ringl_read_pixels_to_bytes()`. It
validates the tightly packed RGBA8 result size before it submits a readback,
changes a tracked image state, or writes destination memory. Short output spans
produce `INVALID_OPERATION` without modifying the destination. The older raw
pointer `ringl_read_pixels()` form remains for trusted native callers that can
prove destination capacity independently.

Shader source inspection is similarly bounded: `ringl_get_shader_source_length()`
returns the complete stored length and `ringl_copy_shader_source()` copies only a
NUL-terminated prefix fitting in caller capacity. Invalid handles report
`INVALID_VALUE` without modifying caller storage, which permits a browser
embedding to implement `getShaderSource()` without borrowing RinGL memory.

Program shader references now follow the deletion lifetime required by browser
code: deleting an attached shader marks its public name deleted while retaining
the compiled object for later linking; `ringl_detach_shader()` or program
destruction releases the final pending reference. A linked executable keeps
using its linked shader pair when later attach/detach calls change the pending
link inputs.

`RinGLProgramInfoV1` exposes a bounded snapshot of a live program's link and
validation status, attached shader count, and linked active attribute/uniform
counts. `ringl_validate_program()` records successful validation only
for a linked executable; each new link resets that state before it validates
the replacement executable. The query validates its ABI header and only writes
the caller-owned structure after it has validated the program, so an invalid
program cannot partially publish stale metadata.

`ringl_get_attached_shaders()` exposes the corresponding pending shader names
in deterministic vertex/fragment order through caller-owned storage. Its
count-only form and its copying form are both failure-atomic; a shader marked
for deletion remains listed while its program retains it for linking. Browser
embeddings therefore query the same RinGL ownership state they execute rather
than treating a local object cache as a second backend.

The current bounded uniform profile consists of linked `sampler2D`, scalar
`float`/`int`/`bool`, `vec2`/`vec3`/`vec4`, `ivec2`/`ivec3`/`ivec4`,
`bvec2`/`bvec3`/`bvec4`, and vertex-only `mat2`, `mat3`, or `mat4` position
transforms. `ringl_get_uniform_1i()` reads either a selected texture unit or
scalar integer/Boolean; `ringl_get_uniform_{2,3,4}i()` read complete integer
or Boolean vectors. The corresponding float getters and
`ringl_get_uniform_matrix{2,3,4}f()` read the scalar/vector or complete
column-major matrix values for a specific linked program and location without
depending on the current program binding. Each validates its complete input
before writing caller-owned storage, so invalid programs or locations cannot
expose a partially updated result.

`ringl_uniform_1f()`, `ringl_uniform_2f()`, `ringl_uniform_3f()`,
`ringl_uniform_4f()`, and `ringl_uniform_matrix{2,3,4}fv()` update a linked program
only after every supplied component is finite. The matrix setter requires
`transpose == 0` and preserves WebGL's column-major order. A NaN, infinity, or
transposed matrix records `INVALID_VALUE` and leaves the published uniform and
its derived executable unchanged. This lets embeddings preserve atomic
WebGL-visible uniform state while the bounded RSH1 lowering path has no
non-finite literal representation.

`ringl_uniform_1i()` accepts the linked sampler, scalar-`int`, or scalar-`bool`
location; `ringl_uniform_{2,3,4}i()` update the matching `ivec` or `bvec`
location. Integer updates retain signed i32 values in RSH1 (`CONST_I32` plus
integer arithmetic) until an explicit GLSL `float(...)` conversion emits
`I32_TO_F32`; they never reinterpret the integer bit pattern as a float.
Boolean writes instead normalize each component to exact zero or one before
the replacement module is built. All numeric setters stage a replacement
program-owned module before publishing it, so a failed backend creation retains
both the old values and executable. Boolean values allow scalar or
single-component `.x/.y/.z/.w` (and color-alias) control flow plus the bounded
GLSL `not`, `equal`, `notEqual`, `any`, and `all` builtins. Scalar conditions
also admit precedence-correct `!`, `&&`, `^^`, and `||`; the profile forbids
expression side effects, so their RSH1 Boolean evaluation preserves observable
GLSL behavior without a second backend. Boolean vector builtins lower to scalar
RSH1 integer comparisons/additions and therefore run through the same generic
RinGPU backend as every other program. Uniform arrays, multi-component Boolean
swizzles, local-mutating integer control flow, and implicit numeric conversions
remain unavailable.

Program-owned uniform artifacts are stage-selective. A mutable vertex matrix
does not force an unrelated fragment `sampler2D` shader back through the
scalar/vector lowerer: RinGL retains the fragment's validated texture RSH1
module and atomically replaces only the vertex module on a matrix update. The
focused RinGPU-module test verifies the fragment module retains its two typed
image/sampler resources across the update.

Stage selection is per linked uniform name, not merely per shader stage. In a
transformed, tinted texture program, changing `transform` replaces only the
vertex executable while changing `tint` replaces only the fragment executable;
a name declared by both stages replaces both. Replacement modules are fully
validated before the former artifact is destroyed, so a failed update retains
the last linked executable and resource layout.

That path now executes a bounded transformed textured-quad profile end to
end: a vertex shader may transform an `attribute vec4` position (or construct
one from `attribute vec2` position) with a `uniform mat4`, then copy one to
eight declared `attribute vec2` values into matching `varying vec2` pairs.
One pair retains the fixed eight-scalar interface; two through eight pairs
publish four through sixteen interpolated scalars. Both the matrix product and
varying stores are RSH1 instructions consumed by RinGPU; no embedding-side
geometry transform or sampled-color fallback is involved. The focused
`textured-draw` test binds two coordinate attributes and two texture units,
uploads a matrix through the public uniform API, and verifies the resulting
RinGPU-native two-sampler pipeline and typed resource bindings. The paired
fragment profile may combine the matching bounded texture calls and multiply
their RGBA result by one linked `uniform vec4`; RinGL materializes the finite
four-component value in the program-owned fragment RSH1 module and retains
the sampler-resource metadata needed to bind every native image/sampler pair.
This specialized varying/texture shape deliberately remains `mat4` only. The
generic no-varying vertex profile additionally executes matching Float
`matrixCompMult(matN, matN)` for `mat2`, `mat3`, and `mat4`: scalar-diagonal,
component-list/vector-column, and matching-copy constructors feed initialized
local or program-owned uniform matrices, and every column-major component
becomes an executable scalar RSH1 multiply before a matching `matN * vecN`.
Matrix arrays, cross-dimension conversion, general matrix arithmetic, and all
other matrix expressions remain outside both shapes. Direct texture coordinates
through eight UV pairs fit the RSH1 interface; broader local coordinate
expressions remain outside it and fail lowering without publishing a truncated
module.

The same transformed route supports the common vertex-color texture form: an
`attribute vec4` is copied into a following `varying vec4`, and the exact
fragment expression `texture2D(texture, uv) * vertexColor` (or a full-width,
read-only `xyzw`/`rgba`/`stpq` swizzle of that color and of an optional
`uniform vec4` tint) loads six
interpolated scalars, samples the typed image/sampler pair, and emits four RSH1
component-wise multiplies. One or two UV pairs may accompany that color while
the native interface remains within eight scalar varyings. With two pairs, the
exact material expression `(texture2D(firstTexture, firstUv) +
texture2D(secondTexture, secondUv)) * vertexColor` samples and adds both typed
image/sampler pairs before the RGBA modulation. Full-width, read-only color
swizzles are composed into those four multiply sources, rather than requiring a
backend vector operation. The focused native draw test verifies both the
six-scalar and twelve-output/eight-input pipelines, matrix
uniform updates, dense position/UV/RGBA attribute layouts, and native resource
bindings. Either material may append one `uniform vec4` tint and one `uniform
float` opacity; RSH1 materializes the finite tint constants, then broadcasts
opacity across RGBA without rebuilding the matrix module. Updating either one
replaces only the fragment executable. The image/sampler binding remains native
throughout; no CPU pre-multiplied color is published.

The bounded varying-coordinate texture chain also accepts a full-width,
read-only selector on its optional `uniform vec4` color operation, such as
`texture2D(texture, uv) * tint.stpq.bgra` or, for one sample,
`tint.stpq.bgra * texture2D(texture, uv)`. Its four RSH1 constants are
permuted directly and operand order is preserved; it does not introduce a
general uniform-vector expression.

The no-varying RSH1 profile also lowers local `vec2`, `vec3`, and `vec4`
values and component-wise vector arithmetic directly to scalar RSH1
instructions. Same-width `+`/`-`, unary `-`, and vector/scalar `*` and `/`
are executable rather than host-side constant folding. The common Float
expression builtins `min`, `max`, `clamp`, `mix`, and `dot` now use the same
RSH1 path: min/max/clamp emit native scalar min/max instructions, while
mix/dot expand to ordered scalar arithmetic. Float `floor`, `ceil`, `fract`,
`mod`, `abs`, `sign`, `step`, and `smoothstep` use the same executable route:
`floor` is RSH1 opcode 59 in generic RinGPU, and the rest scalarize to floor,
ordered arithmetic, and comparisons. They accept only their matching Float
scalar/vector overloads and reject integer, matrix, or mismatched-width calls
before an executable is published. Float geometry `sqrt`, `inversesqrt`,
`length`, `distance`, `normalize`, `cross`, `reflect`, `faceforward`, and
`refract` also lower to scalar RSH1: opcode 60 performs a finite binary32
square root in generic RinGPU, while the vector forms compose ordered scalar
arithmetic and comparisons. `cross` is vec3-only and the directional forms
require matching vec2/vec3/vec4 operands (with scalar Float eta for `refract`);
invalid type/width combinations fail before publication. Undefined zero
normalization/inverse-square-root is not replaced with a host-side value.
The bounded Float trigonometric family `radians`, `degrees`, `sin`, `cos`,
`tan`, `asin`, `acos`, and both `atan` overloads follows the same route:
angle conversion is scalar multiplication, opcodes 61--66 execute finite
sin/cos/atan and inverse-trigonometric values in generic RinGPU, and `tan`
uses executable sin/cos division. Matching Float scalar/vector overloads are
accepted (the two-argument `atan` widths must match); out-of-domain asin/acos,
atan(0,0), and sin/cos/tan angles outside `[-1024, 1024]` radians fail during
lowering or preflight before target publication rather than using host libm or
a direct surface fallback. Scientific Float literals are lowered only when they
are finite binary32 values, so an overflowing literal fails before publication.
The bounded Float exponential family `exp`, `log`, `exp2`, `log2`, and `pow`
also runs only through RinGL RSH1 and generic RinGPU: opcodes 67--69 execute
the finite binary32 base-two operations, while `exp`/`log` lower through exact
scale operations around them. Matching Float scalar/vector overloads are
accepted (`pow` widths must match). `exp2` and the derived `exp`/`pow` result
exponent are bounded to `[-126, 127]`; logarithm and power bases must be
strictly positive. Domain and range failure reject publication rather than
substituting host libm or a direct surface result.
Matching-basic-type `vecN(scalar)` and `ivecN(scalar)` constructors are also
executable in this profile: RinGL aliases the one Float or i32 RSH1 register
across all target components, rather than asking Ladybird or the private
Aquamarine surface to expand it. Component-list constructors still require an
exact target width and one basic type; `vecN(int)` and `ivecN(float)` remain
rejected, with scalar `float(...)`/`int(...)` the explicit typed conversion.
This covers common uniform color modulation and matrix-transformed positions
(`mat2 * vec2`, `mat3 * vec3`, or `mat4 * vec4`) plus a vector offset and the
matching Float `matrixCompMult(matN, matN)` subset described above, while
retaining explicit RSH1 resource and register limits; swizzles, matrix arrays,
cross-dimension conversion, general matrix arithmetic, vector comparisons, and
general control flow are still outside the profile. A bounded scalar
`if (scalar-comparison) { stage-output = vec4(...); } else { stage-output =
vec4(...); }` is executable: RinGL emits the original Float/i32 comparison,
tests its i32 result against zero, and uses only forward RSH1 branches. The
generic RinGPU backend, not Ladybird or Aquamarine, evaluates the branch.
Nested/local-mutating branches, partial outputs, and loops remain rejected.

Standalone fragment `discard;` lowers to RSH1 `DISCARD`. The generic RinGPU
backend terminates that fragment before output validation and depth, stencil, or
color publication; it is not a surface-side clear or Aquamarine command.
Vertex `discard`, conditional `discard` outside the documented scalar-Boolean,
one-discard-branch form, loops, and general control flow remain rejected by
this bounded profile.

The frontend accepts GLSL ES global `precision lowp|mediump|highp` declarations
for `float`, `int`, and `sampler2D`. They are not ignored text: the compiler
validates the complete declaration before lowering, and the accepted numeric
domain executes in IEEE-754 binary32 RSH1 (a conforming higher-precision
implementation for the advertised floating qualifiers). An incomplete or
unknown precision declaration fails compilation rather than being skipped.
`ringl_get_shader_precision_format()` exposes this executable profile through
a versioned caller-owned record: all accepted floating precision classes report
binary32's `[-126, 127]` range and 23 fraction bits, while accepted integer
classes report RSH1's signed i32 GLES range `[31, 30]` with zero fraction bits.
The query validates the stage, precision token, and output header before it
updates caller storage, so a browser need not fabricate shader precision.

`ringl_get_string()` exposes four static descriptions owned by this core:
`VENDOR`, `RENDERER`, `VERSION`, and `SHADING_LANGUAGE_VERSION`. The strings
identify RinGL and its bounded RSH1 GLSL ES 1.00 subset, rather than claiming a
specific host GPU driver or full OpenGL ES conformance. They require a current
context, have process lifetime, and unknown pnames fail with `INVALID_ENUM`.

`ringl_get_renderbuffer_info()` exposes the current renderbuffer's dimensions,
internal format, component bit counts, and zero sample count through a
versioned caller-owned snapshot. An allocated renderbuffer reports zero-sized
RGBA4 default state; the supported RGBA8, D16, D32, and D24S8 storage profiles
report their actual channel/depth/stencil precision without realizing a RinGPU
image merely to answer a query.

Linked vertex shaders retain their active attribute declaration order and
widths. `ringl_bind_attrib_location()` records a requested generic
vertex-array index for the next successful link, and
`ringl_get_attrib_location()` returns the index in the linked executable.
During a draw RinGL resolves only those active, linked indices into dense RSH1
scalar inputs before it builds the RinGPU pipeline. An unrelated enabled array
therefore cannot alter the shader interface or be fetched accidentally. Each
active array retains its captured buffer and effective stride; distinct pairs
are emitted as dense V2 RinGPU bindings for direct and indexed draws. A
disabled active array instead uses its tracked current value (the WebGL default
is `(0, 0, 0, 1)`, updated by `ringl_vertex_attrib1f` through
`ringl_vertex_attrib4f`) as exact Float32 scalar descriptors. The embedding
must explicitly advertise `RINGL_RIN_GPU_VERTEX_INPUT_CONSTANT_FLOAT32` and,
for more than one stream, `RINGL_RIN_GPU_VERTEX_INPUT_MULTI_BUFFER` with the
matching V2 callbacks. Otherwise RinGL reports `INVALID_OPERATION` rather than
interpreting Float32 bits as an address or flattening distinct streams.

The public vertex-attribute query surface uses the WebGL 1 numeric names
`RINGL_CURRENT_VERTEX_ATTRIB`, `RINGL_VERTEX_ATTRIB_ARRAY_*`, and a versioned
`RinGLVertexAttribInfoV1` for array enable/format/stride/captured-buffer/offset
state. `ringl_get_vertex_attrib_current()` copies the generic value into an
embedding-owned four-float output, so no caller observes RinGL internal state;
an invalid index records `INVALID_VALUE` and leaves that output unchanged.
This lets a browser adapter implement both the scalar and typed-array query
forms without a GLES dependency.

`ringl_get_integerv_bounded()` is the corresponding general state-query ABI
for embeddings. It accepts the caller's element capacity, determines the exact
one- or four-integer result before writing, and rejects a null/short output
without changing it. Unknown pnames record `INVALID_ENUM`; a short output
records `INVALID_OPERATION`. The legacy `ringl_get_integerv()` remains for
existing callers, but new browser-facing code uses the bounded entry point so
viewport, scissor, and color-mask queries cannot overrun an output buffer.

After the context-local `WEBGL_draw_buffers` gate is enabled,
`ringl_get_integerv_bounded()` also reports the real four-target limit through
`MAX_DRAW_BUFFERS_WEBGL` and `MAX_COLOR_ATTACHMENTS_WEBGL`, plus each supported
`DRAW_BUFFERi_WEBGL` mapping. A newly bound custom framebuffer reports
`COLOR_ATTACHMENT0` followed by `NONE`; the default framebuffer reports `BACK`
only while an embedding has supplied its drawing buffer. `drawBuffersWEBGL([])`
on a custom framebuffer and `[NONE]` on the default framebuffer are stored as
zero color writes, while a present depth/stencil target still executes through
the RinGL/RinGPU pass. A directive-free `gl_FragColor` program has only output
zero: if another draw-buffer slot is enabled and any color channel is writable,
RinGL records `INVALID_OPERATION` before submission. With every color channel
masked off, that ordinary one-output ABI remains valid for a depth/stencil
draw. A `GL_EXT_draw_buffers`/`gl_FragData` program instead uses the MRT ABI
even if only one attachment is active. Before the extension gate, every one of
these pnames is rejected as `INVALID_ENUM` without changing the caller's
output.

`RinGLBlendColorV1` separately snapshots the finite blend constant that RinGL
resolves into RinGPU's V2 pipeline descriptor. It is normally clamped; after
the private Float-color gate, finite values outside `[0,1]` are retained only
for an RGBA32F pipeline. Its versioned input is validated before the complete
snapshot is copied out, so a browser can answer `BLEND_COLOR` without reusing
clear-color state or exposing internals.

`ringl_depth_range()` retains the OpenGL depth-range state separately from the
viewport. It rejects non-finite inputs without changing state, clamps each
finite endpoint independently to `[0,1]`, and preserves reversed ranges.
`RinGLDepthRangeV1` exposes that state through a versioned, failure-atomic
snapshot. Draw translation passes the exact pair through RinGPU raster state,
rather than treating every range as the default `[0,1]`.

`RINGL_POLYGON_OFFSET_FILL` and `ringl_polygon_offset()` carry finite factor
and units values in the dynamic raster state. The RinGPU V2 descriptor keeps
the V1 default disabled, while the generic RinGPU software backend applies
`m * factor + 2^-23 * units` to filled-triangle depth immediately before depth
comparison and writing. Points and lines are not offset; non-finite inputs
leave the existing RinGL state intact and report `INVALID_VALUE`.

`ringl_line_width()` accepts finite aliased widths from one through 64 pixels.
RinGPU's V3 raster descriptor preserves V1/V2's one-pixel default, and the
generic backend rasterizes bounded unique coverage for line lists, strips,
and loops. A half-open edge rule preserves the width-one rasterization at
pixel-boundary ties; invalid values leave state unchanged with `INVALID_VALUE`.
`RinGLLineWidthV1` exposes the current width and fixed `[1, 64]` range through
a versioned snapshot for WebGL `LINE_WIDTH` and `ALIASED_LINE_WIDTH_RANGE`
queries.

`RINGL_SAMPLE_COVERAGE` and `ringl_sample_coverage()` keep the finite-clamped
coverage value and invert flag in explicit dynamic raster state. RinGPU's V4
suffix preserves the V1--V3 disabled/full-coverage default. The current
Aquamarine target owns one storage sample, so enabled zero coverage suppresses
that sample's color/depth/stencil fragment operation and inverted zero coverage
retains it. `RinGLSampleCoverageV1` provides a versioned, failure-atomic
snapshot for browser queries. `ringl_hint()` accepts only
`GENERATE_MIPMAP_HINT` with standard hint modes as an advisory no-op.
`ringl_generate_mipmap()` is the separate, explicit operation that builds a
complete RGBA8-normalized 2D chain through a deterministic clamped 2x2 box
filter; hints do not select a LOD, and derivative hints are not claimed as
supported.

Linked program reflection is also exposed without borrowing RinGL storage.
`ringl_get_active_attrib()` and `ringl_get_active_uniform()` copy one bounded
entry into `RinGLActiveInfoV1` only after validating its versioned output
header. The current profile reports scalar/`vec2`/`vec3`/`vec4` float
attributes and `sampler2D`, scalar `float`, `vec2`, `vec3`, `vec4`, `mat2`,
`mat3`, and `mat4` uniforms; unlinked programs or out-of-range indices record the appropriate
error and leave caller storage unchanged.

`RINGL_POINTS`, `RINGL_LINES`, `RINGL_LINE_STRIP`, `RINGL_LINE_LOOP`,
`RINGL_TRIANGLES`, `RINGL_TRIANGLE_STRIP`, and `RINGL_TRIANGLE_FAN` map to
RinGPU's native point-list, line-list, line-strip, line-loop, triangle-list,
triangle-strip, and triangle-fan topologies. The primitive is part of RinGL's
pipeline-cache key, so these draws cannot reuse a different primitive's
pipeline. Both direct and indexed draws use the existing vertex and index
validation paths before they reach the embedding; an incomplete line-list
pair, a line strip or line loop with fewer than two vertices, and a triangle
strip or fan with fewer than three vertices are successful no-ops. Public
For the generic no-varying profile and every existing structural varying
profile with a finite literal, `uniform float`, a dedicated `attribute float`
point-size input, or that profile's source color-attribute component optionally
followed by one finite right-hand `+`, `-`, `*`, or nonzero `/` literal, a
vertex `gl_PointSize` Float32 occupies the
native scalar immediately after clip `xyzw`. Existing varying outputs move up
by one scalar, an active float uniform atomically rebuilds the program-owned
vertex RSH1 module, and the scalar attribute/component arithmetic operand is
emitted from a live vertex RSH1 register before the generic backend finite-checks
and clamps the point size to `[1, 64]`. Broader varying point-size expressions remain outside
this bounded slice. Fragment `gl_PointCoord` supports normal vector use and
the bounded `texture2D(sampler uniform, gl_PointCoord)` form; both lower to the same
RSH1 builtin X/Y loads and execute only for native point-list fragments.
Public WebGL binding and presentation remain unsupported.

Custom RGBA8 renderbuffer FBOs may additionally attach a matching
`DEPTH24_STENCIL8` renderbuffer or level-zero
`DEPTH24_STENCIL8`/`DEPTH_STENCIL`/`UNSIGNED_INT_24_8` texture through
`DEPTH_STENCIL_ATTACHMENT`, or attach a D24S8 or WebGL 1 `STENCIL_INDEX8`
renderbuffer through `STENCIL_ATTACHMENT` alone. `STENCIL_INDEX8` retains its
logical stencil-only format and realizes a native one-byte RinGPU `S8_UINT`
target with no depth plane. RinGL converts the packed 24/8 texture input into
RinGPU's F32 depth/S8 stencil storage and executes independent front/back
stencil tests, reference/read/write masks, all eight stencil operations, and
stencil clear before the depth test. A stencil-only attachment rejects depth
comparison/write at the native pipeline boundary; an enabled logical depth
test therefore cannot access or accidentally manufacture a depth plane. The
common `ringl_stencil_*` calls update both
faces, while the `*_separate` forms set one face or both explicitly. This is
tested through the RinOS RinGL-to-RinGPU surface path, including stencil
rejection, replacement, write masking, depth-fail behavior, reversed winding,
back-face culling, and an enabled `DEPTH_TEST` with `NEVER` on a stencil-only
FBO. `ringl_clear(RINGL_STENCIL_BUFFER_BIT)` also forwards the front stencil
write mask through the native render pass, so it preserves masked-off stencil
bits rather than overwriting the complete S8 plane. Separate D32 depth and
native S8 stencil renderbuffers are supported through a versioned three-target
RinGPU pass. RinGL preserves the two attachment owners and only reports a
combined attachment when they are identical; the surface backend passes their
Float32 depth and byte stencil planes independently to the software rasterizer.
That pass accepts a D32 plane from either D32 or D24S8 and an S8 plane from
either native S8 or D24S8, while still requiring distinct physical images.
Consequently a D24S8 renderbuffer is also valid as a logical depth-only
attachment, and either physical aspect can be paired with the other format in
separate depth/stencil renderbuffer FBOs. A depth-only D24S8 pass preserves its
unbound physical stencil plane with native LOAD/STORE rather than exposing it
to logical stencil state. The offscreen D24S8 surface allocation keeps F32
depth and byte stencil in separate checked planes, so its eight-byte transfer
format is never used as the in-memory depth pitch. Strict FBO tests and the
actual RinGL-to-RinGPU-to-Aquamarine test cover these combinations. Texture
attachments are not merely query-only: the same actual test covers every
supported distinct pair of D32/D24S8 depth renderbuffer or texture and native
S8/D24S8 stencil renderbuffer or D24S8 texture through clear, depth-fail
stencil update, and RGBA readback. Exact-dimension manual D32/D24S8 mip levels
may be attached with a same-extent color mip. RinGL uses the optional V6 pass
records to provide every color/depth/stencil subresource to RinGPU; unsupported
embeddings report `FRAMEBUFFER_UNSUPPORTED` rather than redirecting to level
zero. Texture cube faces, multisample storage, and resolve remain outside this
bounded profile.

`ringl_get_framebuffer_attachment()` is the versioned, caller-owned query for
the bounded custom-FBO model. It reports the actual current
`COLOR_ATTACHMENT0`, `DEPTH_ATTACHMENT`, `STENCIL_ATTACHMENT`, or combined
`DEPTH_STENCIL_ATTACHMENT` object kind, name, and texture level without
realizing a RinGPU image for observation. A combined query returns no object
unless both logical aspects share one attachment. The older color-only helper
is retained as a compatibility shorthand; new embeddings should use the
attachment-point query. Texture cube faces, separate attachments outside the
verified D32/D24S8 depth and native S8/D24S8 stencil matrix, and multisample
attachments remain outside the profile.

RinGL exposes its native distinct depth/stencil RinGPU pass without a
WebGL-specific context mode. The Ladybird embedding owns the WebGL 1
compatibility boundary: it queries the live attachments, reports only a
distinct pair as `FRAMEBUFFER_UNSUPPORTED`, and stops draw/read/copy/clear
before native submission. An identical object and level, including shared
D24S8, remains valid.

`ringl_clear()` also carries a bounded lower-left clear region and an RGBA
write mask to RinGPU. An enabled WebGL scissor clips color, depth, and stencil
clears together; masked color channels and disabled depth writes preserve their
previous storage, and requested depth/stencil clears without a matching
attachment are no-ops. The RinOS surface backend applies these rules to both
the caller-owned BGRA target and offscreen RGBA FBO targets.

`ringl_get_clear_values()` snapshots the current mutable color, depth, and
stencil clear values through a versioned structure. An embedding can therefore
perform WebGL's post-presentation default clear and restore the application
state instead of silently changing the next author-visible `glClear`.

`ringl_clear_default_framebuffer_for_embedding()` is the stricter form used by
a browser presentation bridge. It clears only the default drawing buffer to
transparent black/depth one/stencil zero, ignores author framebuffer, scissor,
and write-mask state, and restores every author-visible RinGL state bit. Its
result is returned out-of-band, so the bridge can recover a failed maintenance
clear without consuming or injecting the application's next `glGetError()`.

Level-zero `RGBA`/`RGB`/`ALPHA`/`LUMINANCE`/`LUMINANCE_ALPHA` with
`UNSIGNED_BYTE` are normalized into canonical RGBA8 shadow storage. RinGL
tracks WebGL 1 `UNPACK_ALIGNMENT`; all valid 1/2/4/8-byte row alignments are
applied to `texImage2D` and `texSubImage2D`, including RGB padding and D32
source rows, before the texture is uploaded as a RinGPU sampled image. The
RinOS surface backend now executes the matching typed sampled-image
and sampler bind group instead of treating it as a placeholder: it snapshots
the bounded RGBA8 and packed-color images into the resource-aware software
executor and applies nearest or linear filtering with clamp-to-edge, repeat,
or mirrored-repeat addressing. `ringl_generate_mipmap()` builds a complete
chain for unpacked RGBA8-normalized and native RGB565/RGBA4/RGB5_A1 color
storage; packed levels average their stored 5/6/4-bit components before
repacking. Explicit nonzero-level `texImage2D` and `texSubImage2D` update
exact-dimension same-format color levels without folding them into level zero.
Both paths realize each contiguous level through the
optional V2 RinGPU image/upload callbacks. A base-level sub-image update drops
only generated levels, preserving explicitly supplied level data. The executor
uses the optional V7 bind-group callback for a minification sampler: it
transitions every contiguous sampled level to shader-read, binds the complete
chain, and never silently substitutes level zero for a backend that lacks that
tail. The RinGPU/Aquamarine executor calculates implicit perspective-correct
fragment gradients, applies sampler bias/min/max LOD, then performs nearest or
linear mip selection and the independent min/mag texel filter. Packed-color
mip levels execute as FBO color targets through the same V5 subresource path:
RGB565/RGBA4/RGB5_A1 clear, draw, and readback retain their native format.
Exactly defined unpacked RGBA8 color mips execute through the same callbacks.
Exact-dimension manual D32/D24S8 levels use V6 pass records for
color/depth/stencil subresources; without the required tail the FBO reports
`FRAMEBUFFER_UNSUPPORTED`. The strict C11 RinGL tests cover format
normalization, tightly-packed RGB image/sub-image data, padded D32 source rows,
generated/manual mip upload, and the actual RinGPU bridge verifies a
three-level texture selects its distinct level-one colour while rendering.

The WebGL 1 `WEBGL_depth_texture` inputs use that same texture ownership
boundary: `DEPTH_COMPONENT` with `UNSIGNED_SHORT` or `UNSIGNED_INT` is
normalized into the native Float32 D32 shadow, and `DEPTH_STENCIL` with
`UNSIGNED_INT_24_8` becomes the existing D32/S8 texture storage. Conversion
uses unaligned-safe byte copies and happens only after the complete bounded
source span has been validated. Unsupported formats, short uploads, and failed
sub-image conversions retain the old definition rather than publishing a
partial depth texture. The focused texture and RinOS bridge tests attach those
textures to a real custom FBO and clear/read back its color target through the
RinGL-to-RinGPU route.

The same bounded ownership path also carries Float32 color textures for the
RinOS `OES_texture_float` / `WEBGL_color_buffer_float` embedding. Exact
`RGBA`, `RGB`, `ALPHA`, `LUMINANCE`, and `LUMINANCE_ALPHA`/`FLOAT` uploads are
converted with unaligned-safe reads into an RGBA32F shadow. Every one of those
Float texture formats can also be a color attachment, realized as native
`RIN_GPU_FORMAT_RGBA32_FLOAT` storage with
`COPY_DESTINATION|SAMPLED|COLOR_TARGET|COPY_SOURCE`; `RGBA32F` renderbuffers
use the same physical target.
`ringl_framebuffer_color_attachment_is_float()` lets a browser gate that
native capability behind its WebGL extension object without mirroring RinGL
attachment state. Float clear and fragment outputs retain finite components
outside `[0,1]`. `ringl_enable_webgl_float_color_buffer()` adds the same
context-local WebGL gate for finite `blendColor` components: RGBA32F pipelines
preserve them, while fixed-point pipelines still clamp them. A Float color
target reads back only as `RGBA/FLOAT`, never as silently quantized RGBA8. A
Float texture is initially complete only with
`NEAREST` magnification and `NEAREST` or `NEAREST_MIPMAP_NEAREST`
minification. A browser that has actually granted `OES_texture_float_linear`
calls the explicit context-local `ringl_enable_webgl_float_texture_linear()`
gate; it then permits `LINEAR` magnification and `LINEAR`,
`NEAREST_MIPMAP_LINEAR`, `LINEAR_MIPMAP_NEAREST`, or
`LINEAR_MIPMAP_LINEAR` minification. The gate is deliberately not inferred
from a capable native sampler, so a non-WebGL caller cannot make the browser
extension visible accidentally. RinGL preserves the corresponding min/mag/mip
descriptors through the RinGPU adapter and generated RGBA32F mip chain.
Legacy attachment semantics stay logical while their physical target is RGBA:
`RGB` exposes alpha one, `ALPHA` exposes zero RGB, `LUMINANCE` replicates its
red/luminance value into RGB with alpha one, and `LUMINANCE_ALPHA` replicates
the luminance while retaining alpha. Clear, fragment output, Float readback,
and CopyTex all use that same conversion. A partial RGB `colorMask` for a
luminance target updates the one logical luminance component atomically across
physical RGB; LUMINANCE_ALPHA still masks alpha independently. The generic
software backend rejects a non-finite component before it changes its target.
The Aquamarine embedding snapshots the same Float32 components into its RSH1
sampler table, and the product bridge test draws every Float legacy format
through the full RinGL/RinGPU route into a Float FBO.

`OES_texture_half_float` is represented by the public
`RINGL_HALF_FLOAT_OES` token. Its bounded uploads accept binary16 bytes only
after the caller has checked their complete packed/padded span; RinGL reads
each 16-bit component by `memcpy`, decodes it into the private RGBA32F shadow,
and realizes that exact shadow as the existing `RINGL_RIN_GPU_FORMAT_RGBA32_FLOAT`
sampled image. `RINGL_HALF_FLOAT_OES` has its own
`ringl_enable_webgl_half_float_texture_linear()` completion gate, so granting
Float linear filtering cannot accidentally make a binary16 texture linearly
sampleable. Short `texSubImage2D` imports leave the old shadow unchanged. The
component-type attachment query distinguishes `RINGL_HALF_FLOAT_OES` from
Float32, letting a browser keep half texture FBOs unavailable until it can
implement the separate `EXT_color_buffer_half_float` RGBA16F precision and
readback contract. The RinGL and full Aquamarine bridge tests exercise the
binary16 conversion and a real 2×2 bilinear sampled draw; there is no direct
Aquamarine texture or sampler path.

`EXT_blend_minmax` follows the same policy: `MIN` and `MAX` are rejected until
the embedding explicitly calls `ringl_enable_webgl_blend_minmax()` after the
WebGL extension object is acquired. The existing native blend pipeline then
executes the selected minimum or maximum through the RinGPU/Aquamarine route;
there is no direct surface fallback.

Embeddings handling browser-facing byte uploads must use
`ringl_tex_image_2d_from_bytes()` and `ringl_tex_sub_image_2d_from_bytes()`.
Those APIs take the source extent and
validate every readable row, including unpack-alignment padding but excluding
unused padding after the final row, before allocating or changing texture
shadow storage. A short byte span produces `INVALID_VALUE` with the prior
definition intact. The older raw-pointer texture APIs remain only for trusted
native callers that can prove the source extent independently.

`copyTexSubImage2D` has a bounded data-movement path from the current complete
color target, including RGBA8/packed RGB565/RGBA4/RGB5_A1 and native
RGBA32F/RGBA16F texture/renderbuffer FBOs, into a defined `RGBA`, `RGB`,
`ALPHA`, `LUMINANCE`, `LUMINANCE_ALPHA`, Float32, binary16, or native packed
texture level. It validates FBO completeness and both source and destination
rectangles before allocating a temporary canonical RGBA snapshot, uses the
fenced RinGPU readback path (including default-framebuffer BGRA-to-RGBA
swizzle), validates every Float32 component as finite, and only then updates
the destination shadow. RGB retains implicit alpha one; alpha/luminance formats
apply their canonical component expansion; Float32 preserves finite
out-of-range components, binary16 uses the existing finite saturating
conversion, and fixed-point/packed destinations quantize the snapshot.
Explicit nonzero levels retain their independently defined storage. A failed
readback or non-finite Float source leaves every destination level unchanged.
Depth/stencil readback, multisample, and other unrepresented copy semantics
remain outside the current profile.

`copyTexImage2D` uses the same fenced snapshot from those complete color targets
to define `RGBA`, `RGB`, `ALPHA`, `LUMINANCE`, `LUMINANCE_ALPHA`, Float32,
binary16, or native RGB565/RGBA4/RGB5_A1 storage. Level zero replaces the base
chain; an explicit nonzero level requires a defined same-format base and exact
mip dimensions and retains its base component representation. Canonical formats
apply their component-expansion rules, Float32 retains finite values, binary16
uses the finite saturating conversion, and packed definitions quantize canonical
RGBA directly into two-byte storage. Its full source rectangle, FBO completeness,
and destination limits are validated before readback; the previous texture
definition and realized RinGPU image remain intact unless snapshot completion
succeeds. This profile does not yet define zero-sized, depth/stencil, or
multisample copy definitions.

For native packed storage, that retained representation is the exact WebGL
upload type (`UNSIGNED_SHORT_5_6_5`, `UNSIGNED_SHORT_4_4_4_4`, or
`UNSIGNED_SHORT_5_5_5_1`), never a fabricated `UNSIGNED_BYTE` substitute.
An attached texture therefore re-realizes as a RinGPU color target after a
replacement definition, while a sampled-only texture remains sampled-only. The
sync and texture-GPU tests cover both cases through the actual fenced readback
boundary.

The RinGL-to-RinGPU-to-Aquamarine integration test also performs the full
observable ordering sequence: it draws to a source texture FBO, snapshots it
with both `copyTexSubImage2D` and `copyTexImage2D`, reads the copied Float FBO,
clears that copied attachment, flushes and finishes, then reads the original FBO
again. This proves that the `COPY_SOURCE` and later `COLOR_TARGET` transitions
split passes without aliasing or overwriting either attachment.

The strict sync test complements that live-pixel evidence with an exact fake
RinGPU event trace for clear → copy → clear → flush → finish → readback. It
asserts command-list creation/reset, every `COPY_SOURCE`/`COLOR_TARGET`
transition, render-pass begin/end, immediate queue submission, fenced submission
and wait, plus readback order; `flush` is explicitly checked to add no deferred
work under the current immediate-submit model.

The one-sampler constant texture profile accepts both `vec2(u, v)` and the
GLSL scalar-splat form `vec2(value)`. The latter requires a finite literal and
stores that exact Float32 value in both RSH1 texture-coordinate registers, so
the resource-aware RinGPU surface path samples `(value, value)` rather than
silently using an arbitrary second coordinate.

A bounded fragment profile permits one through eight finite constant-coordinate
calls in a left-to-right `+` chain for `gl_FragColor`. Each active declaration
is compacted in declaration order into a dense typed RSH1 image/sampler pair;
every call then references its active pair, so repeated samples and unused
declarations do not fabricate invalid RinGPU resources. RinGL maps that compact
pair table back to linked program uniform names when it builds the selective
bind group. The profile uses at most 85 instructions and 80 registers (within
RinGL's 96-register ceiling and RinGPU's public 256-register limit). RinGL
realizes every selected texture unit, records transitions for distinct images
before the draw, and publishes tracked states only after submission succeeds.
The focused RinGL-to-RinGPU-to-Aquamarine test leaves a first declaration
unbound while repeated reads of the second draw their actual clamped result;
it also verifies reverse-order red + green + blue and all eight samplers
(sixteen typed resources) producing actual white pixels. The perspective
`varying vec2` extension now accepts one through eight `texture2D()` calls
over one through eight declarations in an exact left-to-right addition, using
the shared interpolated coordinate. Calls may repeat one sampler and unused
declarations are omitted from the dense typed pair table; the saved active map
selects the matching linked uniform. The bridge verifies both red + green
yellow and a three-call, second-declaration-only green result. Other arbitrary
expressions, noncanonical coordinates, and larger varying-coordinate chains
remain unsupported.

The initial varying bridge now also executes a bounded vertex-color profile:
`attribute vec2 position; attribute vec4 color; varying vec4 vertexColor;`
with `gl_Position = vec4(position, 0.0, 1.0)`, `vertexColor = color`, and
`gl_FragColor = vertexColor` or a full-width read-only swizzle such as
`vertexColor.stpq.bgra`. RinGL expands the two attributes to six scalar
Float32 RinGPU inputs and the color to four perspective-interpolated scalar
varyings; RSH1 lowering and a RinGL-to-RinGPU surface test verify the resulting
RGBA pixels. This does not make arbitrary varying declarations or expressions
available: multiple independent varyings and general expressions remain outside
the bounded profile.

`vertexAttribPointer` accepts the WebGL 1 scalar source types `FLOAT`,
`BYTE`, `UNSIGNED_BYTE`, `SHORT`, and `UNSIGNED_SHORT`, including normalized
integer conversion. RinGL expands every component into a typed scalar RinGPU
input and preserves byte strides rather than silently requiring Float32
alignment. The active program's linked generic locations select which arrays
become those inputs. Active arrays may independently use captured buffer and
effective-stride pairs through the opt-in V2 binding ABI; single-stream and
all-constant inputs continue through V1. Disabled active generic values are
emitted as explicit Float32 constants, subject to the embedding capability
described above; WebGL 2 integer attributes and general vertex pulling remain
outside this slice.

The corresponding bounded RGB profile accepts `attribute vec3 color` and
`varying vec3 vertexColor`, with `gl_FragColor = vec4(vertexColor, 1.0)` or a
full-width read-only selector such as `vec4(vertexColor.bgr, 1.0)`.
It uses three interpolated color slots, then explicitly writes `1.0` to the
fourth fixed vertex output and loads that slot in the fragment RSH1. This keeps
the native RinGPU interface fully mapped and type-checked instead of treating
the unused alpha slot as an implicit value.

Two independently declared `vec2` varyings are also supported in one bounded
profile: `colorRG` and `colorBA` are assigned to `vertexRG` and `vertexBA`,
then `gl_FragColor = vec4(vertexRG, vertexBA)` reads all four interpolated
components. Link reflection assigns the declarations distinct vertex-output
and fragment-input locations, which the RinGPU pipeline maps one-to-one.

This remains a bounded profile. A default caller-owned D32 surface has no
stencil storage unless its embedding explicitly supplies the matching S8
plane; that D32S8 default target executes the same front/back stencil path.
Level-zero `DEPTH_COMPONENT32F` and `DEPTH24_STENCIL8` textures also execute
through the existing bounded `sampler2D`/`texture2D()` RSH1 path. RinGL
realizes their D32/D32S8 images with both depth-target and sampled usage;
RinGPU's ordinary sampled-image binding accepts both, and the Aquamarine
executor snapshots their depth plane as the deterministic
`(depth, 0, 0, 1)` compatibility value. The D24S8 stencil plane remains in
its native attachment storage and is not exposed by this regular sample path.
Only red is relied on by the WebGL depth-texture extension; the remaining
values are this backend's documented deterministic choice. A texture equal to
either active FBO attachment is rejected before it can form a feedback loop.
Multisampling, multiple color attachments, and broad GLES framebuffer
semantics are not implemented.

RinGL is not a GLES conformance claim. An exact backend
`RINGL_RIN_GPU_ERROR_DEVICE_LOST` now makes the context sticky-lost: ordinary
entry points stop observing it, `ringl_get_error()` returns
`CONTEXT_LOST_WEBGL` once, and subsequent calls cannot mutate its GL state.
The RinOS Ladybird WebGL 1 embedding latches that browser-visible state and
emits one canvas `webglcontextlost` event before script can re-enter it. Context
restoration, `webglcontextrestored`, and broader shader expressions remain
unfinished. No API or ABI stability guarantee is made yet.

The raw shader path now has a bounded scalar/vector/matrix uniform execution
profile. A linked program retains `float`, `vec2`, `vec3`, `vec4`, and bounded
`mat2`/`mat3`/`mat4` values independently from the shader objects it shares with other
programs. Each `ringl_uniform_{1,2,3,4}f()` or
`ringl_uniform_matrix{2,3,4}fv()` update first lowers a new program-owned RSH1 pair
containing the corresponding Float32 `CONST_F32` instructions, asks RinGPU to
validate/create both replacement modules, then invalidates the old pipeline
and publishes the new executable. Thus an update affects the actual RinGPU
draw without textual source replacement or a CPU color fallback. The accepted
generic no-varying forms include `vec4(tint2, 0.0, 1.0)` for a `vec2`,
`vec4(tint3, 1.0)` for a `vec3`, a direct `vec4` read, and vertex
`gl_Position = transform * position` for one matching `uniform mat2`/`mat3`/
`mat4` and `attribute vec2`/`vec3`/`vec4`, as well as bounded no-varying vector
locals and component-wise arithmetic. The generic form has no varyings and one
matrix per type/location; arrays, other matrix expressions, and matrix/vector
combinations with the specialized varying/texture profiles remain unavailable
rather than being reported as successful GLES.
