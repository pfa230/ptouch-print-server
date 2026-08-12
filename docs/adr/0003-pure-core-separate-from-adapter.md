# 3. Keep a pure C core separate from the PAPPL adapter

**Status:** Accepted

## Context

Physical printer tests consume tape and may require a power cycle. Keeping protocol
and planning logic independent of hardware makes most checks fast and repeatable.

## Decision

Split the code:

- **Pure core** - `protocol.c` (command byte builders), `tables.c` (device and tape tables),
  `raster.c` (line packing), `status.c` (status parsing), `cutter.c` (cut/chain planning). No PAPPL,
  no libusb, no I/O.
- **Adapter** - `driver.c`, `main.c`, `device_usb.c`, `filter_png.c`. PAPPL callbacks and transport.

The core builds and unit-tests on a laptop with CTest and no hardware.

## Consequences

- Protocol and planning logic is testable in milliseconds. The cutter plan in particular is exercised
  by table-driven tests covering cut modes, copies, and chaining.
- Bugs in planning are caught before they cost a hardware run. The cross-job chaining P0 (see
  ADR-0008) was caught in review partly because the logic was reviewable in one small pure function.
- A discipline is required: behaviour that belongs in the core must not drift into the adapter, or it
  loses its tests.
