# Design

## Context

See proposal.md for the revised scope. The user explicitly replaced queue inference with the job's finishing option on 2026-10-11.

The driver already advertises `PAPPL_FINISHINGS_TRIM`. Pinned PAPPL 1.4.11 resolves `finishings` or `finishings-col`, falling back to `finishings-default`, into `options->finishings`. Its current default is `none`. The driver instead reads `PTOUCH_CUT_MODE`, replans at the final page's start when another job is pending, and cuts stranded runs from an idle timer.

The old FF followed by a separate EJECT failed on hardware. It is no longer part of this design. The normal chain-off/EJECT single-label baseline printed successfully. The diagnostic late-chain-off variant also cut a single rectangle free. Multiple-page probes exposed a separate page-boundary transfer failure: accepting print-completed status before receiving-ready left a stale receiving-ready packet for the next page. The actual IPP driver with receiving-ready handling printed three trim copies on 2026-10-12; the user observed one leader and three X labels, all cut apart including the last. Retain the diagnostic and physical evidence in tests/CUT-PROBE.md.

## Goals / Non-Goals

**Goals:** Make the client finishing choice authoritative with less driver logic, retaining existing raster and cutter command builders.

**Non-Goals:** Predicting client batches, queue inspection, automatic idle cuts, new client transport, image changes, queue reordering, other models, or modifying the separate labeler app.

## Decisions

### Resolve cutting once from the job option

Select `PT_CUT_EACH` when `options->finishings` contains `PAPPL_FINISHINGS_TRIM`, otherwise `PT_CUT_NONE`. Use the existing terminal cutter plan once with `chain_out=false`; remove queue-driven replanning. `trim` cuts copies individually and finishes the final copy with the established chain-off/EJECT sequence. `none` disables all cutter commands, including precut, and ends pages with continuation FF. Keep `PTOUCH_PRECUT` only as the leader policy for trim jobs. When it is off, disable autocut on the first copy and enable it before every later copy, including the terminal one; disabling it on the terminal copy left the last two copies joined in the 2026-10-12 physical test. The old `PTOUCH_CUT_MODE` is obsolete rather than an override of the client.

Use PAPPL's existing advertised default, including any supported configuration of it. Do not add a hidden fallback to the old environment setting. Both enum finishings and finishings-col templates pass through PAPPL's normal resolution.

### Retain only physical run state

Retain the bounded per-printer open-run/media state and its lock. Mark an unfinished run open before writing its page terminator; clear it only after a checked terminal EJECT transfer. A no-cut job with no available state slot fails before output rather than silently cutting or starting an untracked run.

Skip ESC @ on subsequent jobs while that state is open, even when PAPPL closed and reopened its USB handle during idle time. Preserve rasterstart. During an unfinished run, request status without initialization, including before building the next PNG job's geometry, so cassette changes remain detectable without resetting tape. Absence of the device still clears stale state through the existing presence check. Remove all timer registration, idle counters, job scans, and automatic end-of-job detach. Cancellation or failure does not invent an unrequested cut; cleanup frees job data and never replays raster or terminal output.

### Check transfers and page readiness

Use transferred-byte metrics around write and flush, following the existing smoke probe, so buffered acceptance cannot report a successful print or cut. Return failure through raster callbacks on detected transfer errors.

On the custom USB transport, after a page terminator consume unsolicited status until the printer reports receiving-ready (type 06, phase 00, zero phase number), with a bounded deadline and error/cancellation handling. Print-completed (type 01) alone does not establish readiness. Initial media queries consume earlier notifications until their requested type-00 snapshot so those packets cannot satisfy a later page wait. Do not send a status request or a new page while waiting. Socket-based tests skip USB reads and use deterministic media/status fixtures. This is printer flow control, not a batching delay or cut decision.

## Risks / Trade-offs

- Existing clients omit finishings and therefore use the advertised none default: document that they must request trim for automatic cutting.
- A client never submits its terminal trim job: the tape stays uncut until an explicit trim job or physical action; there is no idle fallback.
- A failed terminal write has uncertain physical outcome: report failure without automatic replay or a claimed successful finalization.
- USB completion semantics and cuts cannot be certified by socket captures: verify the revised sequence on the PT-2730 before integration.
- Process restart loses open-run state: persistence across server restart is not introduced; document this existing limitation.

## Migration Plan

1. Implement and test finishing resolution and removal of automatic cutting, including enum and collection options, defaults, copies, cancellation, failed transfers, idle gaps, and queue independence.
2. Update README and ADR-0008 with client examples and the obsolete environment variable.
3. Complete PT-2730 verification with explicit none/trim jobs, final cuts, leader counts, lengths, and offline recovery. Preserve diagnostic evidence separately from success claims.
4. Sync specs and archive before merging.
