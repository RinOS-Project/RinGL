# RinGL vertex attribute query profile v1

The bounded `glGetVertexAttribfv`/`glGetVertexAttribiv` equivalents expose the
represented array descriptor fields, divisor, and four-component current value
through complete caller spans. Integer current-value queries round Float32
components to the nearest signed integer and reject NaN, infinity, or
out-of-range values before changing caller storage. Descriptor and float-value
queries remain failure-atomic. Buffer names and divisors larger than `INT32_MAX`
cannot be represented by the integer query ABI, so `glGetVertexAttribiv`
rejects those pnames without publishing a truncated value. The Float32 query
converts those unsigned descriptor values directly, without an intermediate
signed narrowing.

The bounded `ringl_get_vertex_attrib_pointer_offset_bounded()` query now returns
the captured VBO byte offset through one caller-owned `uint64_t` slot. It
validates the complete slot and attribute index before publishing, and never
casts the offset to or returns a host pointer. The GLES-shaped
`glGetVertexAttribPointerv` entry point remains unavailable because raw pointers
are not part of the RinGL embedding ABI. No build or tests were run.
