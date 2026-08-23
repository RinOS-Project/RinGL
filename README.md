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

## RinGPU translation model

OpenGL exposes mutable, implicit state while RinGPU is intentionally explicit. RinGL therefore maintains a context-side state machine and derives backend objects lazily.

For example, blend state, depth state, the linked program, vertex layout, attachment formats, and other draw-relevant state can form a pipeline-cache key. Changing `glEnable(GL_BLEND)` should invalidate the derived pipeline state rather than immediately emit a GPU command. The blend key includes the clamped RGBA `ringl_blend_color()` value. Constant blend factors use an additive native-pipeline V2 descriptor and optional callback tail, preserving the V1 descriptor for existing RinGPU embeddings; an embedding without that V2 path rejects the draw instead of substituting a zero blend constant.

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

## Current packed-color framebuffer slice

`RINGL_RGB565`, `RINGL_RGBA4`, and `RINGL_RGB5_A1` renderbuffers realize as
native two-byte `RIN_GPU_FORMAT_*_UNORM` color images, not RGBA8 substitutes.
RinGL reports their logical 5/6/5/0, 4/4/4/4, and 5/5/5/1 component sizes, uses
each physical format in its pipeline key, and returns WebGL RGBA/UNSIGNED_BYTE
readback through private native staging. The RinGPU software and
Aquamarine-surface backends preserve 16-bit storage for upload, clear, draw,
sampling, and readback.

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
it is not folded into a host-side shortcut. The bounded two-UV profile maps
`firstUv` and `secondUv` to separate RSH1 perspective input pairs and also
supports `firstUv + secondUv` and `firstUv - secondUv` as sampler coordinates.
Up to six fragment-local values may be chained from the shared UV; each
initializer is lowered in source order before samples. This remains a
deliberately narrow GLSL ES subset: other local vector expressions, more than
six locals, more than two UV varyings, and more than eight calls are still
unsupported.

See [TODO.md](TODO.md) for the implementation roadmap.

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
validation status, attached shader count, and linked active attribute/sampler
uniform counts. `ringl_validate_program()` records successful validation only
for a linked executable; each new link resets that state before it validates
the replacement executable. The query validates its ABI header and only writes
the caller-owned structure after it has validated the program, so an invalid
program cannot partially publish stale metadata.

The current bounded uniform profile consists of linked `sampler2D` values.
`ringl_get_uniform_1i()` reads the selected texture unit for a specific linked
program and location without depending on the current program binding. It
checks the complete input before writing its caller-owned integer, so invalid
programs or locations cannot expose a partially updated result.

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

`RinGLBlendColorV1` separately snapshots the finite, clamped blend constant
that RinGL resolves into RinGPU's V2 pipeline descriptor. Its versioned input
is validated before the complete snapshot is copied out, so a browser can
answer `BLEND_COLOR` without reusing clear-color state or exposing internals.

`ringl_depth_range()` retains the OpenGL depth-range state separately from the
viewport. It rejects non-finite inputs without changing state, clamps each
finite endpoint independently to `[0,1]`, and preserves reversed ranges.
`RinGLDepthRangeV1` exposes that state through a versioned, failure-atomic
snapshot. Draw translation passes the exact pair through RinGPU raster state,
rather than treating every range as the default `[0,1]`.

`RINGL_POLYGON_OFFSET_FILL` and `ringl_polygon_offset()` carry finite factor
and units values in the dynamic raster state. The RinGPU V2 descriptor keeps
the V1 default disabled, while the Aquamarine surface backend applies
`m * factor + 2^-23 * units` to filled-triangle depth immediately before depth
comparison and writing. Points and lines are not offset; non-finite inputs
leave the existing RinGL state intact and report `INVALID_VALUE`.

`ringl_line_width()` accepts finite aliased widths from one through 64 pixels.
RinGPU's V3 raster descriptor preserves V1/V2's one-pixel default, and the
Aquamarine backend rasterizes bounded unique coverage for line lists, strips,
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
`GENERATE_MIPMAP_HINT` with standard hint modes as an advisory no-op because
this bounded profile has no generated mip chain; derivative hints are not
claimed as supported.

Linked program reflection is also exposed without borrowing RinGL storage.
`ringl_get_active_attrib()` and `ringl_get_active_uniform()` copy one bounded
entry into `RinGLActiveInfoV1` only after validating its versioned output
header. The current profile reports scalar/`vec2`/`vec3`/`vec4` float
attributes and `sampler2D` uniforms; unlinked programs or out-of-range indices
record the appropriate error and leave caller storage unchanged.

`RINGL_POINTS`, `RINGL_LINES`, `RINGL_LINE_STRIP`, `RINGL_LINE_LOOP`,
`RINGL_TRIANGLES`, `RINGL_TRIANGLE_STRIP`, and `RINGL_TRIANGLE_FAN` map to
RinGPU's native point-list, line-list, line-strip, line-loop, triangle-list,
triangle-strip, and triangle-fan topologies. The primitive is part of RinGL's
pipeline-cache key, so these draws cannot reuse a different primitive's
pipeline. Both direct and indexed draws use the existing vertex and index
validation paths before they reach the embedding; an incomplete line-list
pair, a line strip or line loop with fewer than two vertices, and a triangle
strip or fan with fewer than three vertices are successful no-ops. Public
WebGL binding and presentation remain unsupported.

