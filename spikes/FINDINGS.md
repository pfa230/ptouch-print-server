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
