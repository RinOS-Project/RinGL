# RinGL uniform-led local texture coordinates v1

The generic bounded texture lowerer accepts a `uniform vec2` as the left
operand of a local coordinate initializer when the right operand is the
current varying or preceding local coordinate. The accepted operators are
`+`, `-`, `*`, and `/`; a full-width read-only `xy`/`yx`, `rg`/`gr`, or
`st`/`ts` selector may permute the uniform components. RSH1 retains the source
operand order for subtraction and division through its reverse-operation
forms. The initializer remains in the same bounded local-coordinate chain and
does not add general vector-expression support or a host-side texture path.

The existing `ringl-shader_ir-test` and `ringl-textured_draw-test` targets
compile against the change. They were not executed in this work session, and
there is no focused readback regression yet for the four uniform-led local
operators, swizzle permutation, or zero-divisor target preservation. The
runtime regression remains open in `TODO.md`; this status does not complete
the broader GLSL texture or varying items.
