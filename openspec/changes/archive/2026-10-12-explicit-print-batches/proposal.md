# Proposal

## Why

Queue timing cannot reliably identify a client's last label. The printer already advertises IPP trimming, but its driver ignores the job's finishing option and guesses whether to cut from the queue. Let the client make that choice explicitly.

## What Changes

- Use PAPPL's resolved job finishings: `none` leaves the tape uncut; `trim` cuts the job's labels and finalizes its last label.
- Remove page-start queue lookahead, end-of-job queue checks, and idle finalization. An empty queue never overrides `none`, and a queued follower never overrides `trim`.
- Retain the existing open-run state solely to avoid resetting an unfinished tape run between jobs, including jobs separated by idle time.
- Honor the printer's advertised finishing default, currently `none`, when an option is omitted. **BREAKING:** clients that require a cut must request `trim`; `PTOUCH_CUT_MODE` no longer controls printing.
- Preserve raster geometry, existing document formats and copy limits. Use checked writes and the printer's receiving-ready status for real USB page boundaries.

## Capabilities

### New Capabilities

- `batch-printing`: Client-controlled finishing and continuation across individual label jobs. The existing planning path is retained for this revised solution to the leader issue.

### Modified Capabilities

None. There are no main specifications yet.

## Impact

Changes primarily affect `src/driver.c`, timer registration in `src/main.c`, focused driver/IPP tests, README, and ADR-0008. No new document format, image parser, batch registry, dependency, or queue is added. Clients must send the finishing choice; changes to the separate labeler repository are not included. PT-2730 hardware validation remains required before claiming the leader issue fixed or integrating the change.
