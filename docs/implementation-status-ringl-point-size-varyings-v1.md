# RinGL generic point-size and varying interface v1

RinGL's generic GLSL lowerer now combines a programmable vertex
`gl_PointSize` with matching `varying float`, `vec2`, `vec3`, and `vec4`
interfaces. The native graphics ABI reserves RSH1 vertex output 4 for point
size. Without a point-size output, RinGL can use outputs 4 through 31 for 28
scalar perspective varyings. With point size, varying outputs move to 5 through
31, leaving 27 scalar varyings.

The lowerer marks point-size stores while parsing and finalizes the output
layout after the full shader has been read. This makes the mapping independent
of whether `gl_PointSize` appears before or after varying assignments and also
covers stores produced by bounded statically expanded control flow. Varying
stores are shifted as a group; point-size stores become output 4. A shifted
store beyond the 32-slot RSH1 interface is rejected before module publication.
The existing pipeline cache recognizes the extra vertex output and shifts its
native varying descriptors to match.

The broader generic varying TODO remains open for end-to-end coverage of
arbitrary mixed expressions, varying-driven texture combinations, and wider
linkage semantics. No tests or builds were run for this change; validation was
limited to source review and `git diff --check`.
