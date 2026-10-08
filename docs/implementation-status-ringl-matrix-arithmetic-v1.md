# RinGL bounded square-matrix arithmetic v1

The generic GLSL RSH1 lowerer now expands bounded square `mat2`, `mat3`, and
`mat4` arithmetic into scalar operations. It handles same-dimension addition
and subtraction, matrix products, matrix/vector products in both orders,
matrix/scalar multiplication in both orders, matrix/scalar division, and
unary negation.
The generic route can use these operations with initialized locals, linked
matrix uniforms, and matrix varyings. Matrix values retain column-major
component order. The existing RSH1 instruction and register ceilings still
reject expressions that cannot fit before module publication.

Rectangular matrices, cross-dimension matrix operations, scalar divided by a
matrix, matrix/vector operations in the specialized transformed-texture
profile, and dynamic indexing remain unsupported.

The RinGL CMake `RinGL` library target builds after this change. No tests were
added or run. Parser/IR and real RinGL-to-RinGPU numeric readback coverage,
including all matrix dimensions and budget rejection, remains open in
[`TODO.md`](../TODO.md); this document does not claim verified numerical
execution.
