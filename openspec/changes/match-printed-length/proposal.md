# Proposal

## Why

The user reports that a 36 mm `brother_12mm_item` label prints about 40 mm long on the PT-P710BT. The current driver transmits the whole rendered page and separately commands 14-dot end feed, consistent with roughly 3.95 mm being counted twice.

## What Changes

- Count the existing P710BT end feed inside the requested page: declare and transmit N minus 28 rows for an N-row rendered page, replacing only its first and last 14 blank rows.
- Preserve rendered ink size and physical position, explicit length and PNG-derived length, and PT-2730 output.
- **BREAKING for incompatible P710BT pages:** fail visibly when a replaced end row contains ink, using existing output recovery rather than silently cropping it.
- Verify the reported 36 mm case, a 50 mm case, and the existing failure and legacy regressions with software captures. The user validates physical output separately.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `label-rendering`: Define end feed as part of page length and permit equivalent feed to replace verified blank raster end rows.
- `printer-device`: Declare the actual transmitted P710BT row count while preserving its existing protocol and PT-2730 output.

## Impact

The correction belongs in the existing driver callbacks. Reuse current rendering, geometry, protocol builders, and batch recovery; adjust affected existing fixtures/capture expectations and add direct PNG/raster regressions. Add a brief README explanation of the feed accounting and nonblank-end rejection.

The user's KISS rescope removes new media limits, model geometry fields, media metadata/bridge changes, PNG compaction or scaling redesign, dependency work, certification paperwork, and expanded acceptance matrices. No physical acceptance check, live-template inspection, printer access, deployment, or service/configuration change is part of this change. Previously authorized model-alias changes are separate tooling work and remain intact.
