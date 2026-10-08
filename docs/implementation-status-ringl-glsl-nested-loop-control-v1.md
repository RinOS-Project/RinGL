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
constructs remain rejected. This is still an incomplete language profile: the
change has not been built or tested, and its branch, output, and loop-exit
behavior must be verified before the parent control-flow TODO can close.
