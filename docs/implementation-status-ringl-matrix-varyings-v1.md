# RinGL matrix varyings v1

The generic GLSL parser and RSH1 lowerer now accept `varying mat2`, `mat3`, and
`mat4`. Matrices are flattened in column-major element order into 4, 9, or 16
consecutive scalar perspective slots. The linker compares the matrix dimension
across stages, accounts for every scalar against RinGPU's 28-component
varying limit, and rejects mismatched dimensions before module publication.
The fragment lowerer reconstructs the matrix value from its consecutive scalar
input loads, so existing bounded matrix-vector operations can consume it.
Matrix values cannot use vector swizzles.

Parser/IR modules now pin the 4/9/16 scalar stage interfaces for all matrix
dimensions and execute the corresponding fragment matrix/vector operation.
The bridge reads matrix-varying results for `mat2`, `mat3`, and `mat4` from
real RinGPU draws. Program-link regressions accept the exact 28-component
`mat4 + mat3 + vec3` interface, reject 29 components, and reject cross-stage
`mat2`/`mat3` mismatch. The focused RinGL IR and program targets and the
RinOS-side bridge test executable build; none of these tests were executed, so
runtime behavior remains unverified.

This does not add varying arrays or general matrix arithmetic.
