# RinGPU integration boundary

RinGL is a user-space OpenGL/OpenGL ES compatibility layer. It does not discover a global GPU device by reaching into RinGPU internals and it does not expose RinGPU implementation objects as GL objects.

The embedding runtime creates or selects the RinGPU execution environment, then supplies RinGL with a versioned `RinGLRinGpuBindingV1` when a context is created. The binding contains an embedding-owned opaque session, a small operation table, the selected graphics queue handle, and the queue capability snapshot.

The context copies both the fixed-width binding and the caller-provided prefix of the v1 operation table, so older compatible tables remain valid and the caller's descriptors may be temporary. Ownership of the opaque session remains with the embedding runtime.

## Public visible-upload path

RinGPU exposes the CPU-visible upload primitive in its public ABI. A buffer created for immediate host upload uses `RIN_GPU_BUFFER_CPU_VISIBLE` and must also declare `RIN_GPU_BUFFER_COPY_DESTINATION`; host bytes are written with `ringpu_upload_buffer()`.

That means the RinOS implementation of RinGL's current `upload_buffer` adapter does not need a private memory-runtime or vendor-specific path. It is a thin adapter over the public RinGPU contract:

```text
RinGL ringl_buffer_data()
        |
        v
create RinGPU buffer
  CPU_VISIBLE + COPY_DESTINATION
        |
        v
ringpu_upload_buffer()
        |
        v
published as GL buffer storage
```

Browser and other untrusted embeddings use
`ringl_buffer_data_from_bytes()` and
`ringl_buffer_sub_data_from_bytes()`, not the raw-pointer forms. Each call
carries the source span length. A non-null source shorter than its requested GL
range, or a null source with a nonzero span length, reports `INVALID_VALUE`
before allocating/replacing the CPU shadow or calling RinGPU. The staged
replacement then makes the backend observe only RinGL-owned validated bytes.
The original pointer-only APIs remain compatibility entry points for trusted
native callers.

## Bounded texture-import path

`ringl_tex_image_2d_from_bytes()` and
`ringl_tex_sub_image_2d_from_bytes()` accept a source pointer together with its
byte extent. Before they allocate, replace, or patch CPU texture shadow storage,
they calculate the exact bytes that valid source rows can read under the current
`RINGL_UNPACK_ALIGNMENT`. This includes inter-row padding and excludes padding
after the final row. A short source reports `RINGL_INVALID_VALUE`, preserving
the previous definition. Browser/other untrusted embeddings must use these
bounded APIs; the older raw-pointer forms are compatibility entry points for
trusted native callers only.

For RinOS WebGL 1, the same bounded path also normalizes
`DEPTH_COMPONENT`/`UNSIGNED_SHORT` and `DEPTH_COMPONENT`/`UNSIGNED_INT`
uploads into the internal Float32 D32 shadow, with unaligned source values read
by byte copy before normalization. `DEPTH_STENCIL`/`UNSIGNED_INT_24_8` maps to
the existing D32/S8 split storage. A short depth source, an unsupported type,
or a failed conversion leaves the previous texture definition and realized
RinGPU image intact; there is no fallback to a raw Aquamarine texture command.

## Bounded color-readback path

`ringl_read_pixels_to_bytes()` accepts a destination capacity for the current
complete color target's tightly packed RGBA output. For an RGBA8/BGRA8 target it
reads directly into that output; for native `RGB565_UNORM`, `RGBA4_UNORM`, or
`RGB5_A1_UNORM` targets it first reads two-byte native texels into private
staging and expands them to RGBA (opaque alpha only for RGB565). It rejects a
short destination with `RINGL_INVALID_OPERATION`
before a RinGPU command list is created, the target transitions to
`COPY_SOURCE`, or destination memory is written. A staging-allocation or
readback failure likewise leaves the caller output untouched. Browser embeddings
must use this API; `ringl_read_pixels()` remains a trusted native-only
compatibility form because it cannot determine destination capacity from a raw
pointer.

The callback boundary remains useful because RinGL is a standalone repository and should not hard-wire an OS-Core internal session type into its portable context ABI.

## OS-Core adapter

OS-Core now contains `src/webengine/rin_ringl_ringpu_adapter.{h,c}`. The adapter owns no RinGPU objects itself. It borrows a `RinGpuCore*` and maps the RinGL v1 operation table directly onto the public RinGPU API.

