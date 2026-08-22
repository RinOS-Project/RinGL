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

For example, blend state, depth state, the linked program, vertex layout, attachment formats, and other draw-relevant state can form a pipeline-cache key. Changing `glEnable(GL_BLEND)` should invalidate the derived pipeline state rather than immediately emit a GPU command.

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

## Current bounded texture-coordinate extension

The shared perspective-UV texture profile now executes one through eight
left-to-right `texture2D()` calls with a coordinate of `uv`,
`uv + vec2(finite, finite)`, or `uv - vec2(finite, finite)`. Every offset is
lowered directly to public RSH1 `CONST_F32` and `ADD_F32`/`SUB_F32` operations
before its real RinGPU sample; it is not folded into a host-side shortcut.
This remains a deliberately narrow GLSL ES subset: local coordinates, arbitrary
vector expressions, multiple UV varyings, and more than eight calls are still
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
`COPY_SOURCE`. A matching `DEPTH_COMPONENT32F` renderbuffer or level-zero
`DEPTH_COMPONENT32F`/`DEPTH_COMPONENT`/`FLOAT` texture can now be attached as
`DEPTH_ATTACHMENT`; the pair is rejected on an invalid attachment or dimension
mismatch, and its D32 image is realized lazily as a RinGPU depth target. The
RinOS surface integration test verifies a depth-only clear while preserving
color and depth-tested triangle output from either custom FBO kind. The
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

Program shader references now follow the deletion lifetime required by browser
code: deleting an attached shader marks its public name deleted while retaining
the compiled object for later linking; `ringl_detach_shader()` or program
destruction releases the final pending reference. A linked executable keeps
using its linked shader pair when later attach/detach calls change the pending
link inputs.

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

Custom RGBA8 renderbuffer FBOs may additionally attach a matching
`DEPTH24_STENCIL8` renderbuffer or level-zero
`DEPTH24_STENCIL8`/`DEPTH_STENCIL`/`UNSIGNED_INT_24_8` texture through
`DEPTH_STENCIL_ATTACHMENT`. RinGL converts the packed 24/8 texture input into
RinGPU's F32 depth/S8 stencil storage and executes independent front/back
stencil tests, reference/read/write masks, all eight stencil operations, and
stencil clear before the depth test. The common `ringl_stencil_*` calls update
both faces, while the `*_separate` forms set one face or both explicitly. This
is tested through the RinOS RinGL-to-RinGPU surface path, including stencil
rejection, replacement, write masking, depth-fail behavior, reversed winding,
and back-face culling. `ringl_clear(RINGL_STENCIL_BUFFER_BIT)` also forwards
the front stencil write mask through the native render pass, so it preserves
masked-off stencil bits rather than overwriting the complete S8 plane.

`ringl_clear()` also carries a bounded lower-left clear region and an RGBA
write mask to RinGPU. An enabled WebGL scissor clips color, depth, and stencil
clears together; masked color channels and disabled depth writes preserve their
previous storage, and requested depth/stencil clears without a matching
attachment are no-ops. The RinOS surface backend applies these rules to both
the caller-owned BGRA target and offscreen RGBA FBO targets.

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

`copyTexSubImage2D` has a bounded data-movement path from the current complete
RGBA color target, including a complete RGBA8 texture/renderbuffer FBO, into a
defined level-zero `RGBA` texture. It validates FBO completeness and both source
and destination rectangles before allocating a temporary RGBA snapshot, uses the
existing fenced RinGPU readback path (including default-framebuffer
BGRA-to-RGBA swizzle), and updates the canonical texture shadow only after that
readback succeeds. Non-RGBA destination textures and out-of-range rectangles
are rejected; custom FBO depth/stencil readback, multisample, and other
unrepresented copy semantics remain outside the current profile.

`copyTexImage2D` uses the same fenced snapshot to replace a bound texture with
a new level-zero `RGBA` definition. Its full source rectangle, FBO completeness,
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
Depth and depth-stencil textures are attachment-only in this slice:
depth-texture sampling, multisampling, multiple color attachments, and broad
GLES framebuffer semantics are not implemented.

RinGL is not a GLES conformance claim. An exact backend
`RINGL_RIN_GPU_ERROR_DEVICE_LOST` now makes the context sticky-lost: ordinary
entry points stop observing it, `ringl_get_error()` returns
`CONTEXT_LOST_WEBGL` once, and subsequent calls cannot mutate its GL state.
Browser `webglcontextlost` dispatch, restoration, and broader shader expressions
remain unfinished. No API or ABI stability guarantee is made yet.
