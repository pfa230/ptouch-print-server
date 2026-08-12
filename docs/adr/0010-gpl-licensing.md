# 10. GPL-3.0-only, because the protocol core is derived work

**Status:** Accepted

## Context

The project reuses the Brother P-touch raster protocol and device/tape tables from
hannesweisbach/ptouch-print, and the cutter planning logic from its macOS fork. Both are GPL-3.0.

## Decision

License the project **GPL-3.0-only**, with attribution to upstream in `LICENSE`, `README.md` and
`THIRD_PARTY_NOTICES.md`.

## Consequences

- A permissive licence (MIT/Apache) is not available: GPL is copyleft, and roughly 18% of the C code
  (`protocol.*`, `tables.*`, plus the cutter) is derived from GPL-3.0 sources. The volume is small but
  it is the irreplaceable part - device IDs, per-model flags, tape geometry and the raster command
  bytes.
- Relicensing would require a clean-room re-derivation of exactly that core, which is not worth it.
- PAPPL (Apache-2.0) and libusb (LGPL-2.1) are compatible and impose no additional constraint.