The first-slice mappings are:

- buffer creation/upload/destruction -> `ringpu_create_buffer()`, `ringpu_upload_buffer()`, `ringpu_destroy()`;
- shader modules -> `ringpu_create_shader_module()`;
- vertex graphics pipelines -> `ringpu_create_graphics_pipeline_vertex()` or
  `ringpu_create_graphics_pipeline_vertex_bindings()`;
- command lists -> `ringpu_create_command_list()` and `ringpu_command_list_reset()`;
- image transitions -> `ringpu_command_transition_image()`;
- render passes -> `ringpu_command_begin_render_pass()` / `ringpu_command_end_render_pass()`;
- vertex drawing -> `ringpu_command_draw_vertices()` or
  `ringpu_command_draw_vertices_v2()`;
- indexed vertex drawing -> `ringpu_command_draw_indexed()` or
  `ringpu_command_draw_indexed_v2()`;
- presentation -> `ringpu_command_present()`;
- submission -> `ringpu_command_list_close()` and `ringpu_queue_submit()`.

The WebEngine CMake integration is opt-in through `RIN_LADYBIRD_ENABLE_RINGL`.
Its default `RIN_RINGL_SOURCE_ROOT` is the checked-out `libs/RinGL` tree and
can be overridden only for an explicitly selected external checkout.

The opt-in `ringl_aquamarine_surface` embedding owns a caller-provided BGRA/D32/S8 target and exposes a private native view of its RinGPU core, graphics queue, and default images. OS-Core's `rin_webgl_ringl_bridge.{h,c}` creates a RinGL context using that view and binds the images as RinGL's default framebuffer. The embedding remains the owner of the core, queue, images, and caller-provided pixel backing store.

`RinGLDefaultFramebufferV1.flags` provides the browser-facing logical
depth/stencil contract. A zero flag word retains the original native
format-derived behavior. An embedding that supplies a shared D32/S8 allocation
for stencil sets `RINGL_DEFAULT_FRAMEBUFFER_EXPLICIT_ASPECTS` together with
`RINGL_DEFAULT_FRAMEBUFFER_STENCIL`; RinGL
then preserves the physical D32 plane but exposes no depth attachment to GL.
`RINGL_DEFAULT_FRAMEBUFFER_DEPTH` and the combined mask are likewise explicit;
the explicit bit alone deliberately exposes neither physical plane. An invalid
D32/stencil combination is rejected before the framebuffer is published. This
prevents `getContext({ depth: false, stencil: true })` from gaining depth
testing merely because its storage must carry both physical planes.

During the bridge lifetime RinGL is the exclusive graphics command producer for the embedding. The embedding has no public clear/draw/present entry point: it only prepares the initial image state and records state after a successful RinGL submission.

The bridge creates an unflagged native RinGL context. WebGL 1 attachment policy
lives in the Ladybird embedding: it queries the live depth and stencil
attachments, reports a distinct pair as `FRAMEBUFFER_UNSUPPORTED`, and rejects
draw/read/copy/clear before native submission. Query failure also closes that
path. RinGL and RinGPU therefore retain their native separate depth/stencil
render-pass capability, while an identical object and level (including shared
D24S8) remains a complete RinGPU-backed WebGL framebuffer.

The surface color image declares `COPY_SOURCE` and `CPU_READABLE` as well as
its existing `COPY_DESTINATION`, `COLOR_TARGET`, and `PRESENT` uses. Its
backend supplies bounded readback, so RinGL's complete RGBA color-target
`RGBA/UNSIGNED_BYTE` `readPixels` path can transition to `COPY_SOURCE`, wait,
read back, and swizzle default-framebuffer BGRA storage to RGBA. The same fenced
snapshot powers `copyTexSubImage2D` and `copyTexImage2D` from either the default
color buffer or a complete RGBA8/packed RGB565/RGBA4/RGB5_A1
texture/renderbuffer FBO. `copyTexSubImage2D` writes defined
RGBA/RGB/ALPHA/LUMINANCE/LUMINANCE_ALPHA or packed texture levels, including
explicit nonzero mips; canonical formats apply their GL component expansion and
packed destinations are quantized from the canonical snapshot only after
readback succeeds. FBO completeness and source/destination rectangles are
checked before temporary allocation. `copyTexImage2D` retains a prior texture definition/image until the
replacement snapshot completes, then defines
RGBA/RGB/ALPHA/LUMINANCE/LUMINANCE_ALPHA or native RGB565/RGBA4/RGB5_A1 storage
at level zero or an exact explicit nonzero mip. The latter requires a defined
same-format base. Canonical formats apply their component expansion and packed
output is quantized directly from the canonical snapshot. The
focused OS-Core `rin_webgl_ringl_bridge_test` and
`rin_webgl_ringl_framebuffer_test` cover clear, readback, and packed
level-zero/level-one copy definitions through this borrowed surface. The latter
executes draw -> copy -> copied-FBO readback -> copied-FBO clear -> flush/finish
-> source-FBO readback, verifying that `COPY_SOURCE`/`COLOR_TARGET` pass
boundaries retain both attachments rather than aliasing the source and copy.
Depth/stencil, multisample, and other FBO copy semantics remain outside this
slice.

