# Design

## Context

See `proposal.md` for the incident. In the current driver, `r_startpage` confirms geometry/tape, releases any preceding pending page, latches uncertainty and sends setup; `r_writeline` subsequently checks the 14 logical end rows. `r_endjob` frees a failed batch without clearing that uncertainty. The existing `raster_end_ink_case` explicitly expects this lockout for both leading and trailing ink.

`pt_read_live_tape` sends ESC i S and accepts the first complete type-0 reply. A scratch probe compiled that unchanged function with a transport fixture containing an old 18 mm reply; sending the request appended a fresh 12 mm reply. The result was `accepted_width=18 fresh_reply_width=12 unread_bytes=32`. This establishes the queued-reply bug, not the provenance of job 40's original response.

## Goals / Non-Goals

Keep validation inside the existing rendered-row path, with the supplied pixels, placement and finishing unchanged. Avoid introducing a second PNG renderer or cancel/recovery state machine. The implementation changes page admission, not the required blank strips or the template's ink positions; the reported template will fail cleanly until its layout is corrected separately.

## Decisions

### Stage one P710BT page before sending it

Keep header/margin validation and the existing first-page tape wait in `r_startpage`, but allocate one packed-page buffer and defer P710BT setup and prior-page release. Pack rows and check the existing end strips in `r_writeline`; retain the 16-byte rows in memory rather than sending them. At successful `r_endpage`, release the prior pending complete label, run the existing setup sequence and transmit the validated interior rows. Leave the existing pending final command for the next valid page or normal job completion.

The buffer is owned by the existing batch, reused or replaced for the next page, and freed during the existing cleanup. Its bound is derived from the validated roll maximum: `16 * floor(30000 * 180 / 2540) = 34000` bytes, independent of copies or batch size. Reuse the callback options, row index and existing active/pending/failed flags; do not save a duplicate header, run snapshot or per-copy raster. PAPPL's existing complete-page/decode checks remain authoritative before `rendpage`.

Keep PT-2730 on its existing path. A P710BT rejection or cancellation during staging sends no page output and does not change the run state. If the same job already transferred an earlier page which remains uncertain, that existing uncertainty stays latched; this is not whole-job atomicity. Mark uncertainty immediately before actual output as today, so partial writes and failed readiness retain their current behavior.

**Alternatives:** Checking source PNG bands alone would duplicate PAPPL orientation/scaling/placement and would leave the existing trailing-raster case failing after transfer. Deferring only until the leading strip is checked leaves trailing ink with the same lockout. Sending invalidate/init after rejection can destroy a preceding held run and needs a separate recovery protocol. One bounded page avoids all three without retaining a whole batch.

This deliberately amends `server-architecture`'s prohibition on an extra raster buffer and the old unconditional recovery expectation for an incomplete first P710BT page. Successful page bytes, feed accounting, pending-label policy and true interrupted-output recovery retain their contracts.

### Remove queued input before asking for tape status

In the shared `pt_read_live_tape` exchange, discard input already available on the owned USB or Bluetooth connection until the existing bounded reader reports no data, then issue ESC i S and assemble its response as today. Use the existing bounded status budget for the exchange; inability to establish that input boundary leaves media unconfirmed rather than publishing the queued value. Do not initialize or reconnect merely to detect tape. Socket sinks retain their current lack of live status.

Adapt the existing driver transport fixture so request replies become readable after the corresponding status-request write, with stale input separately available beforehand. Release print readiness notifications only after their corresponding FF/EJECT write as well. Its current eagerly readable list models future replies and readiness notifications as already queued and would otherwise be drained along with stale data. Reuse existing fragmented-reply, no-reply, I/O-error and held-run coverage instead of adding another fixture framework.

Add one job log entry at the tape decision with requested and accepted reported widths, using PAPPL's existing logging. The user requested an explanation of the mismatch, and the incident logs omitted the accepted reading. No packet archive, diagnostic setting or new persistent record is needed.

**Alternative:** Issuing a second request without discarding queued input merely consumes the next queued response and does not establish freshness, as the probe demonstrates.

## Risks / Trade-offs

- P710BT output begins after the page is rendered, using at most 34,000 additional bytes. This is the bounded cost of checking trailing ink before output; copies reuse the existing per-page lifecycle.
- A later invalid page can still leave an earlier transmitted page uncertain. Preserve existing abort semantics and demonstrate both an incomplete first page with no output and an incomplete later page with prior output.
- Brother status replies have no request identifier. Draining fixes the demonstrated prequeued-input case; it cannot prove that a delayed older reply arriving after the drain belongs to the new request, or correct a cassette sensor reporting the wrong width. Do not describe the historical mismatch or physical tape detection as resolved by software tests.
- Moving writes changes where driver fixture failures occur. Preserve assertions about actual bytes, finishing and recovery while moving them to the callback that now transfers the data; do not delete failure coverage to make timing-dependent tests pass.

## Migration Plan

