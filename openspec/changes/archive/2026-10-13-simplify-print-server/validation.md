# Validation summary

The implementation was merged as commit 0603578 after all 42 tasks were completed,
specifications synced, and simplify-print-server archived on 2026-10-13.

## Software

- The full runtime build passed all 11 CTest suites in 120.50 s, including core,
  adapter, dependency-boundary, USB, PNG, and command-capture checks.
- Runtime IPP/socket smoke passed all four finishing presets, synchronized
  defaults, explicit geometry, image copies, and malformed-input rejection.
- A conventional driverless CUPS queue passed PNG/JPEG, multi-label PDFs and PDF
  copies, offline queuing/resumption, registered finishings, collection
  submission, and existing Web-default preservation.
- Review regressions verified named-only media length and raster contradictions,
  active-job status polling, bounded custom-width rounding, and rejection of
  oversized custom designs without replacing explicit margins or resizing.
- Strict OpenSpec validation passed. The
  [PR checks](https://github.com/pfa230/ptouch-print-server/actions/runs/38357815011)
  and [main build/publication](https://github.com/pfa230/ptouch-print-server/actions/runs/38358284589)
  also passed.

## Physical PT-2730 acceptance

- The eight-label direct-protocol gate on 24 mm tape passed chain:2→each:2 and
  none:2→end:2, including release of held ends without discarded inter-batch leaders.
- The separately authorized 21-label integrated session on 12 mm tape passed
  chain:3→chain:2 across idle/offline→each:1, none:2→none:2→end:1, each PNG:3,
  each unknown-count raster:3, end PNG:3, and one recovery label.
- The operator confirmed separation, joined strips, final cuts, complete rectangle
  borders, the longer middle raster label, orientation, and no ghost output.
- Wrong tape emitted no raster. Truncated input withheld normal finishing and
  blocked subsequent output until printer power-cycle followed by server restart;
  recovery printed only the explicitly resubmitted label.
- Physical coverage is 12 mm integrated and 24 mm direct protocol. 6/9/18 mm retain
  software-only coverage.

## Deployment

The published 0603578 image was deployed to vault's ptouch service on 2026-10-13.
Live IPP capability/readiness and empty-queue checks passed; the public route
returned HTTP 200. USB/network settings were preserved and the previous image
retained for rollback. Deployment verification printed no labels.