The strict RinGL sync test records the corresponding callback-level ordering:
command-list create/reset, image transition, render-pass begin/end, immediate
submit, fenced submit/wait, and readback for clear → copy → clear → flush →
finish → readback. It requires `flush` to leave the trace unchanged because
current RinGL submits observable render work immediately.

The same level-zero sampled-image path accepts WebGL 1 packed texture input:
`RGB`/`UNSIGNED_SHORT_5_6_5`, `RGBA`/`UNSIGNED_SHORT_4_4_4_4`, and
`RGBA`/`UNSIGNED_SHORT_5_5_5_1` retain native RGB565/RGBA4/RGB5_A1 two-byte
storage and create their matching RinGPU image format. The surface sample table
expands texels only at execution time, with opaque RGB565 alpha and preserved
RGBA4/RGB5_A1 alpha.

## Dither raster state

RinGL owns `RINGL_DITHER` as an OpenGL ES default-enabled capability. Its
optional V8 operation-table suffix carries `RinGLRinGpuRasterStateV2`, which
extends the existing raster-state prefix with `dither_enabled`. The OS-Core
adapter maps that field to public `RinGpuRasterStateV5`; no reserved field is
overloaded. A V1-only embedding may receive the compatible default-enabled
prefix, but an explicit `ringl_disable(RINGL_DITHER)` fails the draw if the V8
callback is unavailable rather than silently using an unknown backend default.

The Aquamarine embedding applies a deterministic 4x4 ordered threshold only
when a fragment is quantized into RGB565, RGBA4, or RGB5_A1 storage. The
threshold uses the public lower-left pixel origin, RGB components only, and is
applied after blend/color-mask resolution. Clear is deliberately uniform: the
GLES clear operation does not dither. The generic RinGPU software executor
uses the same state rule, while the real bridge regression verifies the
enabled/disabled packed readback difference through RinGL and the private
surface.

## Optional multi-mip image path

`RinGLRinGpuOpsV1` has an optional V4 tail with paired
`create_image_2d_mip_v2` and `upload_image_2d_mip_v2` callbacks.  It uses
separate V2 descriptor records rather than extending the old unversioned image
records, so a V1-only backend remains ABI-safe and continues to handle
level-zero textures.  When both callbacks are present,
`ringl_generate_mipmap()` and explicit nonzero-level `texImage2D` uploads
create a CPU-visible sampled image with its exact number of contiguous 2D
levels and upload each level with an explicit `mip_level`.

The generated source is presently the bounded color profile whose shadow
representation is normalized RGBA8 or native RGB565/RGBA4/RGB5_A1. RinGL
computes a deterministic clamped 2x2 box level on the CPU before replacing
prior generated storage; packed levels average their stored component values
then repack once at the destination. A missing V2 callback or allocation
failure leaves that prior chain untouched. Explicit nonzero levels are bounded
to exact-dimension same-format color storage; `texSubImage2D` can update such
a level and base-level updates retain manually defined levels while discarding
only generated ones. D32 and D24S8 levels use the same exact-dimension storage
rule but are manually defined only. The optional V5 tail selects a color mip
for transition, render pass, direct and multi-buffer draw, and readback. The
V6 tail supplies the color/depth/stencil mip selections for depth passes,
including combined D24S8.
The appended V7 `create_graphics_bind_group_v2` callback identifies a sampled
mip-chain without changing the V1 binding layout. For a mipmap minification
filter RinGL transitions every contiguous level to `SHADER_READ` and requires
V7; it neither binds only level zero nor submits an ambiguous draw to an older
backend. The OS-Core adapter maps the flag to the public RinGPU typed binding,
whose core validates every exposed subresource and rejects render-target alias
or a non-shader-read level. Aquamarine snapshots the complete chain and the
software executor derives perspective-correct implicit gradients, applies
sampler bias/min/max LOD, and executes nearest/linear mip selection with the
independent min/mag texel filters. RGB565/RGBA4/RGB5_A1 generated chains keep
their native packed storage and average stored component precision before
repacking. The V5 color-subresource records execute their nonzero packed levels
as FBO targets for clear, native draw, and RGBA readback. Focused fake and real
bridge tests inspect unpacked and packed level bytes, the RinGPU descriptor,
packed mip FBO output, and the distinct level-one colour selected from a
three-level texture.

