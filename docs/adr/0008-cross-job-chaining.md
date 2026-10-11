# 8. Respect client finishing and never reset an unfinished run

**Status:** Accepted; revised on 2026-10-11 to replace queue inference.

## Context

Different PNG labels arrive as separate IPP jobs. The old driver checked the
queue before the final label started, which missed followers arriving during
printing. It also cut an open run from an idle timer. Neither signal describes
what the client wants.

The driver already advertises trimming through IPP. PAPPL resolves both
`finishings` and `finishings-col`, including defaults, into its job options;
the driver previously ignored that value in favor of `PTOUCH_CUT_MODE`.

Earlier hardware investigation established that sending `ESC @` into an open
run could wedge the PT-2730 with a cassette-changed message. Suppressing that
reset remains necessary regardless of who decides whether to cut. The attempted
queue-drain sequence of FF followed by a standalone EJECT failed its hardware
probe and is not used by this decision.

## Decision

Use only the resolved job finishing choice to select the existing cutter plan:
`none` disables all cuts and leaves the run open; `trim` cuts copies individually
and finalizes the final label with chain-off/EJECT. An omitted choice honors the
advertised default, currently `none`. `PTOUCH_CUT_MODE` is ignored.

Remove queue scans, page-start replanning, the idle cutter timer, and automatic
terminal output during abort cleanup. Keep per-printer open-run and media state
to suppress `ESC @` between unfinished jobs, including after idle time and a
handle close/reopen. Retain rasterstart. Idle status polling must not reset an
open run; the existing USB presence check still detects disconnection.

Check actual write/flush transfers before recording terminal output, without
replaying a failed label or cut. At USB page boundaries, consume unsolicited
status until receiving-ready before sending another page. A print-completed
packet can precede that readiness indication; waiting for it alone leaves a
stale packet for the following page. This is device flow control, independent
of the finishing decision.

## Consequences

- Client intent controls the cut even if other work is queued or the queue is empty.
- Clients must request `trim` when they want cutting; the advertised none default is honored.
- No-cut jobs remain uncut until an explicit trim job or physical intervention.
- `none` disables every cut and can leave a strip of joined labels.
- PNG geometry, existing formats, width checks, copy limits, and scheduling stay unchanged.
- Process restart does not preserve unfinished-run state.
- Socket captures establish command behavior; PT-2730 output must separately verify cuts and leaders before claiming the issue fixed.
