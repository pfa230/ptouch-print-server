# Design

## Context

See [proposal.md](proposal.md) for motivation and the capability deltas for the behavioral contract.

[verified] All printer I/O uses the `ptouch://` libusb scheme in `src/device_usb.c`. `src/driver.c` gates every live status read on `pt_usb_is_uri()` and reads through `pt_usb_read_status()`; `status_cb` (`src/driver.c:612`) also counts USB devices before opening one. A URI that is not a supported USB URI (the socket test transport) skips live status entirely.

[verified] PAPPL 1.4.11 invokes `status_cb` from `papplPrinterGetReasons()` (`pappl/printer-accessors.c:528`) whenever the printer is idle and the last call is more than one second old. Idle polling is therefore driven by client queries (CUPS, web UI, IPP monitors), not by a timer. When a job cannot open its device, PAPPL stops the queue and retries the open every 5 seconds (`pappl/job-process.c:1192-1213`) until it succeeds or the job is canceled. That retry is independent of `status_cb`.

[verified] The PT-P710BT accepts the same raster command stream over Bluetooth Classic RFCOMM, channel 1, and replies to the status request with the same 32-byte packet. [robby-cornelissen/pt-p710bt-label-maker](https://github.com/robby-cornelissen/pt-p710bt-label-maker) sends invalidate, initialize, status request, mode settings, raster and print over one RFCOMM socket and parses the reply with the same offsets as `src/status.c`.

[verified] Mainline Linux refuses Bluetooth sockets outside the initial network namespace (`net/bluetooth/af_bluetooth.c`: `if (net != &init_net) return -EAFNOSUPPORT;`). RFCOMM socket creation requires no capability (`net/bluetooth/rfcomm/sock.c` has no `capable()` check).

## Goals / Non-Goals

**Goals:**

- One queue, one configured transport, chosen at startup, with no runtime fallback between transports.
- Reuse the existing driver lifecycle, status parsing, finishing and recovery code unchanged above the device layer.
- One polling setting that means the same thing on every transport.
- The same image tags run on the existing amd64 host and on a Pi Zero 2 W.

**Non-Goals:**

- Bluetooth discovery or pairing inside the server. Pairing is a one-time host step with `bluetoothctl`.
- Bluetooth for PT-2730 (no radio) or for recognized-only models.
- Bluetooth Low Energy, Wi-Fi, or printing from outside Bluetooth range.
- Battery reporting or auto-power-off control.

## Decisions

### Bluetooth is a second PAPPL device scheme

Add `src/device_bt.c` registering a `ptouch-bt://` scheme (`ptouch-bt://Brother/PT-P710BT/<address>`) with open, close, read, write and status callbacks over a blocking `AF_BLUETOOTH`/`BTPROTO_RFCOMM` socket on channel 1. Open performs one bounded connect; close shuts the socket. PAPPL already opens the device per job and per status read, so per-job connection needs no extra state.

The driver's USB-only gates become transport-neutral: one function says whether a URI is a live printer transport (USB or Bluetooth), and one bounded status read dispatches by scheme. The USB presence count in `status_cb` stays a USB step; Bluetooth has no enumeration, so its reachability check is the open itself. The socket test transport keeps skipping live status.

Alternatives: extending `ptouch://` with a query parameter mixes two transports in one scheme and its URI guard; a user-space serial bridge (`rfcomm bind` to `/dev/rfcommN`) adds a host service and a TTY layer for no gain.

### Configuration selects the transport

`PTOUCH_BT_ADDRESS` set and non-empty selects Bluetooth. It requires `PTOUCH_MODEL=PT-P710BT` and a well-formed `XX:XX:XX:XX:XX:XX` address; anything else fails startup. When it is set, discovery reports only the Bluetooth URI and the queue never touches USB. When it is absent, behavior is exactly today's USB path.

Alternative: try USB first, then Bluetooth. Rejected: it is a silent fallback and makes the transport in use depend on cabling.

### Idle polling is rate-limited in `status_cb`

`PTOUCH_IDLE_POLL_SECONDS` is parsed once at startup. Absent: `status_cb` runs on every PAPPL call, as now. `0`: `status_cb` returns without device access. `N > 0`: `status_cb` returns without device access unless at least `N` seconds passed since its last live read. Skipped calls leave the last published reasons and media in place. Job-owned media refresh and PAPPL's open retry are unaffected, so queued jobs still print when the printer returns.

The setting applies to both transports. A Bluetooth deployment chooses its value; the documented recommendation is `0` for battery use.

Alternative: disable idle polling automatically for Bluetooth. Rejected by the owner in favor of an explicit setting, and it would be a per-transport special case.

### Deployment runs the same container with host networking

Bluetooth sockets work only in the host network namespace, so a Bluetooth container runs with `--network host`. The image's own dbus and Avahi then share the host's mDNS port; the Pi guide disables the host's `avahi-daemon` so the container's instance is the only responder. Host BlueZ owns pairing and link keys on the Pi's persistent storage; the container needs no BlueZ and no added capability.

Alternatives: a native systemd build on the Pi duplicates the pinned CUPS/PAPPL patch build outside CI; mounting the host's dbus socket and using host Avahi changes the entrypoint for one deployment.

### arm64 image built in the same workflow

Add `linux/arm64` to the publish step's platforms with QEMU emulation, so `edge` and `sha-*` become multi-arch manifests. The amd64 load-and-smoke step stays as is; the arm64 build runs CTest under emulation during `Dockerfile.deploy`.

Alternative: a native `ubuntu-24.04-arm` job plus a manifest merge. Faster, but adds a second job and digest stitching. Revisit only if the emulated build time is unacceptable.

## Risks / Trade-offs

- [The printer may treat a Bluetooth disconnect as a session end and drop a held chain end between jobs] → The acceptance gate tests chain-only continuation across two jobs. If the held end is lost, the change documents that limit rather than altering the continuation contract silently.
- [Walking out of range mid-job leaves output uncertain] → The existing uncertain-output contract applies: the queue blocks and requires power-cycle plus restart. Documented as the expected cost of losing the link during a print.
- [A connect to an absent printer blocks for the Bluetooth page timeout, about 5 seconds] → Status reads are skipped while a job owns the device, and polling `0` avoids idle connects entirely.
- [Writes may need pacing over RFCOMM] → Blocking writes rely on RFCOMM flow control. If acceptance shows corruption, add pacing then, with evidence.
- [Emulated arm64 build may be slow] → Measured in CI; switch to a native runner only if it blocks routine merges.
- [Pi Zero 2 W range is roughly 10 m indoors] → An operational limit, documented, not mitigated.

## Migration Plan

Existing amd64 USB deployments need no change: both new variables stay unset and the amd64 image is unchanged. The Pi deployment is new: flash Raspberry Pi OS Lite, install Docker, pair and trust the printer, disable host `avahi-daemon`, run the image with `--network host`, `PTOUCH_MODEL=PT-P710BT`, `PTOUCH_BT_ADDRESS` and `PTOUCH_IDLE_POLL_SECONDS`. Rollback: stop the Pi container and return to the USB deployment on vault.
