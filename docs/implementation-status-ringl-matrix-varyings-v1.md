# RinGL matrix varyings v1

The generic GLSL parser and RSH1 lowerer now accept `varying mat2`, `mat3`, and
`mat4`. Matrices are flattened in column-major element order into 4, 9, or 16
consecutive scalar perspective slots. The linker compares the matrix dimension
across stages, accounts for every scalar against RinGPU's 28-component
varying limit, and rejects mismatched dimensions before module publication.
The fragment lowerer reconstructs the matrix value from its consecutive scalar
input loads, so existing bounded matrix-vector operations can consume it.
Matrix values cannot use vector swizzles.

The RinGL CMake `RinGL` library target builds after this change. No tests were
run. Parser/IR checks and a real bridge draw/readback for all supported matrix
dimensions, dimension mismatch, and varying-budget overflow remain open in
[`TODO.md`](../TODO.md). This does not add varying arrays or general matrix
arithmetic.
