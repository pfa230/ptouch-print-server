# Design

## Context

See `proposal.md` for the problem and `specs/batch-printing/spec.md` for the contract. The existing change identifier is retained; this revision replaces the multi-page transport proposal with a server-only queue-draining fix.

Verified in local source:

- `r_startpage` scans PENDING jobs and replans the final label before sending its mode commands. A later arrival cannot change that plan.
- `pt_plan_batch(..., chain_out=true)` already produces continuation mode (`SETADVANCED 0x00`) and FF. It changes the final-label autocut setting for `each` with precut off; physical compatibility must be verified rather than claiming identical commands.
- `r_endjob` already checks the queue and ejects a stranded run while holding the device. PAPPL schedules the next job after the current filter returns.
- Per-printer state already records an open run and media confirmation. `r_startjob` skips `ESC @` for an open run; resetting mid-run previously stopped the PT-2730.
- The existing timer finalizes stranded runs. It is cleanup machinery, not a necessary delay for successful queue draining.
- PAPPL's buffered write return does not prove transfer, and its flush discards errors. `run_smoke` already checks actual transferred bytes using `papplDeviceGetMetrics`.

The October 10 deployment logs showed Job 26 accepted milliseconds after Job 25's geometry was prepared but before Job 25 finished; Job 25 nevertheless finalized. The queue policy has not been implemented; the proposed terminal sequence failed the hardware probe below.

## Hardware gate result (2026-10-11)

On the PT-2730 with user-confirmed 24 mm tape, the single-label `each`, precut-on baseline transferred successfully and the user observed one leader and one printed rectangle. The immediate continuation FF followed by separate EJECT also transferred successfully, but the user observed only one leader and no rectangle. Captures contain identical raster payloads; they differ in advanced mode (`08` versus `00`) and the drain's additional FF before EJECT. See `tests/CUT-PROBE.md` for the recorded evidence.

This failed gate blocks the production changes below. Their finalization mechanism remains a proposal, not a validated implementation plan. The cause is unknown; a delay or alternate command cannot be assumed to fix it. Stop further physical comparisons and revise the mechanism with the user before resuming.

## Goals / Non-Goals

**Goals:** Change the finalization decision point with the smallest driver change; preserve the existing rendering, spooler, command builders, and cross-job state.

**Non-Goals:** Explicit client batches, new document formats, upload delays, an idle grace period for normal jobs, queue reordering, image-processing changes, other printer models, and fixing issue #42. This cannot predict future submissions or guarantee one leader for a client batch whose jobs arrive after the queue has drained.

## Decisions

### 1. Prepare for continuation; decide to finalize at job end

For cutting-enabled jobs, reserve the existing per-printer chain slot before selecting a continuation plan. Plan the copies once with `chain_out=true`; remove the page-start queue scan and replanning. If the existing state table is unavailable, retain the existing safe terminal-plan fallback rather than leave an untracked open run.

Keep per-page raster, mode commands, and FF. When the last page has been submitted, mark the run open. At normal `r_endjob`, exclude the current job and look for another PENDING job using the existing iterator:

- Another runnable job: leave the run open for its ordinary chained startup.
- No runnable job: send one separate EJECT immediately, before releasing device ownership, and clear the open flag only after a successful transfer.

Held, incomplete/uploading, canceled, and stopped jobs do not keep a normal run open. The scan is the run-boundary decision point: a submission after it can start a new run even if it arrives before the eject is sent. No second queue or new synchronization protocol is needed. Client batch identifiers have no role. PAPPL's existing scheduling order is retained; do not promise FIFO.

Continuation flags precede the raster, so simply moving the current replan to job end would be too late. Preparing for continuation and ejecting separately avoids that early commitment. Use a small test-only probe built from the existing command builders and custom device scheme: send initialization/rasterstart, `pt_plan_batch(..., chain_out=true)` with known raster fixtures, and an immediate `pt_cmd_eject` after the final FF, all on one open `ptouch://` handle. Do not substitute the idle timer's delayed close/reopen sequence. Compare against the current terminal plan (`chain_out=false`) as a physical baseline and verify the final label is cut free without extra blank tape. If the sequence fails, stop and revise the plan with the user rather than silently introducing a delay or new transport.

For `each` with precut off, a single terminal label currently uses `SETMODE 0x00`, `SETADVANCED 0x08`, EJECT; the continuation plan uses PRINTINFO, `SETMODE 0x40`, `SETADVANCED 0x00`, FF, then the separate EJECT. The final copy changes similarly. Require no newly introduced leader cut when precut is disabled. Probe single/multiple labels with `each` precut on/off and `end` precut off before changing the production driver; this expected physical compatibility is an unproven gate.

