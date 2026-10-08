# RinGL raster-state readback status

The RinGL draw path copies aliased `line_width` and polygon-offset factor and
units into the RinGPU raster descriptor. The generic software backend rasterizes
line coverage at the requested width and applies filled-triangle polygon offset
before depth comparison and writing.

`tests/rin_webgl_ringl_bridge_test.c` now contains a 5×5 RinGL→RinGPU→Aquamarine
regression. It distinguishes width 1 from width 3 by checking each output row,
then draws the same triangle against depth 0.5 with positive and negative
polygon units under `LESS`: positive offset preserves the green color and depth,
while negative offset writes red and a smaller depth value.

The bridge test executable was compiled from the root test manifest with these
cases. The executable was not run in this work session. Broader GLES conformance
and hardware/software parity remain open.
