# Tasks

## 1. Correct end-feed accounting

- [x] 1.1 Reconcile the stopped implementation with the narrowed plan: retain only the shared P710BT driver correction that declares/transmits N minus 28 rows, verifies the replaced 14 rows per end are blank, consumes all logical rows, and reuses existing failure/finishing state. Remove the superseded model geometry/minimum-limit, metadata/bridge, PNG-compaction/scaling, and certification changes introduced by this change; preserve separately authorized model aliases. Extend existing raster captures for 36 and 50 mm with feed-plus-raster length and internal-marker assertions, nonblank-end failure, and truncation inside blank trailing rows; retain existing PT-2730 byte expectations. Add a brief README explanation of feed accounting, end-row rejection, and software-only verification. Verify these cases through the existing driver/raster tests.
- [x] 1.2 Verify the existing PNG route with a 36 mm repository fixture using explicit and image-derived length: capture 255 logical rows as 227 transmitted rows plus 28 feed dots, preserving marker position and scale. Adjust only affected existing P710BT fixtures/capture expectations for blank ends and reduced transmission, retaining their assertions and PT-2730 expectations; verify the existing PNG suite without adding a renderer or test harness.

## 2. Verify integration

- [x] 2.1 Run the configured core checks, the full adapter suite against the existing pinned container dependencies, and `openspec validate --all --strict --no-interactive`; reuse build caches and report actual results without a separate committed validation record. Perform the required cut pass against this scope. No physical or live-template validation, new CUPS harness work, or certification task is required; leave spec sync and archival to the loop's later archive stage.