## Shader module validation path

RinGL lowers its current scalar GLSL ES subset to RinShader RSH1 before asking the embedding adapter to create a GPU shader module. The `create_shader_module` adapter maps directly to public `ringpu_create_shader_module()`.

RinGPU's public shader-module creation path snapshots the supplied RSH1 and calls `ringpu_shader_validate()` before invoking the backend's shader-module creation callback. RinGL therefore reuses the canonical RinShader validator instead of maintaining a second backend-facing validator with subtly different rules.

```text
GLSL ES
  |
  v
RinGL parser / semantic checks
  |
  v
RinGL scalar RSH1 lowering
  |
  v
create_shader_module adapter
  |
  v
ringpu_create_shader_module()
  |
  +--> ringpu_shader_validate()
  |       reject malformed RSH1
  v
RinGPU backend shader module
```

`ringl_realize_shader_module()` is transactional. A newly created RinGPU module replaces the previous module only after creation succeeds. Replacing shader source, recompiling, re-lowering, deleting the shader, or destroying the context invalidates and releases stale realized modules and pipelines that reference them.

## Graphics pipeline path

RinGL builds a deterministic first-slice pipeline key from the linked
vertex/fragment shader modules, color attachment format, triangle-list
topology, resolved vertex bindings/strides, and resolved vertex attributes. Active program
attributes are selected through their linked generic GL locations, then emitted
as dense scalar RSH1/RinGPU inputs. This means `bindAttribLocation` affects the
actual vertex fetch source without exposing sparse GL indices to RinGPU. A
bounded cache reuses identical pipelines and evicts old entries in FIFO order.

The adapter's V1 `create_graphics_pipeline` callback maps directly to
`ringpu_create_graphics_pipeline_vertex()`. Its additive V2 callbacks map
multi-stream layouts to `ringpu_create_graphics_pipeline_vertex_bindings()` or
the native-state counterpart, converting each `RinGLRinGpuVertexAttributeV2`
and dense `RinGLRinGpuVertexBufferLayoutV1` while preserving linked shader
module handles. `RinGLRinGpuVertexBufferLayoutV1.flags` is a binding divisor:
zero advances per vertex, otherwise fetch uses `floor(instance / flags)`. A
single stream with a nonzero divisor deliberately uses this V2 layout rather
than the legacy one-buffer ABI, so the adapter copies the divisor into
`RinGpuVertexBufferLayoutV1.flags` and the RinGPU backend can validate the
last required instance element before submission.

The binding must advertise `RINGL_RIN_GPU_VERTEX_INPUT_CONSTANT_FLOAT32` before
RinGL emits a disabled active generic attribute. RinGL then marks the scalar
descriptor as constant and stores the exact IEEE-754 binary32 bits in its
offset field; the RinOS adapter maps that contract to
`RIN_GPU_VERTEX_ATTRIBUTE_CONSTANT_FLOAT32`. RinGPU permits zero vertex stride
only when every attribute is constant, and that draw has no vertex buffer or
vertex offset. Mixed streamed and constant inputs use the same V2 descriptor
when more than one captured `(buffer, effective stride)` pair is active. The
binding must advertise `RINGL_RIN_GPU_VERTEX_INPUT_MULTI_BUFFER` and provide
the matching create/draw callbacks; otherwise RinGL rejects the draw before it
records a command. This explicit capability prevents an older callback from
interpreting Float32 bits as a buffer offset or silently collapsing streams.

