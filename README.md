# RinGL

RinGL is the RinOS OpenGL/OpenGL ES compatibility and translation layer.

Its job is to preserve OpenGL-style state and object semantics at the API boundary, then translate resolved draw, resource, synchronization, and presentation work into the native RinGPU contract used by RinOS.

> [!IMPORTANT]
> RinGL is an early-stage project. The initial implementation target is a bounded OpenGL ES 2.0-style graphics path. It is not currently a conformant OpenGL or OpenGL ES implementation.

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
implemented and covered by strict C11 mock-RinGPU tests. Framebuffer and
renderbuffer objects now have a bounded state-model slice: lifecycle/binding,
level-zero color attachment tracking, RGBA4 renderbuffer storage metadata, and
automatic attachment detachment when an attached texture or renderbuffer is
deleted. They are not yet GPU render targets: framebuffer completeness,
depth/stencil attachments, and custom-FBO rendering remain unfinished.

RinGL is not a GLES conformance claim. Broader shader expressions, device-loss
handling, and browser-facing context-loss policy also remain unfinished. No API
or ABI stability guarantee is made yet.
