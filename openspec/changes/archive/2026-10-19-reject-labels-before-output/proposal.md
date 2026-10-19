# Proposal

## Why

A normal label validation error currently disables subsequent printing until recovery: the reported 18 mm Gridfinity PNG failed at row 11 after page setup had already latched uncertain output. The tape check also has a reproduced weakness: an already queued 18 mm status reply can satisfy a new request whose actual reply reports 12 mm.

## What Changes

- Validate each complete P710BT rendered page before sending that page's setup or raster, so end-margin ink and incomplete first pages fail without creating printer uncertainty. Use one bounded packed page in the existing driver callbacks rather than duplicating PAPPL's image placement.
- Preserve a preceding healthy held run when the first page of a following job is rejected; allow a subsequent valid job to print without a reset or restart. Genuine uncertainty from output already transferred retains the existing recovery contract.
- Discard queued status input before requesting live tape information, within the existing bounded status exchange, and extend the existing wrong-tape regression to a queued 18 mm reply followed by a fresh 12 mm reply.
- Include requested and reported widths in the existing job log at the tape decision, and document the difference between validation rejection and interrupted output.

The stale-reply weakness is reproduced in a hardware-free probe, not established as the cause of the October 19 incident: the original raw reply was not logged and the printer was unreachable during the direct status query. This proposal does not change the labeler template, silently move or crop ink, add a reset operation, or automatically clear genuinely ambiguous output.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `label-rendering`: Require complete P710BT page validation before its setup or raster output, including both replaced end strips; incomplete or rejected first pages no longer create output uncertainty.
- `server-architecture`: Permit one bounded P710BT packed page in the existing deferred lifecycle, replacing the prohibition on any extra raster buffer.

The tape-reader repair implements the existing `printer-device` live-media confirmation and `label-rendering` wrong-tape requirements; it does not need another copy of those requirements.

## Impact

Primary changes are in `src/driver.c` and the existing `tests/test_driver.c` fixtures; PNG, JPEG, PWG raster and URF continue using their existing renderers and the common P710BT callbacks. PT-2730 keeps its current streaming path and print bytes. README printing/recovery notes need a short update. No dependency, public IPP attribute, finishing policy, deployment setting, live service change, or labeler edit is required.