### 2. Reuse state and cleanup

Retain the existing open-run flag, lock, printer-ID lookup, media-confirmation state, and timer. Keep chained startup's suppression of `ESC @` and retain rasterstart. Do not add any normal idle countdown; successful queue-empty finalization occurs inside the finishing job.

The existing timer remains a fallback if a queued follower is canceled, fails validation before driver startup, or never prints. After acquiring maintenance device ownership, recheck the run is still open and no runnable/processing job remains before ejecting; clear the flag before closing the device. Preserve the existing fallback interval; it is not a batching window. Failed acquisition leaves cleanup pending, but is not guaranteed harmless: PAPPL's existing maintenance-open path can overwrite a retained device handle when held/stopped jobs remain active. Document that existing best-effort recovery limitation; a PAPPL device-lifecycle fix is outside this change.

Cancellation and early output failure retain best-effort detach and prevent a duplicate successful eject. Preserve normal `none` behavior without a new final eject, and preserve the existing `end` plus precut limitation. A callback repeated during PAPPL abort cleanup must find no freed job data and must not emit another successful terminal command.

### 3. Confirm transfers before recording finalization

Use a checked write-and-flush helper for print/eject command paths, following `run_smoke`: compare transferred-byte metrics around the write and flush, not just buffered acceptance. Keep buffers drained between these operations. The check proves transport transfer, not physical printing.

Propagate a failed raster or terminal transfer through the driver callback so PAPPL aborts the current job. Keep a failed cleanup run marked open for existing best-effort recovery; do not automatically resend the label raster. Do not promise that a dead USB handle is reopened for already queued followers; that is an existing dependency limitation, not a new recovery subsystem. No physical-completion acknowledgment is introduced.

## Risks / Trade-offs

- Queue actually empty at completion: finalize now; a later arrival gets a new leader. This is the accepted policy, not a timer defect.
- FF followed by separate EJECT may change physical output: make the small PT-2730 probe a gate before broader implementation. Socket captures cannot prove leader count or cut position.
- Job disappears after the continuation decision: retain the existing stranded-run timer and test cancellation/validation failure.
- Device or media failure: retain strict width checks and media recovery, report detected transfer failure, and avoid promises of transparent device-loss recovery.

## Migration Plan

1. Confirm the command sequence on the PT-2730 when hardware testing is authorized; retain the result as evidence. Do not proceed on a failed probe.
2. Implement the local driver change and focused tests. Update README and ADR-0008 to explain the completion-time queue check and late-arrival boundary.
3. Run the core gates and full PAPPL 1.4.11 container tests. Use a protocol-aware socket capture for software behavior; USB media refresh requires a USB-level mock or hardware because the socket path skips it.
4. Complete hardware validation, sync specs, and archive the OpenSpec change before any merge. An apply request does not authorize an earlier merge.
5. Roll out the server only when deployment is authorized, through pfa's SSH connection to vault. Verify the deployed revision and actual labeler output; retain the prior server image tag for rollback. Labeler requires no deployment change.

## Validation

Capture a single PNG, copies, a follower accepted after page start but before the end-of-job check, and a follower submitted after the previous job completed. For the during-print case, backpressure the socket sink with a small receive buffer and paused reads, use a sufficiently large copies job, and verify from captured progress/logs that the current job remains in progress when the follower is accepted; do not assume a paused receiver blocks small buffered jobs. For the late-arrival case, wait for Completed instead of trying to hit the tiny scan-to-return interval. Under default settings continuing jobs have no intervening reset or EJECT; the drained run has one separate terminal EJECT without a normal timer wait. Use actual observed processing order for comparison.

Use a socket test fixture with fixed online media/status, leaving production USB status handling unchanged. Otherwise printer-attribute polling without a USB device invokes `status_cb`, clears the run-open flag, and marks the printer offline, invalidating a cross-job capture. USB media refresh remains a separate mock/hardware check.

Also exercise a canceled follower, a malformed follower failing before driver startup, cleanup overlapping new work, failed FF/EJECT transfer, and normal `none` plus existing `end` configurations. Compare raster contents, dimensions, and copy limits against baselines. Physical verification must record leader count, label separation, final-label length, and absence of an extra blank label for both a single job and a drained queue.
