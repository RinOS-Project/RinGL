# RinGL 8-bit stencil wrap status

The RinGL pipeline translator maps `RINGL_INCR_WRAP` and `RINGL_DECR_WRAP` to
the corresponding RinGPU stencil operations. The RinGPU software backend
applies those operations to the represented 8-bit stencil value, so increment
wraps `255` to `0` and decrement wraps `0` to `255`.

`tests/rin_webgl_ringl_bridge_test.c` now contains an end-to-end surface case
that clears the native S8 plane to 255, draws with `INCR_WRAP`, checks the
resulting zero value and red color, then gates a second draw with `EQUAL 0`,
uses `DECR_WRAP`, and checks the resulting 255 value and green color. This
checks both stencil mutation and the color effect of the stencil comparison
through RinGL, RinGPU, and the Aquamarine software surface.

The bridge test executable was compiled from the root test manifest while
adding this case. The executable was not run in this work session. This covers
the bounded S8 wrap behavior only; other unfinished GLES/WebGL stencil
attachment semantics remain open under the parent TODO.
