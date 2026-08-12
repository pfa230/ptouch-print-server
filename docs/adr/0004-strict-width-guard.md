# 4. Strict width guard: reject size mismatches, never scale silently

**Status:** Accepted

## Context

A client can request a media width that does not match the tape physically loaded. Printing anyway
produces a mis-sized or clipped label, silently.

## Decision

Reject. If the job's cross-tape width does not match the loaded tape, fault the job before any raster
is written, with `document-unprintable-error` and a message naming both widths.

`PTOUCH_WIDTH_GUARD` selects the policy; only `strict` is implemented and it is the default. Any
other value logs that it is unimplemented and falls back to strict, so a mismatch is never silently
accepted.

## Consequences

- A wrong-tape job fails visibly instead of wasting tape on an unreadable label.
- The guard must compare like with like. This bit us twice: ADR-0005 changed what `cupsWidth` means,
  and the live-tape override in the same function was not updated, so it compared imageable px
  against full-page px and faulted correct jobs.
- Clients are expected to preflight with `media-ready`.

## Alternatives considered

- **`fit` mode** (scale or crop to the loaded tape). Deferred as issue #35 and later closed as not
  needed: for labels, a visible error beats an unreadable label.
