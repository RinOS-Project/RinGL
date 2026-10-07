# Framebuffer depth/stencil bit queries

Date: 2026-10-07

## Implemented

`ringl_get_integerv_bounded()` now answers `DEPTH_BITS` and `STENCIL_BITS`
from the currently bound custom framebuffer's logical depth or stencil
attachment metadata. A detached aspect reports zero, and a D24S8 resource
attached through only one logical attachment point reports only that aspect.
Default-framebuffer behavior still uses its configured explicit-aspect
contract, so hidden physical storage does not become visible through a query.
The query writes the caller's value only after attachment metadata succeeds.
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
