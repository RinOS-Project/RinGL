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

`glGetVertexAttribPointerv` remains unavailable because returning a host pointer
would cross the RinGL embedding boundary. No build or tests were run.
