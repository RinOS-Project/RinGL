# RinGL uniform-led local texture coordinates v1

The generic bounded texture lowerer accepts a `uniform vec2` as the left
operand of a local coordinate initializer when the right operand is the
current varying or preceding local coordinate. The accepted operators are
`+`, `-`, `*`, and `/`; a full-width read-only `xy`/`yx`, `rg`/`gr`, or
`st`/`ts` selector may permute the uniform components. RSH1 retains the source
operand order for subtraction and division through its reverse-operation
forms. The initializer remains in the same bounded local-coordinate chain and
does not add general vector-expression support or a host-side texture path.

`tests/rin_webgl_ringl_bridge_test.c` now registers a focused product
regression through the browser-facing RinGL bridge, RinGPU, and the private
Aquamarine surface adapter. It reads back distinct texels for uniform-led
local `+`, `-`, `*`, and `/` with an `offset.yx` permutation, then supplies a
runtime zero divisor and checks both `INVALID_OPERATION` and byte-for-byte
target preservation. The regression was not executed in this work session;
its runtime result remains unverified. This bounded item does not complete the
broader GLSL texture or varying work.
