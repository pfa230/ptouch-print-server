# 8. Chain consecutive queued jobs, and never re-init mid-chain

**Status:** Accepted

## Context

Chaining existed only within a job (copies). Because a PNG is single-page, N different labels must be
N separate IPP jobs, so a pool of queued jobs - the normal case after the printer has been off - trimmed
a fresh leader and finalised for every job, wasting tape.

## Decision

When another job is genuinely pending, end the current job chained (`SETADVANCED 0x00` + `FF`) instead
of finalising (`0x08` + `EJECT`), and **skip the `ESC @` initialisation** when starting into an open
chain. Keep `rasterstart`.

## Consequences

- A pooled run produces one leader for the whole run instead of one per job.
- `ESC @` mid-chain is what wedged the printer: it reported "Tape cassette changed!" and stopped
  accepting data, needing a physical power-cycle. PAPPL keeps the device open while jobs remain
  active (it closes only when the active count reaches zero), so the reset landed on a live,
  unfinalised stream.
- There is no separate "pre-cut" command. Pre-cut is autocut; the leader exists only because the
  previous job's `EJECT` advanced tape past the cutter. Chaining removes the cause, so the following
  job needs no special handling beyond skipping `ESC @`.
- A boundary only chains if the next job is already queued when the current job's final label starts,
  since the chain bit precedes the last page's raster. Rapid-fire submissions may not chain the first
  boundary; the pooled case, which is the one that wastes tape, chains fully.
- Supporting machinery: a pending-job scan (`papplPrinterGetNumberOfActiveJobs` counts held and
  incomplete jobs and reads unlocked, so it is unsuitable), per-printer state keyed by printer id
  (never a raw pointer, which can dangle), and an idle timer that finalises a chain left open.

## Alternatives considered

- **Expose chaining as a per-job `finishings` option.** Simpler and still possible as an enhancement,
  but it does not avoid the `ESC @` defect - the printer does not care who decided to chain.
