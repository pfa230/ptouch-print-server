# Proposal

## Why

The page a user opens, `/`, lists no jobs. It shows only "Idle, 0 jobs", and that count covers queued and printing jobs only. Finished and failed jobs appear only on the printer page, `/ptouch/`, and there a failure is a bare "Aborted at 02:33:44". On the deployed server on 2026-10-15, job 6 was listed exactly that way. Its `job-state-message`, "Page geometry does not match loaded 18 mm tape at 180 dpi", was available only through IPP Get-Job-Attributes. PAPPL 1.4.11 never renders that message on any page, and its log said only "Unable to start raster page."

## What Changes

- Show the printer's 20 most recent jobs on the landing page `/`, newest first, with the same columns as the printer page.
- Show each job's failure reason in the job status cell on `/`, `/ptouch/`, and `/ptouch/jobs`. Use its `job-state-message` when set, otherwise its `job-state-reasons` keywords.
- Set a specific `job-state-message` when a printer write fails and when a PNG cannot be decoded, is too large, or has non-square resolution. Today these paths only log or set reason bits.
- Implement the web changes as one pinned PAPPL patch, `patches/pappl-1.4.11-job-history.patch`, applied like the existing patches. The application cannot change PAPPL's stock pages through public APIs.
- Out of scope: submissions that PAPPL refuses before creating a job never become jobs, so they stay in the client's error response and the server log. The deployed log shows no such refusals.

## Capabilities

### New Capabilities

- `job-history`: What the server's web interface shows about recent jobs, and the guarantee that a failed job carries a visible reason.

### Modified Capabilities

None. The patch fits the existing `server-architecture` "Bounded dependency corrections" requirement: it is pinned, narrow, and documented in `patches/README.md`, and it changes no IPP behavior.

## Impact

Code: `patches/pappl-1.4.11-job-history.patch`, touching PAPPL's `printer-webif.c` only. `Dockerfile` and `Dockerfile.deploy` apply it. `src/driver.c` and `src/filter_png.c` set the missing messages. `patches/README.md` documents the patch.

Tests: the deploy-image smoke (`scripts/smoke-image.py`) checks that its existing input-failure job shows its reason on `/`. The existing driver and PNG suites assert the new messages. No printer is needed, and no Brother command bytes, geometry, or finishing change, so no hardware acceptance applies.
