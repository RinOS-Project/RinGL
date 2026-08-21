# RinGPU integration boundary

RinGL is a user-space OpenGL/OpenGL ES compatibility layer. It does not discover a global GPU device by reaching into RinGPU internals and it does not expose RinGPU implementation objects as GL objects.

The embedding runtime creates or selects the RinGPU execution environment, then supplies RinGL with a versioned `RinGLRinGpuBindingV1` when a context is created. The binding contains an embedding-owned opaque session, the selected graphics queue handle, and the queue capability snapshot.

This mirrors RinGPU's explicit-handle model while keeping OS transport details outside the portable GL state core. The context copies the fixed-width binding so the caller's descriptor may be temporary; ownership of the opaque session remains with the embedding runtime.

At the current bootstrap stage the context stores but does not execute through this binding. The `src/backend/` and `src/translate/` layers will consume it as command encoding is implemented.

## Expected ownership

```text
RinOS / browser / native embedding runtime
  owns RinGPU session/device selection
  owns display/window/drawing-surface integration
              |
              v
        RinGL context
  owns GL state and GL object namespaces
  derives RinGPU objects and commands
              |
              v
           RinGPU
```

A context may currently be created without a RinGPU binding for validation-only unit tests. Rendering entry points must not silently fall back to such a context once command translation is introduced; they should report an appropriate GL-visible failure/context-loss condition.

## ABI policy

Public structures carry `struct_size` and `api_version`. RinGL v1 accepts structures at least as large as the v1 definition and requires reserved fields to be zero. New compatible fields should be appended. Incompatible semantic changes require a new API version or a new versioned structure.
