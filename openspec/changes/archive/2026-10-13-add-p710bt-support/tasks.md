# Tasks

## 1. Evidence and pure protocol support

- [x] 1.1 Preserve the available P710BT diagnostic source, rectangle captures, and status logs from `/tmp` on the development VM `openclaw` in a named external validation bundle, recording its host/path, hashes, and operator observations in the change's validation summary; verify hashes against design.md and report files lost before apply separately from the recorded physical observations.
- [x] 1.2 Add PT-P710BT `04f9:20af` metadata with 128-dot/180-dpi and raster-mode/PackBits flags in `src/tables.*`; verify lookup by ID/name, rejection of unknown metadata, and unchanged 32/52/76/120/128-dot geometry in `test_tables`.
- [x] 1.3 Add pure extended print-information, compression-selection, and automatic-status command builders in `src/protocol.*`; verify exact bytes, 32-bit page heights, first/subsequent indication, literal PackBits decoding, and unchanged legacy commands in `test_protocol` and the raster suite.
- [x] 1.4 Update the model-addition guide to document the P710BT command path and the explicit support gate without calling pending integration hardware-certified; review its protocol claims against the builders and retained probe evidence.

## 2. Model-consistent USB selection and queue identity

- [x] 2.1 Extend supported-model/URI resolution and identity formatting in `src/device_usb.*` for the two intended models; verify default PT-2730, explicit P710BT, unsupported metadata rejection, and exact URI validation with fake-USB tests.
- [x] 2.2 Make discovery/opening count the selected VID/PID and derive device IDs/diagnostics from that model; verify both models attached, selected model absent, an actionable attached-but-unselected model/PTOUCH_MODEL diagnostic, duplicate same-model devices, renumbering, claim/enumeration errors, and no fallback/output to the other model in `test_device_usb`.
- [x] 2.3 Derive normal/socket-override queue identity and the existing `probe`/USB `smoke` subcommands' VID/PID, URI, and messages from PTOUCH_MODEL in `src/main.c`, and use the immutable selected device in printer state; verify both diagnostic subcommands with fake USB, URI/identity rejection before commands, and socket transport guards without physical helper prints.
- [x] 2.4 Document explicit model selection, its unchanged default, one-queue/no-fallback behavior, enumeration-only `probe`, and the initializing USB `smoke` command's known-closed-run restriction in README; check the text against startup/fake-USB regressions and the architecture delta without recreating the retired ADR subtree.

## 3. P710BT output through the existing driver lifecycle

- [x] 3.1 Dispatch model-specific setup in `r_startpage`, with the validated destination row count, page indication 0 for a freshly initialized run and 1 for later/healthy-held continuation pages, notification, 14-dot feed margin, and PackBits; enlarge command buffers and verify exact production-callback captures for both models and cross-IPP-batch flag selection.
- [x] 3.2 Dispatch P710BT PackBits lines in `r_writeline` while retaining canonical geometry and packing; verify decoded full-width 9/12/18 mm rectangles, all five software media widths, and unchanged PT-2730 raster output in the driver/PNG suites.
- [x] 3.3 Apply invalidate/initialize only to a fresh closed P710BT run and preserve the current deferred print, FF, late chain-off/EJECT, and uncertainty latch; verify all four presets, unknown-count mixed-height pages, idle continuation, fragmented P710BT status, cancellation/fault handling, and no terminal replay in production-callback tests.
- [x] 3.4 Update protocol/finishing documentation for model-specific setup while keeping the client finishing contract unchanged; review it against decoded captures and the batch-printing delta, with physical P710BT acceptance still marked pending.

## 4. Container and runtime integration

- [x] 4.1 Parameterize the existing IPP/socket and PNG fixtures and `scripts/smoke-image.py` for selected PT-2730 and PT-P710BT, using a length-aware PackBits decoder, and wire both selections into `.github/workflows/deploy.yml`; verify model-correct captures, geometry/input failures, four presets, and invocation of both image-smoke variants before publication.
- [x] 4.2 Run the required full C build/checks (`cmake -S . -B build`, `cmake --build build`, `ctest --test-dir build --output-on-failure`) in the pinned container environment, plus explicit core-only checks and both runtime socket smoke variants; retain successful logs, check the CI wiring, and investigate failures before proceeding.
- [x] 4.3 Prepare the seven positive stages and zero-label wrong-media trial 5a, totaling 11 complete labels, with exact fixtures/captures, commands, timer-aware 10-second idle gap, button power-cycle versus USB-only disconnection, tape budget, and restoration procedure; verify no live output during preparation and present the concrete gate before running it, without extending the PT-2730 cut probe.

## 5. P710BT physical acceptance and certification

- [x] 5.1 After concrete-gate authorization, confirm P710BT/12 mm tape on vault through pfa's SSH connection, record the original service/image state, and start the isolated candidate; verify clean idle status/identity, adequate charge/power, auto-off assumptions, and no competing USB/Bluetooth printing client before output, using no initializing USB smoke preflight.
- [x] 5.2 Execute stages 1 through 4 without helper prints, keeping one candidate process and continuous USB/power through the 10-second idle gap; verify cuts/held ends/leader waste, cross-batch page flags, unknown-count lengths, job states, and captures, stopping on a fault or power loss without automatic flag fallback/retry.
- [x] 5.3 Execute the incomplete-stream/blocked-follower trials, actual button power-cycle and candidate restart, and counted stage-5 76-dot recovery label; after loading 9 mm on the closed run, run trial 5a requesting 12 mm with zero page output, then stages 6/7; verify USB-only queued recovery, intact 52/120-dot outlines, no ghost/replay, and 11 complete labels total.
- [x] 5.4 Restore any service temporarily yielded to the candidate and retain commands, captures, status/job logs, hashes, and physical observations; verify restoration and update README/acceptance documentation with the passed integrated scope and the separate software-only 6/24 mm coverage.

## 6. Final reconciliation

- [x] 6.1 Review implementation, tests, documentation, and evidence against all three deltas and unchanged rendering; verify P710BT physical acceptance, PT-2730 byte-identical print/adapter regressions, and a separately bounded PT-2730 physical check only if its emitted print bytes or physical command policy changed.
- [x] 6.2 Reconcile the change with main specifications, run strict OpenSpec validation, and complete local sync/archival through the OpenSpec workflow when finalizing; verify no unresolved task or failed acceptance remains before archival and keep pushing, pull requests, and production rollout outside this local implementation scope.
