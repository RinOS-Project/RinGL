# RinGL conditional loop control v1

The bounded GLSL ES lowerer now accepts a terminal `if` in a statically
unrolled `for` body when its branch contains one `break` or `continue`. An
optional `else` may contain one loop-control statement. The predicate branch
and loop-control edges lower to forward-only RSH1 jumps.

Conditional `break` requires that the loop body leave all outer local values
unchanged. Without a loop-exit value merge, publishing a different value for
the break path would be incorrect. Conditional `continue` may preserve body
writes because both paths reach the same statically expanded next-iteration
state. Loop control followed by additional body statements and mixed
control/output branch bodies remain rejected.

The broader `break`/`continue` item remains incomplete: nested conditional
control shapes and loop-exit value merges are not implemented. No build or
tests were run for this change.
