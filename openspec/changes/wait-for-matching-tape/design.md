# Design

## Context

See proposal.md for motivation and the label-rendering delta for the required behavior.

The tape check lives in `r_startpage` (`src/driver.c`). PNG jobs reach it through `papplJobFilterImage`, and PWG raster and URF jobs reach it directly, so it is the one point where every format's page geometry meets the loaded tape. `r_startjob` has already read the live tape through `pt_refresh_ready_media` and stored its width in the batch as `tape_mm`. On the first label, nothing has been written to the printer when the check runs: the run-state change, `ESC @`, and page setup all come after it.

By then PAPPL has moved the job to `processing`. PAPPL 1.4.11's `papplJobHold` refuses any job at or past `processing` (`job.c`, "Only hold jobs that haven't entered the processing state"), so a driver cannot park a running job and let others pass. The job thread owns the device until the callback returns, and `status_cb` skips polling while the printer is processing, so only the job thread can observe a cassette swap.

## Goals / Non-Goals

**Goals:**

- One wait point that covers PNG, PWG raster, and URF jobs.
- Reuse the existing live read and media publication, so the web UI and IPP clients see the new tape as soon as the job does.

**Non-Goals:**

- Letting matching jobs overtake a waiting one. That needs a PAPPL scheduler patch; the user chose to block the queue instead.
- Waiting when `r_startjob` cannot read a clean tape at all, for example with no cassette or the cover open at job start. That path still fails as today.
- Showing the waiting message in PAPPL's web job table. The `show-job-failures` change renders `job-state-message`; this change only sets it.

## Decisions

### D1. Wait inside `r_startpage`, before any output

Split the existing guard into a tape-independent part (resolution, bits per pixel, page length) and a width part. If the tape-independent part fails, fail as today. Otherwise find the supported tape whose geometry `pt_page_width_matches` the page. No match fails as today. A match equal to `tape_mm` continues as today. A different match on the first label waits (D2); on a later label it fails, because a cassette swap would end the run with the earlier labels already printed.

The physical-window check then runs against the matched tape. A PWG raster or URF job without explicit IPP media gets its page dimensions from the raster header (`patches/pappl-1.4.11-raster.patch`) but keeps the margins PAPPL copied from the loaded tape's default media, so a 12 mm page built while 18 mm is loaded carries 18 mm margins and would fail the window check even after the swap. Before the window check, margins the job did not submit in `media-col` are replaced with the matched tape's published margins (`media_col_for`); margins the job submitted stay authoritative and are validated as they are today. When the matched tape is the loaded tape, inherited margins already equal its published margins, so matching jobs are unaffected.

The wait sits after the uncertain-run check and before the pending-label release and `pt_run_set(PT_RUN_UNCERTAIN)`, so a waiting job sends no print command (initialization, page setup, or raster) and never marks the run uncertain. It does send status requests (D2), and publishing a changed tape closes a healthy run as idle polling already does.

Alternatives: waiting in `r_startjob` fails because raster page geometry is not known until the first page header arrives. Waiting in the PNG filter would cover only PNG. Holding the job before processing needs a PAPPL patch to the scheduler.

### D2. The wait loop

Every 2 seconds, until the job is canceled:

1. Read the tape with `pt_read_live_tape`.
2. If the read returns an I/O error (`n < 0`), abort with "Cannot read loaded tape while waiting for <N> mm tape". A socket sink takes this path on the first read, which preserves today's abort-without-output for the socket smoke and PNG geometry tests without a transport special case.
3. Publish the result with `pt_publish_tape`. It updates `media-ready` and closes a healthy run when the width changes, as idle polling already does.
4. If the published tape matches the wanted width, set `tape_mm` and continue with page setup. Otherwise set the printer's `media-needed` reason and the job message "Waiting for <N> mm tape; <M> mm loaded" (or "no tape loaded" when no cassette is read), then sleep.

A fault, a missing cassette, or no reply (`n == 0`) keeps waiting, since an open cover is a normal part of a swap. The job logs once when the wait starts and once when it ends, not on every poll.

The 2-second interval keeps cancellation and swap detection prompt without flooding USB with status requests. A read itself is bounded at about 4 seconds, so cancellation takes effect within about 6 seconds.

Alternative: reuse `pt_refresh_ready_media` with a longer deadline. It treats any unreadable or faulted status as a reason to fail and is bounded at 10 seconds; bending it to wait indefinitely would change the startup-barrier behavior it exists for.

### D3. No time limit

The user chose an indefinite wait ended by a tape swap or cancellation. There is no configuration setting.

### D4. Hardware acceptance is an operator follow-up

The user decided that cassette-swap acceptance does not block completion of this change. The change completes on automated verification: the driver tests, the socket wrong-tape checks, and both image smoke variants. After deployment, the operator checks the swap on each printer and records the result in `tests/HARDWARE-ACCEPTANCE.md` (PT-2730) and `tests/P710BT-ACCEPTANCE.md` (PT-P710BT, with 9, 12 and 18 mm tape). The check: with 18 mm tape loaded, submit a 12 mm job and confirm the waiting message and `media-needed`. Then swap the tape and confirm that exactly one label prints at the correct size. Finally, confirm that a job canceled during the wait prints nothing.

## Risks / Trade-offs

- [A forgotten wrong-width job blocks the queue until someone swaps tape or cancels it] → Accepted by the user. The printer reports `media-needed`, and the job message names the needed tape.
- [`pt_publish_tape` logs an error on every poll while the printer reports a fault, such as an open cover] → About one line every 2 seconds during a swap. Accept it; the log is bounded by how long the cover stays open.
- [Clients may time out waiting for job completion] → The job stays visible and completes later; the labeler and CUPS treat a processing job as accepted, and the job's state is still queryable.
- [Unplugging or powering off the printer during the wait aborts the job] → The job's device handle cannot recover from a removed device. The abort message names the cause, and the user resubmits.

## Migration Plan

Ship in the normal `edge` image. Rollback is the previous `sha-<commit>` tag. No state migrates.
