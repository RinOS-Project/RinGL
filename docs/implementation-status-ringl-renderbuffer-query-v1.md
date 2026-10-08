# RinGL renderbuffer query profile v1

The bounded `glGetRenderbufferParameteriv` equivalent exposes the full GLES 2.0
query set for RinGL's represented renderbuffer formats: dimensions, internal
format, and actual component sizes. It validates the target, bound object,
pname, and one-element caller span before publication. The supported storage
profile is single-sample; the additional RinGL sample-count pname reports zero.
No build or tests were run.
