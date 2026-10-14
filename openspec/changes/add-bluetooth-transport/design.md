# Design

## Context

See [proposal.md](proposal.md) for motivation and the capability deltas for the behavioral contract.

[verified] All printer I/O uses the `ptouch://` libusb scheme in `src/device_usb.c`. `src/driver.c` gates every live status read on `pt_usb_is_uri()` and reads through `pt_usb_read_status()`; `status_cb` (`src/driver.c:612`) also counts USB devices before opening one. A URI that is not a supported USB URI (the socket test transport) skips live status entirely.

[verified] PAPPL 1.4.11 invokes `status_cb` from `papplPrinterGetReasons()` (`pappl/printer-accessors.c:528`) whenever the printer is idle and the last call is more than one second old. Idle polling is therefore driven by client queries (CUPS, web UI, IPP monitors), not by a timer. When a job cannot open its device, PAPPL stops the queue and retries the open every 5 seconds (`pappl/job-process.c:1192-1213`) until it succeeds or the job is canceled. That retry is independent of `status_cb`.

[verified] PAPPL keeps the job device open across queued jobs: it reuses an open device (`pappl/job-process.c:1188`) and closes it only when no active jobs remain (`pappl/job-process.c:1121`). A burst of queued jobs therefore shares one connection; the device closes when the queue goes idle.

[verified] The live status read loops at most `PT_STATUS_POLL_TRIES` calls, accumulating partial replies (`src/driver.c:89-96`); a read returning 0 means "no data yet" and a negative value ends the read as a failure (`src/device_usb.c:235`). Mainline RFCOMM sockets default to a 30-second connect timeout (`RFCOMM_CONN_TIMEOUT (HZ * 30)`, `include/net/bluetooth/rfcomm.h`).

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

Add `src/device_bt.c` registering a `ptouch-bt://` scheme (`ptouch-bt://Brother/PT-P710BT/<address>`) with open, close, read, write and status callbacks over an `AF_BLUETOOTH`/`BTPROTO_RFCOMM` socket on channel 1. The scheme keeps PAPPL's lifetime: the connection opens when a job starts with no device open, stays open across queued jobs, and closes when the queue goes idle; an idle status read opens and closes its own connection. No extra connection state is added.

The scheme's callbacks keep the USB return conventions so the driver needs no transport-specific handling:

- **Connect** is non-blocking with a 10-second deadline, then the socket returns to blocking mode. Ten seconds covers Bluetooth paging (about 5 seconds) plus RFCOMM setup, and replaces the kernel's 30-second default so an absent printer cannot stall a status read for half a minute.
- **Status read** waits up to the same 300 ms as the USB status read and returns the bytes available, 0 on timeout, and -1 on EOF or error. The existing bounded accumulation loop then handles fragmented replies unchanged.
- **Write** loops until every byte is sent and returns -1 on error or EOF, like the USB full write.

The address is parsed with `sscanf` into `bdaddr_t`, so the build needs only the `libbluetooth-dev` headers (`bluetooth.h`, `rfcomm.h`); the binary links no new library and the runtime image is unchanged.

The driver's USB-only gates become transport-neutral: one function says whether a URI is a live printer transport (USB or Bluetooth), and one bounded status read dispatches by scheme. The USB presence count in `status_cb` stays a USB step; Bluetooth has no enumeration, so its reachability check is the open itself. The socket test transport keeps skipping live status.

Alternatives: extending `ptouch://` with a query parameter mixes two transports in one scheme and its URI guard; a user-space serial bridge (`rfcomm bind` to `/dev/rfcommN`) adds a host service and a TTY layer for no gain.

### Configuration selects the transport

`PTOUCH_BT_ADDRESS` set and non-empty selects Bluetooth. It requires `PTOUCH_MODEL=PT-P710BT` and a well-formed `XX:XX:XX:XX:XX:XX` address; anything else fails startup. When it is set, discovery reports only the Bluetooth URI and the queue never touches USB. When it is absent, behavior is exactly today's USB path.

