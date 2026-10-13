# ptouch-print-server

A generic Brother P-touch IPP Everywhere print server, built on
[PAPPL](https://www.msweet.org/pappl/). It exposes a P-touch label printer as a
driverless IPP Everywhere / AirPrint device so any client can discover it and
print without a vendor driver.

The Brother raster protocol and device table are reused from
[hannesweisbach/ptouch-print](https://github.com/hannesweisbach/ptouch-print)
(GPL). See [LICENSE](LICENSE).

## Capabilities

Only the **Brother PT-2730** is print-supported and hardware-certified;
other recognized models do not receive print output.

- Prints 1:1 within the tape's imageable width, honoring submitted page geometry;
  PNG length follows its image when a concrete media length is omitted.
- Advertises **nominal** tape sizes with real margins (`om_24x100mm`), so clients
  match the tape they actually loaded.
- Uses standard IPP **finishings** for cuts between labels and at batch end,
  including an advertised chain-only collection template. Queue contents and
  idle time do not choose whether to cut.
- Rejects a job whose width does not match the loaded tape, before anything is
  sent as page setup or raster to the printer.
- Reports `offline` when the printer is switched off instead of disappearing, and
  recovers on its own when it comes back - queued jobs then print.

Known gaps are tracked as issues; cross-tape centring is uncalibrated.

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
   `usb://` scheme issues an IEEE-1284 device-ID probe that hangs on the PT-2730.
   This project registers its own `ptouch://`
   `PAPPL_DEVTYPE_CUSTOM_LOCAL` scheme over raw libusb with bounded timeouts, and
   never auto-adds the built-in `usb://` device.

## Supported devices

`src/tables.c` recognizes the P-touch USB ids below (transcribed from upstream
`ptdevs[]`). **Only the PT-2730 is print-supported and hardware-certified.** Every
other entry remains recognition metadata and is **not print-supported**: discovery
reports it in diagnostics without offering a printable device. Per-model flag
dispatch (PackBits, P700 init) does not exist yet. All known models are 180 dpi /
128 px max. `PTOUCH_MODEL` defaults to `PT-2730`; unknown or unsupported values
fail before discovery. Multiple matching PT-2730 units block printing as ambiguous,
while a single unit can be unplugged and reattached without replacing its queue.

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

The hardware-free core requires CMake, a C11 compiler, and Python 3 for the
capture suite. Select core-only mode explicitly; PAPPL and USB are not needed:

```bash
cmake -S . -B build-core -DPTOUCH_CORE_ONLY=ON
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

This builds `ptcore` and the command probe, then runs the five core suites
(tables, protocol, raster, status, cutter) and the Python capture suite.

The default full build requires `pkg-config` packages `pappl`, `libusb-1.0`,
and `libpng`, in addition to the core test tools. Missing dependencies fail
configuration. It builds `ptouch-app` and the adapter tests as well as the core:

```bash
cmake -S . -B build-app -DPTOUCH_CORE_ONLY=OFF
cmake --build build-app
ctest --test-dir build-app --output-on-failure
```

`Dockerfile` supplies the development dependencies; `Dockerfile.deploy` builds
all targets and runs CTest before installing the app into the runtime image.
These tests use no printer; physical behavior requires separate PT-2730 checks.

### Printing notes for clients

Media dimensions describe the physical tape, and `media-ready` publishes the
loaded nominal width with its physical left/right margins. The supported tape
widths are 6, 9, 12, 18, and 24 mm. A page whose width does not match the loaded
tape is refused before raster output. The default `print-scaling=none` preserves
physical size; explicit standard scaling requests use PAPPL's placement policy.
The server does not silently choose fit or crop to rescue a mismatched design.

For PNG at `print-scaling=none`, use the printer's 180 dpi resolution. PNGs
without resolution metadata are interpreted as 180 dpi; square DPI metadata
sets the physical size, while non-square DPI is rejected. Cross-tape content
must fit the imageable width (32, 52, 76, 120, or 128 printer dots respectively).
A full nominal-width canvas is also accepted when its non-imageable edge
margins are entirely white after transparency is composited. Nonblank physical
margins, oversized content, or content longer than an explicitly selected page
are rejected before page raster rather than silently clipped.

An explicit named `media` size, `media-col.media-size-name`, or
`media-col.media-size.y-dimension` determines
PNG page length. For example, a 180-pixel image at 180 dpi on explicitly selected
50 mm media prints a 50 mm page, with centered content. If concrete length is
omitted, PNG length follows its oriented along-tape pixel dimension and DPI.
Source-only or margin-only `media-col` does not suppress that convenience.
Automatic lengths below 5 mm are padded to 5 mm; lengths above 300 mm are refused.
PNG's automatic orientation uses portrait for tall/square images and
reverse-landscape (90 degrees clockwise) for wide images. Explicit standard
`orientation-requested` overrides it.

JPEG uses PAPPL's standard image placement and selected/default media length,
without PNG's automatic-length convenience or source-content preflight. Standard
PWG raster and URF retain each submitted page's geometry. Explicit page dimensions
have the same physical meaning across formats; contradictory raster/job dimensions
fail visibly. PNG is advertised once as `image/png`, without a private raster MIME.

Custom lengths also use standard `media-col` dimensions. The roll declarations
accept width rounding of ±0.01 mm around each nominal tape width for CUPS custom
sizes; this does not permit selecting another cassette or enlarging its physical
printable window. The submitted length and explicit margins remain authoritative.

CUPS 2.4's generated PPD has one custom-size margin for all tapes. It chooses the
largest advertised margins, so `media=Custom.12x40mm` leaves only about 42 printable
dots on 12 mm tape. A fitting design prints at 1:1; a 76-dot design is rejected.
Named media presets carry the correct per-tape margins. For arbitrary lengths
with the full imageable width, submit standard `media-col` dimensions and the
published tape margins. The server never replaces explicit margins or shrinks
content to conceal this CUPS limitation.

### Cutting

One IPP job is one batch: its PNG copies or actual raster labels. Select cutting
through standard `finishings` or `finishings-col`:

| Selection | IPP selection | Behavior |
|-----------|---------------|----------|
| Chain and continue | `finishings-col` template `ptouch-chain` | Cut between labels; hold the last end |
| Chain and finish | `finishings=60`, or compatibility alias `11` | Cut between labels and cut the final end |
| No label cuts | `finishings=3` | Join labels; hold the batch end |
| Final cut only | `finishings=63` | Join labels; cut only at batch end |

**Precut is mandatory once per nonempty batch**, including none and final-only.
A fresh run discards its initial leader. A healthy continuation releases the
preceding held end without a new discarded leader. Consecutive none batches
therefore become separate uncut strips. An unfinished run can remain open across
idle gaps; canceled/rejected followers do not silently finish it.

Omission honors the current printer default, initially `trim` (11), so ordinary
jobs cut their labels free. Request none explicitly to leave a batch open.
With a CUPS queue named `ptouch`:

```sh
lp -d ptouch -n 3 -o finishings=60 labels.png  # three separate labels, final cut
lp -d ptouch -n 3 -o finishings=63 strip.png   # one joined strip, final cut
lp -d ptouch -o finishings=3 unfinished.png   # hold its end for continuation
```

Registered collection templates `trim`, `trim-after-pages`, and `trim-after-job`
select the corresponding enum policies; collection `no-value` selects none.
`ptouch-chain` is a printer-defined name carried through the standard collection,
with the meaning shown above. Generic dialogs may expose only registered enum
choices; a client must support collection/template submission to select chain-only.
Do not submit both finishing representations in the same job. Enum 60 plus 63
means chain-and-finish with one terminal cut; enum none alongside other values
is ignored according to IPP.

Printer defaults accept the three policies with registered enum equivalents;
enum and collection defaults stay synchronized. Chain-only is explicitly per-job,
because there is no lossless registered enum for its printer-defined meaning.
Existing CUPS queues can retain a cached or explicit none default: refresh their
standard queue settings to use trim when adopting this change, for example
`lpoptions -p <queue> -o finishings=11`. This was verified with an ordinary
CUPS driverless queue; its generated PPD can otherwise submit explicit none
even to a server advertising trim by default. Explicit none remains honored. Retire old `finishings-col` template `none` submissions in favor
of enum 3 or standard collection no-value.

The server uses each complete raster label and successful end-of-input, so an
unknown total count requires no custom metadata. PNG copies support 1 through
999; PWG raster and URF support one copy with multiple labels in the document.
Run state lives in memory. Restart during an unfinished run is unsupported;
finish it deliberately first. Interrupted output requires printer power-cycle
followed by server restart, as described below.

## Configuration

Cutting uses the job's standard finishing choice. `PTOUCH_PRECUT`,
`PTOUCH_CUT_MODE`, and `PTOUCH_WIDTH_GUARD` are retired and ignored. Mandatory
batch precut and strict geometry rejection are intrinsic; no replacement server
knob is needed. `PTOUCH_MODEL` defaults to the supported PT-2730.

`PTOUCH_DEVICE_URI` overrides device discovery with an explicit URI (e.g.
`socket://127.0.0.1:9100`) for testing without a PT-2730; the printer registers
but only connects on an actual print.

## Deploy

The deploy image is published to GHCR as
`ghcr.io/pfa230/ptouch-print-server:edge` (the moving tag) and a per-commit
`sha-<commit>` tag for rollback. It is built and pushed by
`.github/workflows/deploy.yml` on every push to `main`. Both pull requests and
main run the core, driver, and capture suites and submit real IPP jobs to a socket
sink in the deploy-image smoke before publication. Pull requests do not publish.

The image is a multi-stage build (`Dockerfile.deploy`): PAPPL 1.4.11 and the app
compile in a Debian build stage, and only the runtime libraries, `avahi-daemon`,
and `dbus` ship in the `debian-bookworm-slim` runtime stage. The entrypoint
starts a system dbus and Avahi, then runs `ptouch-app server` on port 8000. The
application writes normal startup, job, and error diagnostics directly to stderr,
visible through `docker logs`; no debug logfile or separate tail process is
needed. dbus and Avahi startup failures stop the entrypoint and remain visible.
The container has no persistent state, so no volume is needed.

If a job reports uncertain output, inspect `docker logs ptouch` and
`docker exec ptouch ptouch-app jobs -d ptouch` before restarting; job states and
standard IPP Get-Job-Attributes identify aborted work. Power-cycle the printer,
then run `docker restart ptouch` and check
`docker exec ptouch ptouch-app status -d ptouch`. Explicitly resubmit the desired
failed job after recovery. A clean status or USB replug alone does not clear
uncertainty; server restart alone does not clear the printer's buffered raster.
In-memory job history and an unfinished tape run are not promised to survive
restart, and the server does not automatically replay ambiguous output.

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

## Specifications

The canonical [server architecture](openspec/specs/server-architecture/spec.md)
specification defines the PAPPL application, core/adapter boundary, USB access,
physical media, PNG route, batch lifecycle, deployment, and licensing decisions.
Detailed contracts live in [label rendering](openspec/specs/label-rendering/spec.md),
[batch printing](openspec/specs/batch-printing/spec.md), and
[printer devices](openspec/specs/printer-device/spec.md).

## OpenSpec workflow

[OpenSpec](https://openspec.dev/docs/setup) tracks proposed changes in
`openspec/changes/` and accepted specifications in `openspec/specs/`. Project
constraints live in `openspec/config.yaml`; accepted specifications are the
canonical record of the server's contracts and architecture.

The repository includes six Codex skills in `.agents/skills/`. Start a new Codex
session after setup to load them:

- `$openspec-explore` to investigate an idea.
- `$openspec-propose "describe the change"` to draft its proposal, design,
  specification updates, and tasks.
- `$openspec-update-change` to revise the plan.
- `$openspec-apply-change` to implement the planned tasks.
- `$openspec-sync-specs` to sync specification updates.
- `$openspec-archive-change` to archive a completed change.

The [OpenSpec CLI](https://openspec.dev/docs/cli) must be installed. Run
`openspec list` to see active changes, `openspec validate --all --strict` to check
specifications and changes, and `openspec update` to refresh the generated skills.

## Licensing

This project is licensed under the **GNU General Public License v3.0 only**
(`GPL-3.0-only`). The full text is in [LICENSE](LICENSE).

It is GPL because it reuses Brother P-touch raster protocol and device-table code
from [hannesweisbach/ptouch-print](https://github.com/hannesweisbach/ptouch-print)
(GPLv3, by Dominic Radermacher). Credit and thanks to that upstream project.
