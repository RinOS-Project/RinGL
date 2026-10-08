# RinGL nested GLSL loop control v1

The statically unrolled `for` lowerer now routes nested `if` statements through
the ordinary supported statement parser. Branches may combine local and
varying assignments, stage-output writes, nested bounded loops, `break`,
`continue`, and fragment `discard`. The lowerer retains the forward-only RSH1
contract by patching conditional and loop-control edges to later instructions.

Locals and vertex varyings use stable mutable RSH1 registers, so values written
on one branch remain available at the join and after an early loop exit.
Definite-initialization and known-zero facts are intersected across paths that
fall through, continue, or break. A bounded forward CFG pass also checks that
every output slot written anywhere in the module is initialized on each path
that returns; discard paths are terminal and need not publish outputs.

The lowerer checks source following a terminal statement in a scratch copy and
omits those unreachable instructions from emitted RSH1. Unsupported GLSL
constructs remain rejected. The statically bounded loop header also produces
zero iterations when its initial condition is false, consistently in parser
and lowerer.

## Verification (2026-10-09)

Configured the existing CMake build directory with `RINGL_BUILD_TESTS=ON`,
built `ringl-shader_ir-test`, and ran the focused `ringl-shader_ir` CTest:
1/1 passed. The regression covers a two-iteration nested vertex loop with
local/varying updates on `continue` and `break` paths, fragment color
initialization on break/continue paths alongside a terminal discard path,
rejection when a normal loop exit leaves fragment color uninitialized, and
source after `break` (valid writes are omitted; an undeclared name still
rejects). The geometric builtin regression was split into bounded sources so
each remains within RSH1's 96-register limit.

This closes only the supported bounded loop-control item. Other GLSL constructs
and unsupported control-flow combinations remain open in the parent RinGL
compatibility TODO.
