# Proposal

## Why

An unfinished chain leaves its last label attached, and clients need to release it without printing another label. Expose an explicit printer action over IPP that feeds only as needed and cuts the held end.

## What Changes

- Add a documented, implementation-defined `Ptouch-Cut` IPP printer operation, discoverable through `operations-supported`, with no document or job creation.
- Release a healthy held label or joined strip through a verified Brother cut-only sequence on PT-2730 and PT-P710BT, using the configured USB or Bluetooth transport.
- Reuse exclusive device ownership and the existing closed/open/uncertain run state: busy requests fail immediately, a closed run needs no action, and ambiguous cut results require the existing recovery procedure.
- Supply a numeric `ipptool` example and verify physical release without new raster, replay, or a blank helper label. Existing finishing presets and defaults remain as specified.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `printer-device`: Direct IPP cut action, its device-access and failure contract, physical evidence, and Bluetooth connection lifetime during an explicit cut.
- `server-architecture`: A bounded custom printer action alongside ordinary IPP printing, reusing device ownership and run state rather than introducing another job path.

## Impact

`src/main.c` registers PAPPL's operation callback; `src/driver.c` and `src/driver.h` handle printer state, device ownership, status, transfer, and response; `src/protocol.c` and `src/protocol.h` own any additional Brother command bytes. Extend existing protocol/driver/IPP tests, update README client instructions, and retain cut-only hardware evidence in `tests/`. A narrow pinned PAPPL correction and both Dockerfiles must publish the additional operation alongside the framework's standard operation list without duplicate attributes.

The IPP operation is a project extension, not a standard finishing or a generic print-dialog option. PAPPL remains pinned; its public operation callback and device APIs are the intended integration points. Exact standalone Brother bytes and completion semantics remain unverified; implementation must resolve that specific question on hardware before claiming the capability works. No production configuration change or physical output is authorized by this proposal.
