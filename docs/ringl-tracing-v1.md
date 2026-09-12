# RinGL bounded trace v1

RinGL exposes an optional, caller-owned `RinGLTraceRuntimeV1` sink for
diagnosing the GL-to-RinGPU boundary. The sink is a fixed 256-event ring and
stores only integers: there are no borrowed pointers, allocations, callbacks,
or unbounded strings in a trace record. Sequence numbers are monotonic, and a
reader can resume from the returned cursor after a short read. When the ring
is full it drops the oldest record and keeps the operation that was actually
attempted visible.

The event classes are:

- backend call summary, with the exact signed RinGPU result;
- shader compile and program link attempts;
- draw attempts;
- texture image and sub-image uploads;
- framebuffer binding/attachment transitions; and
- the sticky WebGL context-loss transition.

Compile/link, draw, upload, and framebuffer records describe attempted public
operations even when RinGL rejects their arguments. A backend call summary
records the backend's exact return value. In particular, trace recording does
not manufacture successful work, replace a rejected call, or change GL error
delivery. A device-lost backend result produces both its call record and the
context-loss record.

An embedding initializes the runtime before passing its pointer in
`RinGLContextDescV1::trace`. The context retains the pointer for the lifetime
of the context; the embedding must keep the runtime storage alive until
destruction. Trace read failures are reported by the read API and never turn
into a synthetic GL success or a synthetic GPU result.

`libs/RinGL/tests/context_test.c` verifies all seven event classes through
real context entry points, the exact context-loss status, resumable reads,
and oldest-event behavior after wraparound.
