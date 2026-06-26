# M0 Spike Findings

Running log; each probe appends. Host: Unraid `dockerhost` (<host-ip>), PT-2730 `04f9:2041` on
`/dev/bus/usb/<bus>/<dev>`. Spike image: `ptouch-spike` (Debian bookworm + PAPPL 1.4.11).

## Build environment (Task 1)

- **PAPPL master requires libcups ≥ 2.5**, which bookworm lacks (ships 2.4). Pinned PAPPL to
  **v1.4.11** (the stable 1.4 line, CUPS 2.x). Aligns with the "PAPPL v1.4" our design targets.
- PAPPL also requires **TLS** (`libssl-dev`) and wants **Avahi** (`libavahi-client-dev`) for DNS-SD.
- **`ptouch-print` builds with autotools**, not cmake (our plan said cmake — wrong). Needs
  `autoconf automake libtool gettext autopoint`; build = `sh autogen.sh && ./configure && make`.

## Probe G (partial — basic passthrough)

- `docker run --device=/dev/bus/usb/<bus>/<dev> …` → `lsusb` shows `04f9:2041` inside the container,
  and `ptouch-print --info` reads device status through it. Basic passthrough **works**.
- Still TODO for G: the production compose form (`/dev/bus/usb` bind + `device_cgroup_rules`
  `c 189:* rmw` + `lp` GID) and **replug recovery**.

## Probe A (partial — oracle build + status)

- `ptouch-print` built and ran in-container. `--info` output:
  - `PT-2730 found on USB bus 3, device 4`
  - **loaded tape = 12 mm → max print width 76 px**, media type 01, `error = 0000`.
- Confirms: passthrough + USB status read + the printer all work in the deployment shape.
- **Baseline printed:** `arrow.png` printed OK (`PT-2730 found`, no error). The physical label is
  **retained as the known-good orientation baseline** for a side-by-side comparison against the
  PAPPL output in Probe B (more reliable than describing absolute orientation).
- Source-derived expected mapping (from `print_img` in ptouch-print, to confirm via the side-by-side):
  image **x → tape length** (column x=0 prints first), image **y → cross-tape width** read
  bottom-up (`SY-1-i`), content centered on the 76px head. Probe B must reproduce this.

## Loaded tape

- **12 mm (76 px imageable)** at time of spike. Fixtures sized to this.

## Probe B (raster reality) — ANSWERED via in-process socket-device stub

