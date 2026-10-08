# RinGL mixed varying texture coordinates v1

The GLSL parser now forwards the complete coordinate expression from
`texture2D()` and `texture2DProj()` to the generic RSH1 lowerer. The lowerer
checks the resulting floating vector width and emits arithmetic and sampling
through the ordinary RinGL→RinGPU path. This removes the parser-only restriction
that previously admitted a varying plus at most one coordinate operand.

The `RinGL` CMake library target builds with this parser change. Tests were not
run, so this is not complete evidence for the compatibility slice. The RinGL
TODO remains open for parser and IR regression coverage, an actual bridge
draw/readback using a multi-operand mixed-varying coordinate, and
wrong-width/non-floating rejection coverage. Broader GLSL ES varying linkage
and expression semantics also remain open.
