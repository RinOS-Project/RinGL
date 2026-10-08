# Aquamarine and RinGL software backend evaluation

The existing `public-base/libs/aquamarine` library provides surface-based
2D/3D drawing operations. Its public API does not implement RinGPU's explicit
resource, command-list, shader-module, queue, and synchronization contract.
RinGL lowers GLSL ES into RinShader IR and sends resource/state changes through
RinGPU, so drawing directly through Aquamarine would create a second execution
path that bypasses those contracts.

Turning the existing library into a useful RinGL backend would therefore mean
implementing a complete RinGPU backend over it, not linking its current draw
helpers into the GL frontend. The opt-in RinGL surface currently uses the
generic RinGPU software backend and keeps its public draw path on the same
RinGPU ABI as other backends. The CMake and Meson RinGL core manifests do not
link or include the Aquamarine library.

Aquamarine Shader Language and GLSL ES remain separate frontends that lower to
RinShader IR. The decision is to keep `public-base/libs/aquamarine` as a
separate drawing library unless a complete RinGPU backend over it becomes a
concrete product need. If such a backend is implemented later, shared RinGL
validation/state coverage must keep it aligned with hardware backends.

This is an architecture/source-manifest evaluation; no code or tests were
changed for the evaluation.
