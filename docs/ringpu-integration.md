# RinGPU integration boundary

RinGL is a user-space OpenGL/OpenGL ES compatibility layer. It does not discover a global GPU device by reaching into RinGPU internals and it does not expose RinGPU implementation objects as GL objects.

The embedding runtime creates or selects the RinGPU execution environment, then supplies RinGL with a versioned `RinGLRinGpuBindingV1` when a context is created. The binding contains an embedding-owned opaque session, a small operation table, the selected graphics queue handle, and the queue capability snapshot.

The context copies both the fixed-width binding and the v1 operation table, so the caller's descriptors may be temporary. Ownership of the opaque session remains with the embedding runtime.

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

## Host operation table

The v1 callbacks are:

- `create_buffer(session, size, out)` — create a RinGPU buffer suitable for the initial GLES buffer roles and visible uploads;
- `upload_buffer(session, buffer, offset, data, size)` — call the public RinGPU visible-upload path;
- `destroy_object(session, object)` — release the underlying RinGPU handle;
- `create_shader_module(session, rsh1, size, out)` — map validated RinGL-generated RSH1 onto `ringpu_create_shader_module()`.

The shader callback is optional for validation-only contexts. Calls that require an actual RinGPU module fail rather than silently accepting an unvalidated backend object.

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

The command-list/render-pass translation path will live under `src/translate/` and will use the selected graphics queue from the binding. The host operation table is not intended to replace RinGPU command encoding; it keeps only the embedding-owned session representation outside the portable GL core.

## ABI policy

Public structures carry `struct_size` and `api_version`. RinGL v1 accepts structures at least as large as the v1 definition and requires reserved fields to be zero. Operation tables are copied into the context so their storage does not need to outlive context creation. New compatible fields should be appended. Incompatible semantic changes require a new API version or a new versioned structure.