Custom RGBA8 renderbuffer FBOs may additionally attach a matching
`DEPTH24_STENCIL8` renderbuffer or level-zero
`DEPTH24_STENCIL8`/`DEPTH_STENCIL`/`UNSIGNED_INT_24_8` texture through
`DEPTH_STENCIL_ATTACHMENT`, or attach a D24S8 or WebGL 1 `STENCIL_INDEX8`
renderbuffer through `STENCIL_ATTACHMENT` alone. `STENCIL_INDEX8` retains its
logical stencil-only format and realizes a D32S8 RinGPU target whose depth
plane stays logically inaccessible. RinGL converts the packed 24/8 texture
input into RinGPU's F32 depth/S8 stencil storage and executes independent front/back
stencil tests, reference/read/write masks, all eight stencil operations, and
stencil clear before the depth test. A stencil-only attachment still uses its
D32S8 image for the native render pass, but it suppresses logical depth test,
depth write, and depth clear; a physical depth plane never becomes an
accidental depth attachment. The common `ringl_stencil_*` calls update both
faces, while the `*_separate` forms set one face or both explicitly. This is
tested through the RinOS RinGL-to-RinGPU surface path, including stencil
rejection, replacement, write masking, depth-fail behavior, reversed winding,
back-face culling, and an enabled `DEPTH_TEST` with `NEVER` on a stencil-only
FBO. `ringl_clear(RINGL_STENCIL_BUFFER_BIT)` also forwards the front stencil
write mask through the native render pass, so it preserves masked-off stencil
bits rather than overwriting the complete S8 plane. Separate depth and stencil
images, depth-only D24S8 attachment, multisample storage, and resolve remain
outside this bounded profile.

`ringl_get_framebuffer_attachment()` is the versioned, caller-owned query for
the bounded custom-FBO model. It reports the actual current
`COLOR_ATTACHMENT0`, `DEPTH_ATTACHMENT`, `STENCIL_ATTACHMENT`, or combined
`DEPTH_STENCIL_ATTACHMENT` object kind, name, and texture level without
realizing a RinGPU image for observation. A combined query returns no object
unless both logical aspects share one attachment. The older color-only helper
is retained as a compatibility shorthand; new embeddings should use the
attachment-point query. Texture cube faces, mip levels beyond zero, separate
depth/stencil images, and multisample attachments remain outside the profile.

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
the bounded RGBA8 image into the resource-aware software executor and applies
nearest or linear filtering with clamp-to-edge, repeat, or mirrored-repeat
addressing. The level-zero executor has no derivatives or mip levels, so a
texture larger than 1x1 requires matching minification and magnification
filters; it rejects ambiguous min/mag selection rather than silently choosing
one. The strict C11 RinGL tests cover format normalization, tightly-packed RGB
image/sub-image data, and padded D32 source rows; the surface integration test
renders a normalized RGB texture through the real RinGPU resource binding.

Embeddings handling browser-facing byte uploads must use
`ringl_tex_image_2d_from_bytes()` and `ringl_tex_sub_image_2d_from_bytes()`.
Those APIs take the source extent and
validate every readable row, including unpack-alignment padding but excluding
unused padding after the final row, before allocating or changing texture
shadow storage. A short byte span produces `INVALID_VALUE` with the prior
definition intact. The older raw-pointer texture APIs remain only for trusted
native callers that can prove the source extent independently.

`copyTexSubImage2D` has a bounded data-movement path from the current complete
color target, including a complete RGBA8 texture/renderbuffer FBO or a packed
RGB565/RGBA4/RGB5_A1 renderbuffer FBO, into a defined level-zero `RGBA` texture.
It validates FBO completeness and both source
and destination rectangles before allocating a temporary RGBA snapshot, uses the
existing fenced RinGPU readback path (including default-framebuffer
BGRA-to-RGBA swizzle), and updates the canonical texture shadow only after that
readback succeeds. Non-RGBA destination textures and out-of-range rectangles
are rejected; custom FBO depth/stencil readback, multisample, and other
unrepresented copy semantics remain outside the current profile.

`copyTexImage2D` uses the same fenced snapshot from those complete color targets
to replace a bound texture with a new level-zero `RGBA` definition. Its full
source rectangle, FBO completeness,
and destination limits are validated before readback; the previous texture
definition and realized RinGPU image remain intact unless snapshot completion
succeeds. This profile does not yet define zero-sized, non-RGBA, depth/stencil,
or multisample copy definitions.

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
`gl_FragColor = vertexColor`. RinGL expands the two attributes to six scalar
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
`varying vec3 vertexColor`, with `gl_FragColor = vec4(vertexColor, 1.0)`.
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
Browser `webglcontextlost` dispatch, restoration, and broader shader expressions
remain unfinished. No API or ABI stability guarantee is made yet.
