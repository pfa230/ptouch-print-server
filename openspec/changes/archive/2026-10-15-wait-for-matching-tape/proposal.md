# Proposal

## Why

A label sent for a tape width other than the loaded one is accepted and then aborted about 200 ms later, so nothing prints and the client sees only that its submission succeeded. On 2026-10-15 job 6 requested 12 mm tape while 18 mm was loaded and aborted with "Page geometry does not match loaded 18 mm tape at 180 dpi". The user has to notice the failure, swap the cassette, and resubmit, when the server could have waited for the swap the way a physical printer asks for media.

## What Changes

- **BREAKING** A job whose first label is valid for a different supported tape width than the loaded one no longer aborts. Before it sends any print command to the printer, the job waits until the loaded tape matches, then prints normally. Only status requests reach the printer during the wait.
- While it waits, the job stays `processing` with the message "Waiting for <N> mm tape; <M> mm loaded", and the printer reports `media-needed`. The job keeps the printer, so later jobs queue behind it, as on a physical printer that asks for media.
- The wait has no time limit. It ends when the matching tape is loaded, when the job is canceled, or when a status read fails with an I/O error, which aborts the job with a specific message. An open cover, a printer fault, a missing cassette, or a status request without a reply during the swap does not end the wait.
- Geometry that matches no supported tape width, and a later label whose width differs from the batch's tape, still fail as they do today. Nothing is ever printed on the wrong tape.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `label-rendering`: "Advertised tape media is usable" replaces the "Wrong tape" failure scenario with waiting for the matching tape, cancellation, and loss of status.
- `server-architecture`: "Strict physical rendering" says that wrong-tape geometry never reaches the printer, instead of saying that it fails.
- `batch-printing`: "Existing rendering is preserved" keeps strict tape-width matching rather than rejection.

## Impact

Code: `src/driver.c` gains one polling wait in `r_startpage` before the first label's page setup, reusing `pt_read_live_tape` and `pt_publish_tape`. One driver function resolves margins a job did not submit against the tape its page matches, so a job without explicit margins does not keep the loaded tape's margins. `r_startpage` calls it before its window check for PWG raster and URF jobs, and `src/filter_png.c` calls it before `pt_preflight_png`, because PNG preflight and PAPPL's image placement both use the margins before `r_startpage` runs.

Tests: `tests/test_driver.c` covers waiting then printing, cancellation during the wait, and loss of status during the wait, both through the driver callbacks and through real PNG, PWG raster, and URF IPP submissions swapped between 12 mm and 18 mm, including a PNG whose content fits the selected tape but not the loaded tape's inherited margins. The existing socket-transport wrong-tape checks in `tests/test_png_geometry.c` and `scripts/smoke-image.py` still expect an abort with no output, because a socket sink has no status path to wait on.

Hardware: PT-2730 print bytes for a matching tape do not change. A physical check swaps the cassette while a wrong-width job waits, on PT-2730 and on PT-P710BT.

Interaction: the in-progress `show-job-failures` change also edits `src/driver.c` messages and renders `job-state-message` in the web UI. The two changes are independent; whichever lands second rebases on the other.

No IPP attribute, advertised capability, or client protocol changes.
