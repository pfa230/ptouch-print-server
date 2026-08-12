# ptouch-print-server

A generic Brother P-touch IPP Everywhere print server, built on
[PAPPL](https://www.msweet.org/pappl/). It exposes a P-touch label printer as a
driverless IPP Everywhere / AirPrint device so any client can discover it and
print without a vendor driver.

The Brother raster protocol and device table are reused from
[hannesweisbach/ptouch-print](https://github.com/hannesweisbach/ptouch-print)
(GPL). See [LICENSE](LICENSE).

## Status

Working and deployed. Only the **Brother PT-2730** is hardware-certified; the
print path emits PT-2730 protocol for every device.

| Milestone | Scope | State |
|-----------|-------|-------|
| M0 | Hardware spike: confirm the raster bytes and the `usb://` hang on real hardware | Done |
| M1 | Pure C core: device table, protocol command builders, raster packing, status parse | Done |
| M2 | PAPPL adapter and read path: custom `ptouch://` libusb scheme, media/status callback | Done |
| M3 | Print path: 1:1 geometry, roll media, per-tape imageable width | Done |
| M4 | Cutter modes, chaining, and the strict width guard | Done |
| M5 | Packaging, GHCR publishing, container deployment | Done |

What it does today:

- Prints 1:1 at the tape's imageable width, with the label length taken from the
  submitted image rather than a fixed page.
- Advertises **nominal** tape sizes with real margins (`om_24x100mm`), so clients
  match the tape they actually loaded.
- Pre-cuts once, cuts labels apart, and **chains consecutive queued jobs** so a
  pool of jobs costs one leader rather than one per job.
- Rejects a job whose width does not match the loaded tape, before anything is
  sent to the printer.
- Reports `offline` when the printer is switched off instead of disappearing, and
  recovers on its own when it comes back - queued jobs then print.

Known gaps are tracked as issues: cross-tape centring is uncalibrated, and
`PTOUCH_CUT_MODE=end` cuts every label when pre-cut is enabled.

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

### Printing notes for clients

The printer advertises continuous-roll media (tape-width-fixed, length-variable
`roll_min`/`roll_max` range) and uses `print-scaling=none`, so a label prints
1:1. For a label to come out at its true size, a client must:

- send the image at the printer resolution, **180 dpi** (a PNG with no DPI
  metadata is read as 180 dpi), and
- match the loaded tape across its width. The printer reports the tape via
  `media-ready` at its **nominal** width, with the unprintable edge expressed as
  left/right margins (a 24 mm tape images ~18 mm, the 128-dot head); a job whose
  rendered width does not match the loaded tape is refused.

**Label length follows the PNG.** A submitted `image/png` prints at the physical
length of the image itself (its along-the-tape pixel count divided by its DPI),
so no `media-col` length is needed and there are no blank runs before and after
the content. Lengths below the 5 mm roll minimum are padded up to it; anything
over the 300 mm roll maximum is refused rather than cropped. JPEG is unaffected:
it still goes through PAPPL's built-in filter and prints on the fixed media
length.

## Configuration

The print path is configured entirely through environment variables (no config
file or volume). The driver reads them per job.

| Variable | Values | Meaning |
|----------|--------|---------|
| `PTOUCH_CUT_MODE` | `each` (default) / `end` / `none` | Cut policy for a batch: cut after every label, only after the last, or never |
| `PTOUCH_PRECUT` | `1` (default) / `0` | Feed and cut a leader before the first label of a batch |
| `PTOUCH_WIDTH_GUARD` | `strict` (default) | Refuse to print when the rendered width does not match the loaded tape. Only `strict` is implemented; any other value falls back to `strict` |

`PTOUCH_DEVICE_URI` overrides device discovery with an explicit URI (e.g.
`socket://127.0.0.1:9100`) for testing without a PT-2730; the printer registers
but only connects on an actual print.

## Deploy

The deploy image is published to GHCR as
`ghcr.io/pfa230/ptouch-print-server:edge` (the moving tag) and a per-commit
`sha-<commit>` tag for rollback. It is built and pushed by
`.github/workflows/deploy.yml` on every push to `main`; pull requests build and
run an IPP smoke test but do not push.

The image is a multi-stage build (`Dockerfile.deploy`): PAPPL 1.4.11 and the app
compile in a Debian build stage, and only the runtime libraries, `avahi-daemon`,
and `dbus` ship in the `debian-bookworm-slim` runtime stage. The entrypoint
starts a system dbus and Avahi, then runs `ptouch-app server` on port 8000. The
container has no persistent state, so no volume is needed.

```yaml
services:
  ptouch:
    image: ghcr.io/pfa230/ptouch-print-server:edge
    container_name: ptouch
    restart: unless-stopped
    hostname: ptouch
    ulimits:
      nofile:                            # dbus needs 65536; below that it dies,
        soft: 65536                      # avahi dies with it, and DNS-SD
        hard: 65536                      # advertises nothing at all
    environment:
      PTOUCH_CUT_MODE: each
      PTOUCH_PRECUT: "1"
      PTOUCH_WIDTH_GUARD: strict
    volumes:
      - /dev/bus/usb:/dev/bus/usb        # BIND MOUNT, not `devices:` - see below
    device_cgroup_rules:
      - 'c 189:* rwm'                    # allow libusb to claim USB char devices
    networks:
      some_shared_net:                   # a network the client also joins
        aliases:
          - ptouch.local                 # PAPPL accepts *.local as a Host header
```

**Use a bind mount for `/dev/bus/usb`, not compose `devices:`.** `devices:` maps
only the device nodes that exist when the container starts. Power-cycling the
printer creates a new node, which the container can then never see: it retries
the stale one indefinitely and only a container restart recovers it. A bind
mount propagates new nodes; the cgroup rule grants access to USB char devices.

**Addressing it.** PAPPL rejects any HTTP `Host` that is not an IP address,
`localhost`, or a `*.local` name, with `400 Bad Request`. So address it by IP, by
a `.local` name, or, behind a reverse proxy, rewrite the header:

```caddyfile
@ptouch host ptouch.example.com
reverse_proxy @ptouch http://ptouch:8000 {
  header_up Host ptouch.local:8000
}
```

IPP is HTTP over TCP, so it proxies normally; keep response buffering off so
raster streams.

**Discovery.** The container advertises DNS-SD over mDNS when it is on the LAN
segment itself. Behind a proxy, or on a bridge network, mDNS will not reach LAN
clients: publish unicast DNS-SD records (RFC 6763 `_ipp._tcp` PTR/SRV/TXT) in
your zone instead, pointing at wherever clients should connect.

The PT-2730 USB interface can be held by only one process. A dev/spike container
holding the device must be stopped before this container starts; both cannot own
the device at once.

## Design decisions

The reasoning behind the architecture, including several corrections, is recorded
as ADRs in [`docs/adr/`](docs/adr/README.md) - why this is its own PAPPL
application, why the USB scheme is custom, why media is advertised at nominal tape
width, why the app owns the PNG filter, how offline and job chaining work, and why
the licence is GPL-3.0.

## Licensing

This project is licensed under the **GNU General Public License v3.0 only**
(`GPL-3.0-only`). The full text is in [LICENSE](LICENSE).

It is GPL because it reuses Brother P-touch raster protocol and device-table code
from [hannesweisbach/ptouch-print](https://github.com/hannesweisbach/ptouch-print)
(GPLv3, by Dominic Radermacher). Credit and thanks to that upstream project.
