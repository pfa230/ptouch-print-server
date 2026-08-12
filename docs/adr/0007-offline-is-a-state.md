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
`ptouch://Brother/${PTOUCH_MODEL:-PT-2730}` and mark it `offline` immediately.

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