Fragment sampled resources are emitted as adjacent RSH1 image/sampler pairs.
For every linked sampler RinGL realizes the selected texture unit, validates it
is not the active color target, creates one typed graphics bind group containing
all pairs, and records each distinct image transition to `SHADER_READ` before
the render pass. Its texture state is published only after the queue accepts the
submission. The current GLSL lowerer generates a bounded one-through-eight-call
constant-coordinate addition chain and compacts its active sampler declarations
into the complete reflected pair table. RinGL retains that declaration map and
builds a selective typed bind group by linked uniform name, while the operation
table and RinGPU surface backend validate every resulting pair rather than
silently selecting only its first entry.

For the bounded two-`varying vec2` texture profile, a named local
`vec2 mixedUv = firstUv +/- secondUv` is emitted as two component-wise RSH1
operations over the distinct interpolated input pairs before its sample. It
does not materialize coordinates on the host or collapse either varying pair;
the normal RinGPU sampled-image binding and surface executor consume the live
arithmetic result. Other local vector expressions remain outside this profile.
The combined local may feed the same direct/finite-affine local chain as the
single-UV profile, up to eight declared locals when the complete shape remains
inside the RSH1 instruction/register limits; each subsequent RSH1 stage reads
the preceding result rather than a host-side folded coordinate. The lowerer
counts all locals, calls, optional call-local offsets, combines, stores, and
color operations before it publishes the RSH1 blob.

The bounded native interface supports three independent `varying vec2` texture
coordinates with six scalar fragment inputs and a 10-scalar vertex output
(clip `xyzw` plus those pairs). It also supports a direct four-`varying vec2`
shape with eight scalar fragment inputs and a 12-scalar vertex output. The
RinGPU surface keeps up to eight varying components in a private native
clip/raster form while preserving the public compact RGBA clip-vertex V1 ABI.
Six-plane clipping and perspective interpolation operate on every carried pair
before resource-aware fragment preflight and submission. This route is
deliberately limited to direct/indexed points, lines, line strips/loops, and
triangle lists, strips, and fans. One
`firstUv +/- secondUv`,
`firstUv +/- thirdUv`, or `secondUv +/- thirdUv` local may feed following
direct/finite-affine local declarations in source order when the complete RSH1
shape fits its instruction/register budget, while direct samples may use every
declared pair. It does not claim general varying transport, expressions that
combine all three pairs in one local, or broader three-input local expressions.
The four-UV shape permits independent direct samples plus one named local that
combines any two distinct pairs with `+` or `-`; it can feed the existing
finite-affine local chain. Broader four-UV expressions are not inferred. For
one sampled texture result, or an explicitly parenthesized additive sample
chain, a finite `vec4` color literal is emitted as four RSH1 constants followed
by component-wise `ADD_F32`, `SUB_F32`, `MUL_F32`, or nonzero `DIV_F32`
instructions before the fragment stores its output. This uses the existing
public RinGPU arithmetic execution path; unparenthesized multi-sample precedence
is deliberately not inferred and zero division components are rejected early.
For `+`, `-`, and `*`, the finite literal may also lead one sample or an
explicitly parenthesized additive chain; its RSH1 register is then source0, so
subtraction retains GLSL operand order. Leading division uses the same operand
order. RinGPU preflights every covered fragment, so a sampled zero divisor
rejects the complete draw before a color/depth/stencil target is modified.

Before cache eviction during a draw, RinGL resets its reusable command list. This releases references retained by the previous recorded submission before an old pipeline is destroyed.

## Default framebuffer and command path

The embedding runtime owns the presentable image. It binds or replaces that image with `ringl_set_default_framebuffer()`, supplying the RinGPU image handle, format, dimensions, and display id. `ringl_set_default_framebuffer_state()` then tells RinGL whether a borrowed image currently starts as `COLOR_TARGET` or `PRESENT`. When the WebEngine bridge borrows a freshly-created `UNDEFINED` image, it first invokes the surface owner's `begin_content_update()` operation. This records the real `UNDEFINED -> COLOR_TARGET` transition without clearing or presenting uninitialized content, then passes the observed `COLOR_TARGET` state to RinGL.

The initial command path intentionally mirrors public RinGPU operations rather than hiding them behind a GL-shaped backend call:

