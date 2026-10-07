# Framebuffer component/depth/stencil bit queries

Date: 2026-10-07

## Implemented

`ringl_get_integerv_bounded()` now derives all six component-bit queries from
the currently bound framebuffer. For custom FBOs, `RED_BITS`, `GREEN_BITS`,
`BLUE_BITS`, and `ALPHA_BITS` use `COLOR_ATTACHMENT0`; depth and stencil use
their logical attachment metadata. A missing attachment reports zero, and a
D24S8 resource attached through only one logical attachment point reports only
that aspect. Default-framebuffer behavior retains the configured native color
format and explicit depth/stencil aspect contract, so hidden physical storage
does not become visible through a query. The query writes caller output only
after attachment metadata succeeds. This matches OpenGL ES 2.0 §4.4.6, which
defines these pixel-depth values from the currently bound framebuffer
([specification](https://registry.khronos.org/OpenGL/specs/es/2.0/es_full_spec_2.0.pdf)).
The RinGL README and GLES 2 API status now describe the default and custom
framebuffer behavior separately.

## Verification

No tests or build were run. The implementation path was reviewed against the
existing bounded framebuffer attachment query, which already resolves object
format and logical aspect metadata and reports zero for unattached slots.

## Remaining scope

The broader GLES/WebGL stencil attachment semantics TODO remains open. This
change covers only the global depth/stencil bit-count queries; it does not
claim complete GLES/WebGL conformance.
