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

The callback boundary remains useful because RinGL is a standalone repository and should not hard-wire an OS-Core internal session type into its portable context ABI.

## OS-Core adapter

OS-Core now contains `src/webengine/rin_ringl_ringpu_adapter.{h,c}`. The adapter owns no RinGPU objects itself. It borrows a `RinGpuCore*` and maps the RinGL v1 operation table directly onto the public RinGPU API.

The first-slice mappings are:

- buffer creation/upload/destruction -> `ringpu_create_buffer()`, `ringpu_upload_buffer()`, `ringpu_destroy()`;
- shader modules -> `ringpu_create_shader_module()`;
- vertex graphics pipelines -> `ringpu_create_graphics_pipeline_vertex()`;
- command lists -> `ringpu_create_command_list()` and `ringpu_command_list_reset()`;
- image transitions -> `ringpu_command_transition_image()`;
- render passes -> `ringpu_command_begin_render_pass()` / `ringpu_command_end_render_pass()`;
- vertex drawing -> `ringpu_command_draw_vertices()`;
- presentation -> `ringpu_command_present()`;
- submission -> `ringpu_command_list_close()` and `ringpu_queue_submit()`.

The WebEngine CMake integration is opt-in through `RIN_LADYBIRD_ENABLE_RINGL`.
Its default `RIN_RINGL_SOURCE_ROOT` is the checked-out `libs/RinGL` tree and
can be overridden only for an explicitly selected external checkout.

OS-Core also contains `rin_webgl_ringl_bridge.{h,c}`. The bridge borrows the existing `RinWebGLRingPUSurfaceContext`, obtains its RinGPU core/graphics queue/color image through a private native view, creates a RinGL context using the adapter, and binds that image as RinGL's default framebuffer. The surface remains the owner of the RinGPU core, queue, image, and caller-provided pixel backing store.

During the initial bridge lifetime RinGL is the exclusive command producer for the borrowed surface. The legacy `rin_webgl_ringpu_surface_clear()` / `present()` helpers must not be interleaved with RinGL commands until shared image-state synchronization is generalized.

The surface color image declares `COPY_SOURCE` and `CPU_READABLE` as well as
its existing `COPY_DESTINATION`, `COLOR_TARGET`, and `PRESENT` uses. Its
backend supplies bounded readback, so RinGL's complete RGBA color-target
`RGBA/UNSIGNED_BYTE` `readPixels` path can transition to `COPY_SOURCE`, wait,
read back, and swizzle default-framebuffer BGRA storage to RGBA. The same fenced
snapshot powers `copyTexSubImage2D` and `copyTexImage2D` from either the default
color buffer or a complete RGBA8 texture/renderbuffer FBO into level-zero RGBA
texture storage; FBO completeness and source/destination rectangles are checked
before temporary allocation, and the destination shadow is changed only after
readback succeeds. `copyTexImage2D` retains a prior texture definition/image
until the replacement snapshot completes. The focused OS-Core
`rin_webgl_ringl_bridge_test` covers clear, readback, and present through this
borrowed surface. Depth/stencil, multisample, and other FBO copy semantics remain
outside this slice.

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
topology, vertex stride, and resolved vertex attributes. Active program
attributes are selected through their linked generic GL locations, then emitted
as dense scalar RSH1/RinGPU inputs. This means `bindAttribLocation` affects the
actual vertex fetch source without exposing sparse GL indices to RinGPU. A
bounded cache reuses identical pipelines and evicts old entries in FIFO order.

The adapter's `create_graphics_pipeline` callback maps directly to `ringpu_create_graphics_pipeline_vertex()`. The RinOS adapter fills a `RinGpuGraphicsPipelineVertexDescV1`, converts each `RinGLRinGpuVertexAttributeV1` to `RinGpuVertexAttributeV1`, and forwards the linked RinGPU shader-module handles unchanged.

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

The selected `RinWebGLRingPUSurfaceContext` backend executes generic
`RIN_GPU_BACKEND_COMMAND_DRAW_VERTICES` into the caller-owned BGRA surface;
the OS-Core surface test covers that native command path. RinGL's current
vector position and initial texture/varying profiles lower through the same
public RinGPU adapter. The browser-facing GLES/WebGL object and command bridge,
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

This is a bounded native-to-RinGL propagation contract. The Ladybird bridge
does not yet translate it into browser `webglcontextlost`/restoration events or
recreate an embedding surface, so it is not a browser-facing WebGL context-loss
implementation claim.

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
