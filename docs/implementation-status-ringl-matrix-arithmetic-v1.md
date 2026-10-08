# RinGL bounded square-matrix arithmetic v1

The generic GLSL RSH1 lowerer expands bounded square `mat2`, `mat3`, and `mat4`
arithmetic into scalar operations. It handles same-dimension addition and
subtraction, matrix products, matrix/vector products in both orders,
matrix/scalar multiplication in both orders, matrix/scalar division, and unary
negation. The generic route can use these operations with initialized locals,
linked matrix uniforms, and matrix varyings. Matrix values retain
column-major component order. Finite constant matrix products are folded in
binary32 operation order so uniform `mat4` products can fit the RSH1 budget;
products that cannot be folded retain the ordinary runtime instruction and
register limits.

Parser/IR regression coverage now includes the matrix operators in all three
dimensions and distinct instruction-limit and register-limit rejection cases.
The RinGL-to-RinGPU bridge coverage reads back mat2 and mat3 arithmetic with
both vector/matrix multiplication orders, plus a column-major uniform mat4
product. The focused `ringl-shader_ir-test` target builds. Tests were not run,
so these cases are present as source coverage but their execution is
unverified.

Rectangular matrices, cross-dimension matrix operations, scalar divided by a
matrix, matrix/vector operations in the specialized transformed-texture
profile, and dynamic indexing remain unsupported and are rejected before
module publication.
