# Design

## Context

The web interface is PAPPL 1.4.11's stock one, in multi-queue mode (`src/main.c` passes `PAPPL_SOPTIONS_MULTI_QUEUE | PAPPL_SOPTIONS_WEB_INTERFACE`). In that mode `/` is `_papplSystemWebHome()`. For each printer it calls `_papplPrinterWebIteratorCallback()`, which prints a status card with `papplPrinterGetNumberOfActiveJobs()` and no job list. Jobs are listed only on `/ptouch/` and `/ptouch/jobs`, by `job_cb()` in `printer-webif.c`: 20 rows per page, newest first, up to 100 completed jobs retained. `job_cb()` renders state and time only, never `job->message`. Upstream master is the same.

The driver sets messages on its geometry, finishing, media, and readiness failures. It sets none when `pt_dev_write()` fails. `src/filter_png.c` only logs, or sets reason bits, for libpng decode errors, oversized images, and non-square DPI.

## Goals / Non-Goals

**Goals:**

- The landing page shows recent jobs and why failed ones failed.

**Non-Goals:**

- Recording submissions refused before job creation.
- Persisting history across restarts.
- Messages for allocation and file-open failures, which PAPPL's reason keywords already identify.

## Decisions

### D1: Patch PAPPL's pages instead of replacing them

The application cannot add to `/` or to `job_cb()` through public APIs, and `papplSystemAddResourceCallback()` cannot replace an existing path. The patch touches only `pappl/printer-webif.c` and is applied after the raster and finishing patches with the same `--fuzz=0` command.

Rejected alternatives:

- Redirecting `/` to `/ptouch/`. Every page's nav icon links to `/`, and `/` holds the only link to the system Configuration page, so that page would become unreachable.
- Dropping `PAPPL_SOPTIONS_MULTI_QUEUE`. It moves the IPP resource from `/ipp/print/ptouch` to `/ipp/print`, which breaks existing CUPS queues and DNS-SD records.

### D2: Job list on the landing page

In `_papplPrinterWebIteratorCallback()`, in the existing branch for `/` in multi-queue mode, render a job table after the status card with `papplPrinterIterateAllJobs(printer, job_cb, client, 1, 20)`, or "No jobs in history." when there are none. All three pages then share `job_cb()`, and its buttons already post to `<uriname>/jobs`. Older jobs remain reachable through the card's title link to the printer page and its pager.

### D3: Reason in the existing status cell

In `job_cb()`, append `: <message>` to the state/time text when `papplJobGetMessage()` is set. For an aborted or canceled job without a message, append its reason keywords via `_papplJobReasonString()`, using the loop `job-ipp.c` already has. PAPPL itself never sets job messages but always adds `aborted-by-system` or a cancel reason, so a terminal failure never renders bare. `papplClientHTMLPrintf("%s")` escapes the text.

### D4: Messages where the application fails

- `pt_dev_write()` takes the job and, on a short or unflushed write, sets "Write to printer failed; output uncertain: power-cycle printer, then restart server". Every write happens while the run state is `PT_RUN_UNCERTAIN`: `r_startpage` sets it before page setup, and only a successful `pt_print_pending()` clears it. So the recovery instruction always applies.
- `png_error_func()` sets "PNG decode failed: <libpng message>".
- The oversize and non-square checks in `filter_png.c` set "PNG is too large to print (<w>x<h>)" and "PNG has non-square resolution <x>x<y> dpi".

## Risks / Trade-offs

- [The patch is permanent; upstream has no equivalent] → Document in `patches/README.md` its purpose and its removal criterion: an upstream release that lists jobs on the multi-queue home page and renders `job-state-message`.
