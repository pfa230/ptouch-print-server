# Proposal

## Why

The driver decides whether to finalize before the current label starts, missing jobs that arrive during printing and wasting a leader at the next job. Move the queue decision to job completion so consecutive queued work prints as one tape run, regardless of client batch boundaries.

## What Changes

- Plan labels in continuation mode, retaining the existing per-label cut policy and PNG/copies path.
- At job completion, while that job still owns the device, check for another runnable job. Leave the run open when one exists; otherwise finalize immediately, without a grace period.
- Remove the queue lookahead and replanning from page start. Preserve initialization suppression between chained jobs, media confirmation, and existing stranded-run cleanup.
- Verify actual command transfers before marking the run finalized, using the existing smoke probe's device-metrics pattern.
- Keep the IPP formats and clients unchanged. A client batch boundary does not end a run; an empty queue at the completion check does. Later arrivals can therefore still incur a new leader.

## Capabilities

### New Capabilities

- `batch-printing`: Continuous printing of runnable queued jobs and finalization when the queue drains. This existing planning path now describes queue runs, not a new batch document.

### Modified Capabilities

None. This repository has no main specifications. Revise ADR-0008's early decision point while preserving its cross-job chaining contract.

## Impact

- Primarily `src/driver.c`, with focused cutter/adapter tests, README, and ADR-0008 updates. Reuse existing command builders, state, and PAPPL scheduling.
- No new filter, MIME type, dependency, batch registry, queue, raster encoder, or labeler changes.
- Preserve image geometry, strict width rejection, offline recovery, normal no-cut behavior, and PNG copy limits. Queue ordering and the existing `end` plus precut defect (#42) are outside scope.
- The proposed immediate FF followed by a separate eject failed the initial PT-2730 probe: the user observed only a leader, while the baseline printed its rectangle. Production implementation is paused until the finalization mechanism is revised and physically verified. See `design.md` and `tests/CUT-PROBE.md` for the evidence.