Alternative: try USB first, then Bluetooth. Rejected: it is a silent fallback and makes the transport in use depend on cabling.

### Idle polling is rate-limited in `status_cb`

`PTOUCH_IDLE_POLL_SECONDS` is parsed once at startup. Absent: `status_cb` runs on every PAPPL call, as now. `0`: `status_cb` returns without device access. `N > 0`: `status_cb` returns without device access unless at least `N` seconds passed since its last idle probe attempt. The timestamp is taken when the attempt starts, so a failed open or failed read counts; otherwise an unreachable printer would be probed on every client query. Skipped calls leave the last published reasons and media in place. Job-owned media refresh and PAPPL's open retry are unaffected, so queued jobs still print when the printer returns.

The setting applies to both transports. A Bluetooth deployment chooses its value; the documented recommendation is `0` for battery use.

Alternative: disable idle polling automatically for Bluetooth. Rejected by the owner in favor of an explicit setting, and it would be a per-transport special case.

### Deployment runs the same container with host networking

Bluetooth sockets work only in the host network namespace, so a Bluetooth container runs with `--network host`. The image's own dbus and Avahi then share the host's mDNS port; the Pi guide disables the host's `avahi-daemon` so the container's instance is the only responder. Host BlueZ owns pairing and link keys on the Pi's persistent storage; the container needs no BlueZ and no added capability.

Alternatives: a native systemd build on the Pi duplicates the pinned CUPS/PAPPL patch build outside CI; mounting the host's dbus socket and using host Avahi changes the entrypoint for one deployment.

### arm64 image built in the same workflow

Set up QEMU and add a non-publishing `linux/arm64` build step beside the existing amd64 check build, so pull requests run the arm64 build and its CTest under emulation. Add `linux/arm64` to the publish step's platforms, so `edge` and `sha-*` become multi-arch manifests. The amd64 load-and-smoke step stays as is. The buildx cache shares layers between the check and publish builds.

Alternative: a native `ubuntu-24.04-arm` job plus a manifest merge. Faster, but adds a second job and digest stitching. Revisit only if the emulated build time is unacceptable.

## Risks / Trade-offs

- [The printer may treat a Bluetooth disconnect as a session end and drop a held chain end between jobs] → Queued jobs share one connection, so only a continuation after the queue went idle crosses a reconnect. The acceptance gate tests that idle boundary. If the held end is lost, the change documents that limit rather than altering the continuation contract silently.
- [The server queries status before initializing and waits up to 20 seconds for receiving-ready (`src/driver.c:74`, `src/driver.c:435`); the reference script initializes first and stops at print-completed] → The acceptance gate checks a reset-free status reply on a fresh connection and records receiving-ready timing.
- [Walking out of range mid-job leaves output uncertain] → The existing uncertain-output contract applies: the queue blocks and requires power-cycle plus restart. Documented as the expected cost of losing the link during a print.
- [A connect to an absent printer blocks for up to the 10-second deadline] → Status reads are skipped while a job owns the device, polling intervals count failed attempts, and polling `0` avoids idle connects entirely. The gate measures the actual connect failure time.
- [Writes may need pacing over RFCOMM] → Blocking writes rely on RFCOMM flow control. If acceptance shows corruption, add pacing then, with evidence.
- [Emulated arm64 build may be slow] → Measured in CI; switch to a native runner only if it blocks routine merges.
- [Pi Zero 2 W range is roughly 10 m indoors] → An operational limit, documented, not mitigated.

## Migration Plan

Existing amd64 USB deployments need no change: both new variables stay unset and the amd64 image is unchanged. The Pi deployment is new: flash Raspberry Pi OS Lite, install Docker, pair and trust the printer, disable host `avahi-daemon`, run the image with `--network host`, `PTOUCH_MODEL=PT-P710BT`, `PTOUCH_BT_ADDRESS` and `PTOUCH_IDLE_POLL_SECONDS`. Rollback: stop the Pi container and return to the USB deployment on vault.