No persisted state or client migration is introduced. Update README's rejection/recovery explanation with the implementation. The eventual loop run performs the repository's required full build and adapter checks; planning does not access or change a live printer, restart a service, or print acceptance labels. Existing physical certification is not extended by these software regressions.

## Cut Pass

Removed from the draft: a separate core-only build/test run in task 3.1. The required full build already includes the same core suites; only adapter code changes, so repeating the core-only configuration adds no coverage for this failure. No new requirement was added for tape matching or pre-output rejection preserving a healthy run: the existing specs already require both.

The MODIFIED blocks retain unchanged context/scenarios so sync does not delete them. The changed sentences/scenarios and each retained mechanism, state addition, record, test and task are audited below.

| Item retained | Clause or request served | Simpler version and its concrete failure |
| --- | --- | --- |
| Complete-page-before-output sentence | label-rendering: Page length includes P710BT end feed, as amended | Leading-strip-only validation still transfers raster before the existing trailing-ink case fails. |
| Nonblank-end-row scenario's new THEN | Same amended requirement | Old THEN retains the lockout expectation seen in `raster_end_ink_case`. |
| Incomplete-blank-trailing-rows scenario's new THEN | Same amended requirement and Raster pages retain their submitted geometry | Old THEN leaves an unconditional recovery expectation for a first page that now sends nothing. |
| Lifecycle sentence allowing one bounded P710BT page | server-architecture: Constant-size deferred batch lifecycle, as amended | Existing no-buffer wording prohibits the selected complete-page validation. |
| One 16-byte-row page allocation | Amended complete-page validation | No buffer requires early transfer or a second render; both preserve the observed late-validation problem or duplicate placement. |
| Buffer ownership in existing batch | Same amended requirement | An unowned allocation cannot be freed by the existing failed-job cleanup. |
| Buffer size derived from validated page height | Amended bounded lifecycle | Per-copy or batch storage grows unnecessarily for the existing 999-copy case. |
| Deferred setup and prior pending-page release | Amended complete-page validation; existing healthy continuation | Current `r_startpage` mutates the printer before row 11 is rejected. |
| Existing active/pending/failed flags reused | Existing deferred lifecycle and failure handling | Additional validation/run snapshots duplicate state; removing existing flags loses deferred finalization and abort handling. |
| Shared pre-request input discard | Existing printer-device live-media confirmation | The unchanged-function probe accepts queued 18 mm ahead of fresh 12 mm. |
| Existing bounded status budget reused | Existing bounded status handling and live confirmation | A second request alone leaves the stale-response problem; an unbounded drain would discard the existing finite status-exchange bound. |
| Request-triggered fixture snapshots | Existing live-media tests | Current eagerly readable future snapshots would be mistaken for stale input. |
| FF/EJECT-triggered fixture readiness | Existing finishing/readiness tests | Current eagerly readable future notifications would be drained before output generates them. |
| Existing leading/trailing end-ink cases revised | Amended end-row scenario | Their current expected setup/uncertainty explicitly accepts the regression. |
| Incident-shaped PNG followed by valid job | User's reported PNG lockout; amended end-row rule | Raster-only coverage misses the actual PNG renderer/callback route that failed at row 11. |
| Existing held-run rejection case extended | Existing healthy continuation and Rejected follower | Closed-run-only verification does not establish preservation of the held content a reset would destroy. |
| Existing first/later truncation cases adapted | Amended incomplete-page scenario; existing interrupted-output recovery | Keeping only the first case misses prior transferred output; keeping old expectations rejects the new no-output first-page behavior. |
| Existing write/readiness/finishing/cut regressions retained | Existing batch and uncertain-output contracts | Moving writes without retaining these assertions can hide failures now occurring at end-of-page. |
| Existing PT-2730 capture assertions retained | Existing PT-2730 byte preservation | P710BT-only verification cannot establish that the other model's emitted bytes stayed unchanged. |
| Queued-18/fresh-12 tape-wait regression | Existing Wrong-tape jobs wait for matching tape | Existing tape fixtures lack the stale type-0 reply that the probe demonstrates is accepted. |
| One requested/reported-width job log entry | User's request to explain the tape mismatch | Current matching-path logs omit the accepted tape reading, leaving the incident dependent on inference. |
| README rejection/recovery update | User's request to fix the routine lockout | Existing recovery prose does not distinguish a rejected staged first page from interrupted printer output. |
| Task 1.1 | Amended page-validation and existing continuation clauses | Fixing only PNG or only the leading strip leaves the existing shared-driver trailing case unchanged. |
| Task 2.1 | Existing live-media/wrong-tape clauses and requested diagnosis | A page-validation repair alone leaves the reproduced tape-reader bug. |
| Task 3.1 | Repository required C/adapter checks | Core tests alone do not execute the callbacks changed by either implementation task. |

No new configuration option, recovery command, persistent state, duplicate header, row counter or copy-sized record is required.