```text
ringl_clear()
  reset/create command list
  current state -> COLOR_TARGET when required
  begin render pass (CLEAR)
  end render pass
  close + queue submit

ringl_draw_arrays(GL_TRIANGLES)
  reset/create command list
  resolve/validate vertex fetch
  lookup/create graphics pipeline
  current state -> COLOR_TARGET when required
  begin render pass (LOAD)
  ringpu_command_draw_vertices equivalent
  end render pass
  close + queue submit

ringl_present()
  reset/create command list
  COLOR_TARGET -> PRESENT when required
  present command
  close + queue submit
```

RinGL reuses one graphics command list per context. Each new submission resets it before recording, preserving GL ordering while bounding command-list allocation. The context destroys the command list before cached pipelines and shader modules so RinGPU recorded-command references are released in dependency order.

## Current visible-triangle status

The selected `RinGLAquamarineSurfaceContext` backend executes generic
`RIN_GPU_BACKEND_COMMAND_DRAW_VERTICES` into the caller-owned BGRA surface;
the OS-Core surface test covers that native command path. RinGL's current
vector position and initial texture/varying profiles lower through the same
public RinGPU adapter. The bounded direct `uniform vec4` profile also creates
program-owned RSH1 modules and has the same adapter create/validate them before
the previous graphics pipeline is retired; the focused surface test changes
the tint twice and reads both real colors back. The browser-facing GLES/WebGL object and command bridge,
front-buffer presentation contract, context-loss policy, and product/QEMU
evidence remain separate unfinished work; no WebGL feature claim follows from
this integration slice.

## Host operation table

The v1 callbacks are append-only. The current groups are:

- buffer storage: `create_buffer`, `upload_buffer`, `destroy_object`;
- shader realization: `create_shader_module`;
- graphics pipeline realization: `create_graphics_pipeline`;
- graphics commands: `create_command_list`, `reset_command_list`, `transition_image`, `begin_render_pass`, `draw_vertices`, `end_render_pass`, `present`, `close_command_list`, and `queue_submit`.

Shader, pipeline, and command callbacks are optional for validation-only contexts. Operations that require a missing capability report a GL-visible failure instead of silently falling back to a software renderer.

`ringl_buffer_data()` uses the buffer callbacks transactionally. It creates and optionally uploads replacement storage first; only after both operations succeed does it destroy the previous backing buffer and publish the new storage in GL state. A failed replacement therefore leaves the previous GL buffer storage intact.

## Device loss

`RINGL_RIN_GPU_ERROR_DEVICE_LOST` is the exact result value shared with
RinGPU's public `RIN_GPU_ERROR_DEVICE_LOST`. RinGL treats that value specially
from every v1 command callback and from the optional fence/readback callbacks:
it latches the context as lost, discards any ordinary pending GL error, and
prevents all subsequent ordinary entry points from accessing or changing the
context. `ringl_get_error()` then returns `RINGL_CONTEXT_LOST_WEBGL` once and
returns `RINGL_NO_ERROR` thereafter. `ringl_context_is_lost()` lets a trusted
embedding observe the latched state without re-enabling GL access.

For the RinOS Ladybird WebGL 1 embedding, this native-to-RinGL condition is now
also a browser transition: the command-current, presentation, and
`isContextLost()` paths latch the WebGL context-lost flag before dispatching one
cancelable canvas `webglcontextlost` event. A handler can therefore re-enter
only against the already-lost context. Surface recreation, restoration, and
`webglcontextrestored` are still absent, so this is a loss-notification
contract, not a restoration claim.

## Expected ownership

```text
RinOS / browser / native embedding runtime
  owns RinGPU session/device selection
  owns display/window/drawing-surface integration
  maps the small adapter onto public RinGPU APIs
              |
              v
        RinGL context
  owns GL state and GL object namespaces
  derives RinGPU objects and commands
              |
              v
           RinGPU
```

A context may be created without a RinGPU binding for validation-only unit tests. Operations that require actual GPU storage report a GL-visible error instead of silently falling back to a software renderer.

## ABI policy

Public structures carry `struct_size` and `api_version`. RinGL v1 accepts the required v1 prefix and zero-initializes newer appended operation-table fields when an older compatible table is supplied. Reserved fields must remain zero. Operation tables are copied into the context so their storage does not need to outlive context creation. New compatible fields should be appended. Incompatible semantic changes require a new API version or a new versioned structure.
