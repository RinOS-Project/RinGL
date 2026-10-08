# RinGL native compute surface resources v1

The private `ringl_aquamarine_surface` API now creates and tracks compute
pipelines and compute bind groups for native callers. The surface permits up
to 32 pipelines and 64 bind groups, requires an adapter/runtime COMPUTE queue
capability, checks that bind groups use a pipeline owned by that surface, and
requires bind groups to be destroyed before their pipeline. Surface teardown
releases bind groups before pipelines. The default generic software runtime
advertises compute; a borrowed runtime without that capability returns
`RINGL_AQUAMARINE_SURFACE_NOT_SUPPORTED`.

The surface still does not expose compute through RinGL draw state or WebGL.
Native callers use the borrowed RinGPU runtime to create compute queues and
command lists, dispatch, synchronize, and manage their caller-owned buffers and
shader modules. Callers must complete their external submissions and reset or
destroy command lists that reference a resource before destroying that
resource or the surface. Shader modules must remain alive through the lifetime
of every pipeline that references them; bound buffers must remain alive
through the lifetime of every bind group that references them.

The RinGL CMake `RinGL` library target builds with these entrypoints. No tests
were added or run, so runtime lifecycle and dispatch integration evidence is
still absent.
