# 7. Offline is a printer state, not an absent printer

**Status:** Accepted

## Context

The printer object used to be created only if a device was found at startup. With the printer switched
off there was no printer at all: IPP returned `client-error-not-found`, indistinguishable from a
broken server or a wrong URI, the web UI showed nothing, and recovery needed a container restart.

Meanwhile the driver already knew how to report `offline` - that code simply never ran, because there
was no printer object to attach reasons to.

## Decision

Always create the printer. When no device is present at startup, create it on
`ptouch://Brother/PT-2730` and mark it `offline` immediately. Validate configuration
first: PT-2730 is the only implemented model, and an unsupported value cannot select
a fallback device.

Jobs submitted while offline are accepted and queued; PAPPL pauses the queue and retries the device.
When the printer appears, the queue resumes with no restart.

## Consequences

- Clients can distinguish "printer switched off" from "server broken".
- A job queued while offline prints when the printer returns.
- **Media staleness became possible:** a printer created offline carries a placeholder 12 mm media
  default. The first queued job was built from it and then faulted. Fixed by refreshing the live tape
  in the PNG filter before the page geometry exists - `r_startjob` is too late, because the header
  already exists there and can only be faulted, not repaired. That refresh is a barrier: if the tape
  cannot be read it faults explicitly rather than building a wrong page.
- The device node must be a **bind mount**, not compose `devices:` - see ADR-0009.
- USB presence does not establish readiness. An unclaimable device, incomplete or
  malformed status, unsupported cassette, or printer error leaves the queue
  unavailable. Idle polling and job-owned refresh share the same publication path.
- Each queue owns its run and media-confirmation state through PAPPL driver
  extension data. Status queries do not initialize the printer or finalize an
  unfinished tape run.
- An interrupted raster/print command can leave output uncertain. Block later work
  and report recovery required, even if a later status reply is clean or USB
  reopens. The conservative recovery sequence is to power-cycle the printer,
  then restart the server and inspect failed jobs before explicit resubmission.
  Do not automatically reset, cut, replay, or append to the uncertain buffer.
  Server restart alone does not establish a safe physical tape state, and the
  container's process-local queue/state is not promised to survive it. The authorized
  integrated PT-2730 acceptance verified blocking after truncated raster and
  successful power-cycle/server-restart recovery without replay or ghost output.