Setup notes (hard-won): PAPPL USB auto-discovery hangs on the PT-2730, `file://` device
unsupported, avahi-daemon required, and the `add` CLI rejects names oddly — so the stub creates
the printer **in-process** (`papplPrinterCreate`, name `spike`, `socket://127.0.0.1:9100` → a
python sink). Required driver_data to avoid rejection/segfault: `make_and_model`, non-zero `ppm`,
and the `source[]`/`type[]` arrays (not just `num_source`/`num_type`, else `validate_ready`
strcmp's NULL → SIGSEGV).

Findings (12mm/76px tape loaded; force_raster_type=BLACK_1):
- **Orientation: rwriteline = one CROSS-TAPE scanline.** `rstartpage` reports `cupsWidth=85`
  (≈12mm across at 180dpi), `cupsHeight=708` (=100mm media length), `bpp=1 bpc=1 bpl=11`. So each
  `rwriteline(y,line)` is one Brother raster line; `y` advances along the tape length. **No
  transpose / full-page buffer needed** — stream a raster command per scanline.
- **Bit depth: PAPPL dithers to 1-bit when BLACK_1 is forced.** Gradient fixture → `0f 0f 0f…`
  dither. Driver can consume the delivered 1-bit lines directly (the core's own 127/128 threshold
  is NOT needed if we force BLACK_1).
- **Scaling is media-driven (the spec's worry, confirmed).** A 300×64 source was scaled to fill
  the 85×708 media; `print-scaling-default=auto`. The real driver MUST set media to the intended
  label size (and/or `print-scaling`) or PAPPL rescales. The 100mm length was just my media_col
  `size_length=10000`.
- **Width reconciliation needed:** PAPPL computes 85px for 12mm media (12mm@180dpi), but the head
  images 76px. The real driver must set the media width to the printable 76px (or model margins).
- Get-Printer-Attributes confirms: `document-format-supported` incl. image/png, `finishings-
  supported = none,trim`, `pwg-raster-document-type-supported = black_1,black_8`, `media-ready =
  om_12mm-tape…`, `printer-state = idle`.

## Probe C (status) — partial
- `status_cb` runs and does `papplPrinterOpenDevice` + write `1b6953` + `papplDeviceRead`. Over the
  socket sink it returns `-1` (no status source). **`papplDeviceRead` is unbounded** (confirmed —
  it's why `devices`/status hang on USB). Real PT-2730 status + a bounded-read strategy still need
  the USB device (deferred; `keep_device_open` is not a 1.4.11 field).

## Probe F (job model) — partial
- `options->copies`, `options->finishings`, `options->num_pages` are visible in the raster
  callbacks (logged per job). Multi-document / `last-document` test via Create-Job still pending.

## Probe F (IPP job model) — ANSWERED (changes the M4 design)

- **PAPPL rejects MULTI-DOCUMENT jobs.** `Create-Job` + `Send-Document(last-document=false)` OK,
  but the 2nd `Send-Document` → `server-error-multiple-document-jobs-not-supported`. PAPPL printer
  apps are **single-document-per-job**. **This invalidates the spec/plan batch model (one IPP job
  + N×Send-Document + last-document).** Both the original design and Codex's "fix" assumed it.
- **`copies` works at the driver level.** `copies=2` on a single document → the driver gets **N
  `rstartpage`/`rendpage` renders** (one per copy), each with `options->copies=N`, `num_pages=1`.
  So **N identical labels = one job, copies=N → cut after each render**.
- **N different labels** cannot share a job (PNG is single-page; multi-document unsupported) → must
  be **N separate IPP jobs**, or a single multi-page raster/PDF (outside PNG-only scope). The
  "cut-between-different-labels, chained, one job" model is **not achievable on PAPPL with PNG**.
- Visibility: `options->copies`, `options->finishings`, `options->num_pages` are all available in
  the raster callbacks. (Sending `finishings=trim` needs the correct IPP enum; PAPPL advertises
  `finishings-supported = none,trim`.)
- **M4 impact (revisit before building):** the cutter/batch design must change — `copies` for
  identical labels (cut per render), separate jobs for different labels; reconcile "cut after each"
  + chaining with per-copy vs per-job boundaries.

## Auto-power-off — CORRECTED (false alarm)

The PT-2730 manual lists a 5-min auto-off, which spawned a long detour (keep-alive / power-button
hardware). Empirically that's WRONG while USB-tethered: a passive idle monitor (lsusb only, no
device I/O) showed the printer **ON-BUS continuously past 10 minutes of pure idle**, and it had
already sat ~37 min idle earlier and stayed on. So **it does NOT auto-off at 5 min when connected
to a host.** (An earlier ~2h idle did find it off, so a much longer idle-off may exist; TBD, low
priority.) NOTE: my first "ping" test falsely reported GONE because `ptouch-print --info` prints to
stderr and the script did `2>/dev/null`. **Decision: no keep-alive, no power-button hardware, no
embedded-Pi-for-power needed for the tethered architecture.**

## Probe H (PAPPL built-in usb:// viability) — ANSWERED: NO. + Probe C resolved

USB descriptors: Brother / PT-2730 / serial A4Z350200 (usb://Brother/PT-2730?serial=A4Z350200).

- PAPPL's `stub devices` (USB enumeration / IEEE-1284 device-ID probe) **HANGS** on the PT-2730.
  The hang is in a libusb transfer with **timeout 0**, so it is **not killable** by `timeout`
  SIGTERM (and even SIGKILL didn't return within the window). The hung process **holds the USB
  interface claimed**, which **wedges the device for other clients** — `ptouch-print --info`
  was blocked until the hung PID was killed, then recovered immediately.
- **Per Codex**, opening a `usb://` URI runs the same device-ID probe before URI construction, so a
  manually-built `usb://Brother/PT-2730?serial=…` does NOT bypass the hang.
- **CONCLUSION (drives M2): PAPPL's built-in `usb://` device is UNUSABLE for the PT-2730.** The real
  driver must drive USB via **raw libusb with bounded timeouts** — either a **custom PAPPL device
  scheme** (`papplDeviceAddScheme` providing open/read/write/close) or USB I/O outside PAPPL's
  device layer. `ptouch-print`'s libusb path works (oracle), so this is proven viable.
- **Probe C resolved by the same finding:** `papplDeviceRead` is unbounded/hangs; the bounded
  status read must use raw libusb with a real timeout in the custom device path.
