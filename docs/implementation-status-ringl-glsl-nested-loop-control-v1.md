# RinGL nested GLSL loop control v1

The statically unrolled `for` profile accepts terminal control-only conditionals
whose branches contain `break`, `continue`, or further nested control-only
`if` statements. Every conditional branch lowers to forward-only RSH1 jumps.
An `else` arm follows the same one-control-statement rule, nested `if` without
`else` may fall through to the containing control branch, and lookahead is
bounded to 16 nested conditionals.

This does not add value merging. A conditional break still rejects a loop body
that changes an outer local value. Output assignments or ordinary statements
mixed into the control-only branch, and any loop-control statement followed by
more loop-body statements, remain unsupported and fail shader lowering. No
build or tests were run.
