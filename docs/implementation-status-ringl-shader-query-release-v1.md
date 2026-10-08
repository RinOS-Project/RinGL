# RinGL shader query and compiler release profile v1

The bounded program/shader parameter queries expose the complete GLES 2.0
integer pname sets through failure-atomic one-value spans. Delete-status is
reported for objects retained while delete-pending, and object lifetimes remain
owned by the existing attachment/reference rules.

The source compiler lowers synchronously and retains no process-global compiler
cache or allocation. `ringl_release_shader_compiler()` is therefore an
intentional no-op with the same observable state as releasing all compiler-only
resources. Shader objects, their source, and compiled modules remain governed by
their separate object lifetime calls. `glShaderBinary` remains unavailable
because RinGL has no admitted binary format. No build or tests were run.
