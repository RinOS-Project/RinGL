# RinGL mixed varying texture coordinates v1

The GLSL parser now forwards the complete coordinate expression from
`texture2D()` and `texture2DProj()` to the generic RSH1 lowerer. The lowerer
checks the resulting floating vector width and emits arithmetic and sampling
through the ordinary RinGL→RinGPU path. This removes the parser-only restriction
that previously admitted a varying plus at most one coordinate operand.

Strict IR coverage now checks a two-pair `varying vec2` expression containing
both multiplication and addition, confirms the sampled resource, and rejects
wrong-width `vec3` and integer `ivec2` coordinates without publishing RSH1.
The RinGL→RinGPU bridge draws the expression and reads the blue texel selected
by the combined coordinate. The focused RinGL IR target and bridge executable
build, but tests were not executed, so runtime behavior remains unverified.

Broader GLSL ES varying linkage and expression semantics remain open.
