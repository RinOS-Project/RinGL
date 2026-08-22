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
implemented and covered by strict C11 mock-RinGPU tests. Level-zero RGBA8
texture/renderbuffer color attachments are completeness-checked, realized as
RinGPU color targets, used by clear/draw render passes, and read back through
`COPY_SOURCE`. A matching `DEPTH_COMPONENT32F` renderbuffer can now be
attached as `DEPTH_ATTACHMENT`; the pair is rejected on an invalid attachment
or dimension mismatch, and its D32 image is realized lazily as a RinGPU depth
target. The RinOS surface integration test verifies a depth-only clear while
preserving color and depth-tested triangle output from such a custom FBO. The
RinGPU depth pipeline supports every GLES comparison predicate: `NEVER`,
`LESS`, `EQUAL`, `LEQUAL`, `GREATER`, `NOTEQUAL`, `GEQUAL`, and `ALWAYS`.
A trusted embedding can query the default color image's post-submit
state, allowing its caller-owned presentation surface to remain synchronized
across RinGL `present()` and later content updates. When that embedding
supplies a D32 target, RinGL executes default-framebuffer depth clear and all
eight depth-tested draw predicates through a RinGPU depth render pass.

Custom RGBA8 renderbuffer FBOs may additionally attach a matching
`DEPTH24_STENCIL8` renderbuffer through `DEPTH_STENCIL_ATTACHMENT`. RinGL
realizes it as a RinGPU D32S8 target and executes independent front/back
stencil tests, reference/read/write masks, all eight stencil operations, and
stencil clear before the depth test. The common `ringl_stencil_*` calls update
both faces, while the `*_separate` forms set one face or both explicitly. This
is tested through the RinOS RinGL-to-RinGPU surface path, including stencil
rejection, replacement, write masking, depth-fail behavior, reversed winding,
and back-face culling. `ringl_clear(RINGL_STENCIL_BUFFER_BIT)` also forwards
the front stencil write mask through the native render pass, so it preserves
masked-off stencil bits rather than overwriting the complete S8 plane.

This remains a bounded profile. A default caller-owned D32 surface has no
stencil storage unless its embedding explicitly supplies the matching S8
plane; that D32S8 default target executes the same front/back stencil path.
Texture depth/stencil attachments, multisampling, multiple color attachments,
and broad GLES framebuffer semantics are not implemented.

RinGL is not a GLES conformance claim. Broader shader expressions, device-loss
handling, and browser-facing context-loss policy also remain unfinished. No API
or ABI stability guarantee is made yet.
