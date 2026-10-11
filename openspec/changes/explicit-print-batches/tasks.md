# Tasks

## 1. Prove separate finalization

- [x] 1.1 Add a small test-only probe using existing command builders and the custom device scheme: initialize/rasterstart, emit `pt_plan_batch(..., chain_out=true)` with known raster fixtures, then send `pt_cmd_eject` immediately after the final FF on the same open ptouch handle. Include the current `chain_out=false` terminal plan as baseline; verify captures distinguish commands from raster and document exact sequences and transfer checks without changing production callbacks.
- [ ] 1.2 When hardware testing is authorized, compare those sequences on the PT-2730 for single/multiple labels, `each` with precut on/off and `end` with precut off; record leaders, label separation, final-label length, the final label actually cut free, and absence of an extra blank label. Explicitly verify `each` precut=0 introduces no leader cut despite its changed final-label autocut commands. Stop and revise on failure before production driver changes; socket output cannot complete this gate.

Hardware gate failed on 2026-10-11: the single-label drain probe produced only a leader, while the baseline printed its rectangle. Task 1.2 remains incomplete; production tasks are paused pending an agreed revision. See `design.md` and `tests/CUT-PROBE.md`.

## 2. Minimal driver change and local checks

- [ ] 2.1 Introduce checked write-and-flush handling on print/eject paths using the existing smoke probe's transferred-byte metric pattern; verify injected raster/FF/EJECT failures return failure and buffered acceptance alone cannot mark a run finalized.
- [ ] 2.2 Select the existing continuation plan once for cutting-enabled jobs with reserved chain state; remove page-start queue lookahead/replanning, retaining chained startup, rasterstart, geometry, copies, and the safe state-capacity fallback; verify cutter tests and captures cover supported copies, `each`, normal `none`, and current `end` behavior.
- [ ] 2.3 Make normal end-of-job cleanup decide from the runnable queue while holding the device: continue if another PENDING job exists, otherwise eject immediately. Verify the during-print case using a backpressured socket sink and a sufficiently large copies job, confirming processing is still underway when the follower is accepted; test late arrival by submitting after the previous job is Completed. Pin online media/status in the test fixture so absent-USB polling cannot invalidate captures. Verify one terminal EJECT per drained run without a normal timer delay.
- [ ] 2.4 Retain cancellation/early-failure detach and the existing stranded-run timer, adding its post-acquisition run/queue recheck; verify canceled and malformed followers, cleanup overlapping new work, failed cleanup, and repeated PAPPL abort callbacks do not interleave output, reprint raster, duplicate a successful eject, or access freed job data.
- [ ] 2.5 Update README and ADR-0008 with the completion-time boundary, immediate queue-empty finalization, client compatibility, unchanged queue ordering, and issue #42 limitation; verify their claims match the captures and revised specification.

## 3. Integration and release gates

- [ ] 3.1 Run core gates (`cmake -S . -B build`, `cmake --build build`, `ctest --test-dir build --output-on-failure`), the full PAPPL 1.4.11 deploy-container build, IPP/socket regression scenarios, and strict OpenSpec validation; record actual results. Confirm formats/raster geometry/copy limits are unchanged. Verify offline media refresh with a USB-level mock or hardware, because socket capture cannot exercise it.
- [ ] 3.2 When hardware testing is authorized, verify actual unchanged labeler submissions arriving during printing and a prequeued mixed-label run on the PT-2730; record leaders, cuts, lengths, processing order, immediate final detach, and offline recovery. Keep this gate open if only software evidence exists.
- [ ] 3.3 After implementation and required verification, sync specifications and archive this change before merging; verify the accepted spec and archive exist, then follow the repository's integration convention. An apply request alone does not authorize an earlier merge.
- [ ] 3.4 When rollout is authorized, deploy only the server through pfa's SSH connection to vault, verify its revision and actual labeler output, and retain the prior image tag for rollback; do not report the physical leader issue fixed from socket tests alone.
