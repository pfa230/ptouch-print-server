# Proposal

## Why

An unfinished chain leaves its last printed label attached. Clients need an explicit IPP action to release it without supplying or printing more visible content. The 2026-10-16 PT-P710BT Bluetooth test proved that one white raster row in chain mode releases the preceding label; standalone and zero-raster candidates did not.

## What Changes

- Expose project-local `Ptouch-Cut` (`0x5f01`) on PT-P710BT queues through `operations-supported`, with no submitted document or IPP job.
- Send the verified one-row blank continuation through the configured USB or Bluetooth transport, preserving the printer buffer and using the loaded tape width. This intentionally permits an internal blank page rather than promising a raster-free cutter command.
- Distinguish an open run holding user content from the open blank tail left after release. Repeated cuts do nothing until another printed label is held; subsequent printing continues without initialization.
- Reuse PAPPL device ownership, current status, checked transfers, and uncertain-output recovery. Explicit cuts bypass automatic idle-poll throttling.
- Supply a numeric `ipptool` example, exact protocol/IPP tests, and retained physical evidence. Ordinary finishings, defaults, media geometry, and PT-2730 printing remain unchanged.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `printer-device`: Model-scoped direct cut operation, blank continuation, device access, evidence, and Bluetooth/idle-poll behavior.
- `server-architecture`: An IPP action outside job submission and a distinction between held user content and the remaining blank continuation.

## Impact

`src/main.c` registers the operation callback; `src/driver.c` and `src/driver.h` own request handling and run state; `src/protocol.c` and `src/protocol.h` provide the small sequence using existing builders. A narrow PAPPL 1.4.11 advertisement correction is applied in both Dockerfiles. Extend existing protocol and adapter tests, README client instructions, and the hardware evidence in `tests/`.

Initial operation support is PT-P710BT only. PT-2730 does not advertise it and rejects it as unsupported. P710BT USB and Bluetooth use identical bytes; direct physical proof currently covers 12 mm tape over Bluetooth on the Pi at `ptouch-bt.iot.pfa.name`. This is not a claim of integrated IPP hardware acceptance or physical certification of other widths/transports. No further physical output or deployment is required to finish this change's implementation tasks.
