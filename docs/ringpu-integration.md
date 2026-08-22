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

The callback boundary remains useful because RinGL is a standalone repository and should not hard-wire an OS-Core internal session type into its portable context ABI. The RinOS adapter can directly call the public `ringpu_create_buffer()`, `ringpu_upload_buffer()`, and `ringpu_destroy()` functions.

## Shader module validation path

RinGL lowers its current scalar GLSL ES subset to RinShader RSH1 before asking the embedding adapter to create a GPU shader module. The `create_shader_module` adapter should map directly to public `ringpu_create_shader_module()`.

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

`ringl_realize_shader_module()` is transactional. A newly created RinGPU module replaces the previous module only after creation succeeds. Replacing shader source, recompiling, re-lowering, deleting the shader, or destroying the context invalidates and releases stale realized modules.

## Graphics pipeline path

RinGL builds a deterministic first-slice pipeline key from the linked vertex/fragment shader modules, color attachment format, triangle-list topology, vertex stride, and resolved vertex attributes. A bounded cache reuses identical pipelines and evicts old entries in FIFO order.

The adapter's `create_graphics_pipeline` callback maps directly to `ringpu_create_graphics_pipeline_vertex()`. The RinOS adapter fills a `RinGpuGraphicsPipelineVertexDescV1`, converts each `RinGLRinGpuVertexAttributeV1` to `RinGpuVertexAttributeV1`, and forwards the linked RinGPU shader-module handles unchanged.

Before cache eviction during a draw, RinGL resets its reusable command list. This releases references retained by the previous recorded submission before an old pipeline is destroyed.

## Default framebuffer and command path

The embedding runtime owns the presentable image. It binds or replaces that image with `ringl_set_default_framebuffer()`, supplying the RinGPU image handle, format, dimensions, and display id. RinGL treats a newly supplied image as being in `RIN_GPU_IMAGE_STATE_PRESENT` and tracks subsequent first-slice transitions itself.

The initial command path intentionally mirrors public RinGPU operations rather than hiding them behind a GL-shaped backend call:

```text
ringl_clear()
  reset/create command list
  PRESENT -> COLOR_TARGET when required
  begin render pass (CLEAR)
  end render pass
  close + queue submit

ringl_draw_arrays(GL_TRIANGLES)
  reset/create command list
  resolve/validate vertex fetch
  lookup/create graphics pipeline
  PRESENT -> COLOR_TARGET when required
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

The RinOS adapter maps these callbacks to:

- `ringpu_create_command_list()`;
- `ringpu_command_list_reset()`;
- `ringpu_command_transition_image()`;
- `ringpu_command_begin_render_pass()`;
- `ringpu_command_draw_vertices()`;
- `ringpu_command_end_render_pass()`;
- `ringpu_command_present()`;
- `ringpu_command_list_close()`;
- `ringpu_queue_submit()`.

RinGL reuses one graphics command list per context. Each new submission resets it before recording, preserving GL ordering while bounding command-list allocation. The context destroys the command list before cached pipelines and shader modules so RinGPU recorded-command references are released in dependency order.

## Host operation table

The v1 callbacks are append-only. The current groups are:

- buffer storage: `create_buffer`, `upload_buffer`, `destroy_object`;
- shader realization: `create_shader_module`;
- graphics pipeline realization: `create_graphics_pipeline`;
- graphics commands: `create_command_list`, `reset_command_list`, `transition_image`, `begin_render_pass`, `draw_vertices`, `end_render_pass`, `present`, `close_command_list`, and `queue_submit`.

Shader, pipeline, and command callbacks are optional for validation-only contexts. Operations that require a missing capability report a GL-visible failure instead of silently falling back to a software renderer.

`ringl_buffer_data()` uses the buffer callbacks transactionally. It creates and optionally uploads replacement storage first; only after both operations succeed does it destroy the previous backing buffer and publish the new storage in GL state. A failed replacement therefore leaves the previous GL buffer storage intact.

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
