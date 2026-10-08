# RinGL bounded vector setters v1

RinGL exposes caller-span alternatives for GLES vector setters without adding
unbounded host-pointer entry points. The vertex-attribute forms accept one to
four values, validate the index and complete required span before changing
context state, and use the GLES defaults for omitted components: zero for Y/Z
and one for W.

The texture parameter integer form consumes one value for the represented
sampler enum pnames and preserves the scalar integer anisotropy behavior. The
float form consumes one value for the existing, extension-gated anisotropy
pname. These wrappers dispatch to the existing scalar state paths, so
validation and GL error reporting remain shared. The integer return reports
span/context admission; target, pname, and value errors remain in the GL error
queue.

The GLES API inventory remains partial because the raw pointer overloads are
not exported and float-vector enum conversions for core sampler pnames are not
implemented. No build or tests were run for this change.
