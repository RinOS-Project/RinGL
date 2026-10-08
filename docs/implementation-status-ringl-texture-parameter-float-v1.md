# RinGL GLES texture parameter float profile v1

The bounded GLES texture parameter profile accepts the four core sampler
pnames through both scalar and one-value caller-span setter forms. Float setters
accept only exact Float32 representations of legal integer enum values, then
reuse the integer setter's validation and sampler update path. Unknown pnames and
illegal values continue through the RinGL error queue without unsafe float to
integer conversion.

Integer and Float32 queries cover the same four core sampler pnames. Core enum
values convert exactly to Float32, and the extension-gated anisotropy query
retains its fractional precision. Integer anisotropy queries round to the
nearest integer. The anisotropy setter remains finite, extension-gated, and
upper-bounded.

This closes the represented GLES 2.0 texture parameter profile. The broader
RinGL GLSL ES, stencil, and remaining GLES compatibility work stays open. No
build or tests were run.
