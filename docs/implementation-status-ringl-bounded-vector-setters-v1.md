# RinGL bounded vector setters v1

RinGL exposes caller-span alternatives for GLES vector setters without adding
unbounded host-pointer entry points. The vertex-attribute forms accept one to
four values, validate the index and complete required span before changing
context state, and use the GLES defaults for omitted components: zero for Y/Z
and one for W.

The texture parameter integer form consumes one value for the GLES sampler
enum pnames and preserves the scalar integer anisotropy behavior. The float
form consumes one value for the core pnames as exact Float32 enum values or for
the existing, extension-gated anisotropy pname. These wrappers dispatch to the
scalar state paths, so validation and GL error reporting remain shared. The
integer return reports span/context admission; target, pname, and value errors
remain in the GL error queue.

The GLES API inventory classifies the represented scalar and bounded-vector
texture parameter entry points as complete. This is a caller-span API profile;
it does not export an unbounded host-pointer overload. No build or tests were
run for this change.
