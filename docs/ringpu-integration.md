# RinGPU integration boundary

RinGL is a user-space OpenGL/OpenGL ES compatibility layer. It does not discover a global GPU device by reaching into RinGPU internals and it does not expose RinGPU implementation objects as GL objects.

The embedding runtime creates or selects the RinGPU execution environment, then supplies RinGL with a versioned `RinGLRinGpuBindingV1` when a context is created. The binding contains an embedding-owned opaque session, a small operation table, the selected graphics queue handle, and the queue capability snapshot.

The context copies both the fixed-width binding and the v1 operation table, so the caller's descriptors may be temporary. Ownership of the opaque session remains with the embedding runtime.

## Host operation table

Some GL operations require host integration that is intentionally outside the portable GL state core. The first v1 callbacks are:

- `create_buffer(session, size, out)` — create a RinGPU buffer suitable for the initial GLES buffer roles;
- `upload_buffer(session, buffer, offset, data, size)` — upload bounded host data to that buffer;
- `destroy_object(session, object)` — release the underlying RinGPU handle.

The RinOS adapter is expected to implement upload with the appropriate RinGPU staging/copy path. RinGL does not require a CPU pointer to VRAM and does not place vendor or kernel allocation objects in the GL object model.

`ringl_buffer_data()` uses this table transactionally. It creates and optionally uploads replacement storage first; only after both operations succeed does it destroy the previous backing buffer and publish the new storage in GL state. A failed replacement therefore leaves the previous GL buffer storage intact.

## Expected ownership

```text
RinOS / browser / native embedding runtime
  owns RinGPU session/device selection
  owns display/window/drawing-surface integration
  implements small RinGPU host-operation adapters
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

The command-list/render-pass translation path will live under `src/translate/` and will use the selected graphics queue from the binding. The host operation table is not intended to replace RinGPU command encoding; it only covers embedding work such as staging uploads that the portable GL state layer cannot perform by itself.

## ABI policy

Public structures carry `struct_size` and `api_version`. RinGL v1 accepts structures at least as large as the v1 definition and requires reserved fields to be zero. Operation tables are copied into the context so their storage does not need to outlive context creation. New compatible fields should be appended. Incompatible semantic changes require a new API version or a new versioned structure.
