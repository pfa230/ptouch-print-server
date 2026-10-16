# Tasks

## 1. Verified blank continuation and run state

- [ ] 1.1 Add the minimal PT-P710BT one-white-row continuation helper using existing protocol builders. Verify the captured 57-byte sequence for 12 mm, loaded-width variants, page flag 1, auto-cut/chain on, 14-dot margin, white PackBits data and FF, with no initialize/invalidate/EJECT or user raster. Preserve ordinary protocol captures.
- [ ] 1.2 Add OPEN_BLANK to the existing run enum and treat both healthy open states as continuation. Extend driver tests to prove repeat no-op after release, no initialization on the next real batch, normal FF returning to user-content OPEN, terminal cut returning to CLOSED, cassette invalidation, rejection/cancel preservation, and unchanged ordinary print/finishing/length captures including current main's P710BT correction.

## 2. Direct IPP action and client use

- [ ] 2.1 Register and handle Ptouch-Cut (0x5f01) for PT-P710BT using configured access policy, PAPPL ownership, the existing mutex, live media confirmation, checked output and bounded current-page readiness. Extend real IPP/socket-device tests for no job creation; exact blank-row output over USB/Bluetooth; held label and joined strip; CLOSED/OPEN_BLANK no-ops; unsupported PT-2730; invalid targets/document/job data/access denial; deterministic print/poll/cut contention and failed opens returning server-error-busy; media/status faults and ambiguous output recovery; and explicit cuts bypassing idle polling without changing its rate-limit slot. Verify no automatic retry or transport fallback.
- [ ] 2.2 Add the narrow PAPPL 1.4.11 operation-advertisement correction in both Dockerfiles and document removal criteria in patches/README.md. Verify real Get-Printer-Attributes responses contain exactly one complete operations-supported attribute, preserve all standard operations and explicit requested-attribute queries, advertise 0x5f01 only for P710BT, and apply the patch strictly in the adapter build.
- [ ] 2.3 Add a numeric ipptool request under tests/ and README instructions for scope, repeat behavior, busy/unavailable errors, recovery, and the remaining blank tail. Retain the direct hardware result from diagnostic commit 9e239ae with the exact sequence and failed zero-raster candidates in one tests/ evidence document. Run the documented request against the simulated held-run server and verify its response, bytes and unchanged job count. Distinguish physical Bluetooth/12 mm mechanism proof from software-only widths/transports and integrated IPP validation; require no additional physical output for completion.

## 3. Integrated validation

- [ ] 3.1 Run cmake -S . -B build, cmake --build build, and ctest --test-dir build --output-on-failure; build the full pinned adapter container and run its CTest/IPP checks; run strict OpenSpec validation. Record the results, including existing geometry, default-finishing, continuation and length regressions. No hardware deployment or new label matrix is part of this task.
