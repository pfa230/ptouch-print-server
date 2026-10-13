# Proposal

## Why

The PT-P710BT is replacing the owner's PT-2730, but the refactored server rejects its USB identity and emits only the older printer's setup commands. Direct USB tests already demonstrate a working P710BT raster path and usable 52-, 76-, and 120-dot print areas on 9, 12, and 18 mm tape, so support can be added without changing the client interface or narrowing those margins.

## What Changes

- Add an explicitly supported PT-P710BT USB path (`04f9:20af`), selected with `PTOUCH_MODEL=PT-P710BT`; retain PT-2730 as the default.
- Derive discovery, queue identity, USB opening, status checks, and the existing probe/smoke diagnostics from the selected supported model. Identify an attached but unselected supported printer and the required PTOUCH_MODEL selection without fallback. Keep one queue, exact model selection, same-model ambiguity rejection, and recovery after USB renumbering.
- Add model-specific P710BT page setup and dispatch the existing raster-mode and PackBits builders. Use normal 180 dpi, a 128-dot head, extended print information with the actual page row count, and explicit compression/status settings.
- Reuse the current canonical tape geometry and the refactored streaming batch lifecycle. Preserve four finishing policies, mandatory batch precut, healthy continuation, strict geometry rejection, and uncertain-output recovery.
- Add model-specific core, fake-USB, and IPP/socket regression coverage, with runtime socket smoke for both models in CI, followed by bounded P710BT hardware acceptance. Require byte-identical PT-2730 print regressions and a separate physical check only if its emitted print bytes or command policy change. Preserve the distinction between direct geometry observations and integrated server certification.
- Document model selection, protocol differences, available physical evidence, and unsupported models. Bluetooth, additional printer models, simultaneous queues, and 360 dpi mode are outside this change.

## Capabilities

### New Capabilities

None. USB model selection and its protocol belong to the existing printer-device capability.

### Modified Capabilities

- `printer-device`: Extend the supported-model contract to PT-P710BT, make model selection and identity consistent throughout the USB path, and specify the selected model's print protocol.
- `batch-printing`: Make hardware verification apply to the affected supported model and establish that P710BT receives the same finishing/continuation behavior as PT-2730.
- `server-architecture`: Update the canonical supported-hardware constraint from PT-2730-only to the two explicitly selected implemented model paths.

The `label-rendering` requirements remain authoritative and unchanged; the observed P710BT geometry matches the existing imageable-width table.

## Impact

Implementation will affect `src/tables.*`, `src/protocol.*`, `src/device_usb.*`, `src/main.c`, and `src/driver.c`. Tests will extend the existing table/protocol suites, fake USB selection and diagnostic checks, production callback captures, PNG geometry checks, and runtime IPP smoke, including `.github/workflows/deploy.yml`. The existing PT-2730-only cut probe needs no P710BT extension; all new physical acceptance uses the integrated server. README, the model-addition guide, and hardware acceptance instructions will distinguish certified paths from recognition metadata.

Reuse PAPPL, libusb, libpng, and the existing pinned dependency patches. No new service, parser, image buffer, client attribute, or dependency fork is needed. PT-2730 defaults, byte sequences, rendering, and finishing behavior must retain regression coverage.

This proposal authorizes planning only. Implementation requires a subsequent apply request; physical integration tests require a concrete bounded gate, and production deployment remains a separate action. Keep change work local until specification sync and archival are complete.
