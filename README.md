# ptouch-print-server

A generic Brother P-touch IPP Everywhere print server, built on
[PAPPL](https://www.msweet.org/pappl/). It exposes a P-touch label printer as a
driverless IPP Everywhere / AirPrint device so any client can discover it and
print without a vendor driver.

The Brother raster protocol and device table are reused from
[hannesweisbach/ptouch-print](https://github.com/hannesweisbach/ptouch-print)
(GPL). See [LICENSE](LICENSE).

## Status

The project is built in milestones. Only the **Brother PT-2730** is
hardware-certified; the print path currently emits PT-2730 protocol for every
device.

| Milestone | Scope | State |
|-----------|-------|-------|
| M0 | Hardware spike: confirm the raster bytes and the `usb://` hang on real hardware | Done |
| M1 | Pure C core: device table, protocol command builders, raster packing, status parse | Done |
| M2 | PAPPL adapter and read path: custom `ptouch://` libusb scheme, media/status callback | Done |
| M3 | Print path: raster callbacks that print and cut on the PT-2730 | In progress |
| M4 | Cutter modes and width guard (`PTOUCH_*` configuration) | Pending |
| M5 | Packaging | Pending |

M3 raster callbacks print and cut a label on the PT-2730 today
(init / rasterstart / sendraster per line / eject). The exact print geometry is
still being finished: no-scaling fit, variable label length, and head-limiting
behaviour are not yet final.

## Architecture

Three layers:

1. **Pure C core** (`src/tables.c`, `src/protocol.c`, `src/raster.c`,
   `src/status.c`, `src/cutter.c`). Libc only, no hardware, no PAPPL. All Brother
   byte sequences live in `protocol.c`; raster packing centers a scanline into
   the 16-byte (128-dot) raster line in `raster.c`; the device table and per-model
   flags live in `tables.c`. This layer builds and unit-tests on any machine.

2. **PAPPL driver** (`src/driver.c`). Registers the IPP printer, fills the driver
   data (media, resolution, finishings), reads loaded-tape status, and drives the
   raster print callbacks (`rstartjob` / `rwriteline` / `rendjob`).

3. **Custom `ptouch://` libusb scheme** (`src/device_usb.c`). PAPPL's built-in
   `usb://` scheme issues an IEEE-1284 device-ID probe that hangs on the PT-2730
   (confirmed in M0). This project registers its own `ptouch://`
   `PAPPL_DEVTYPE_CUSTOM_LOCAL` scheme over raw libusb with bounded timeouts, and
   never auto-adds the built-in `usb://` device.

## Supported devices

`src/tables.c` recognizes the P-touch USB ids below (transcribed from upstream
`ptdevs[]`). **Only the PT-2730 is print-supported and hardware-certified.** Every
other entry is recognized and discoverable, but is **not print-supported**: the
raster path emits PT-2730 protocol (uncompressed, 128-dot head) regardless of the
matched model, and per-model flag dispatch (PackBits, P700 init) does not exist
yet. All known models are 180 dpi / 128 px max.

| Model | USB id | flags | Print-supported |
|-------|--------|-------|-----------------|
| PT-2420PC | 04f9:2007 | `RASTER_PACKBITS` | recognized only |
| PT-1230PC | 04f9:202c | `NONE` | recognized only |
| PT-2430PC | 04f9:202d | `NONE` | recognized only |
| PT-1230PC (PLite) | 04f9:2030 | `PLITE` | recognized only |
| PT-2430PC (PLite) | 04f9:2031 | `PLITE` | recognized only |
| **PT-2730** | **04f9:2041** | `NONE` | **certified** |
| PT-E500 | 04f9:205f | `RASTER_PACKBITS` | recognized only |
| PT-H500 | 04f9:205e | `RASTER_PACKBITS \| P700_INIT` | recognized only |
| PT-P700 | 04f9:2061 | `RASTER_PACKBITS \| P700_INIT` | recognized only |
| PT-P700 (PLite) | 04f9:2064 | `PLITE` | recognized only |
| PT-P750W | 04f9:2062 | `RASTER_PACKBITS \| P700_INIT` | recognized only |
| PT-P750W (PLite) | 04f9:2065 | `PLITE` | recognized only |
| PT-D450 | 04f9:2073 | `RASTER_PACKBITS` | recognized only |
| PT-D600 | 04f9:2074 | `RASTER_PACKBITS` | recognized only |

To add a model, see [docs/ADDING-A-MODEL.md](docs/ADDING-A-MODEL.md).

## Build and run

The pure C core builds and unit-tests anywhere with CMake and a C11 compiler. No
hardware or PAPPL needed:

```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

This builds `ptcore` and runs the five core test suites (tables, protocol,
raster, status, cutter).

The full `ptouch-app` executable builds only where PAPPL and libusb-1.0 are
present (`pkg-config` finds `pappl` and `libusb-1.0`). On a machine without them,
CMake skips the app target and still builds the core and tests. The app is built
and exercised against PT-2730 hardware in a container; see `Dockerfile`.

## Configuration

> **Planned (M4): not yet implemented.** The environment variables below are the
> intended configuration surface for the cutter and width guard. They are **not**
> wired up yet. Setting them today has no effect.

| Variable | Planned meaning |
|----------|-----------------|
| `PTOUCH_CUT_MODE` | Cut policy for a batch: each / end / none |
| `PTOUCH_WIDTH_GUARD` | Refuse to print when the rendered width would overrun the loaded tape |

The cutter-plan logic (`src/cutter.c`, `pt_plan_batch`) exists as inert core
infrastructure, but the driver does not yet dispatch on it.

## Licensing

This project is licensed under the **GNU General Public License v3.0 only**
(`GPL-3.0-only`). The full text is in [LICENSE](LICENSE).

It is GPL because it reuses Brother P-touch raster protocol and device-table code
from [hannesweisbach/ptouch-print](https://github.com/hannesweisbach/ptouch-print)
(GPLv3, by Dominic Radermacher). Credit and thanks to that upstream project.
