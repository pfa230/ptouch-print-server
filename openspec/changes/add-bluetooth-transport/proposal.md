# Proposal

## Why

The owner wants the PT-P710BT to run on its own battery and travel around the house, while the print server stays on a wall-powered Raspberry Pi Zero 2 W. Today every status read and print goes through the USB-only `ptouch://` scheme, and the published image is amd64-only, so the printer must stay cabled to an amd64 host.

## What Changes

- Add a Bluetooth RFCOMM transport for the PT-P710BT, selected by configuring the printer's Bluetooth address (`PTOUCH_BT_ADDRESS`). The queue then uses only Bluetooth, never USB, and does not fall back to USB. Configuring an address for any other model fails startup.
- Connect only while a job or status read owns the device and disconnect afterwards. The printer stays available to its phone app between jobs.
- Treat an unreachable printer (switched off, out of range, or held by another Bluetooth client) as offline. Queued jobs wait and print when the printer becomes reachable. Reuse the existing offline, media-confirmation, finishing, continuation and uncertain-output contracts unchanged.
- Make idle status polling configurable for every transport with `PTOUCH_IDLE_POLL_SECONDS`. Absent keeps today's behavior, `0` disables idle polling so status refreshes only during jobs, and a positive value sets the minimum seconds between idle polls. An invalid value fails startup.
- Publish the runtime image for `linux/arm64` as well as `linux/amd64` under the same tags.
- Document Pi deployment: pairing, host networking, and the required container settings.

## Capabilities

### New Capabilities

None. Transport selection and status polling belong to the existing printer-device capability.

### Modified Capabilities

- `printer-device`: Add Bluetooth device selection and reachability for PT-P710BT, and the configurable idle polling contract.
- `server-architecture`: Extend the private-transport constraint to the Bluetooth scheme, add Bluetooth container deployment, and add the arm64 image.

`batch-printing` and `label-rendering` remain unchanged: the same command and raster stream travels over either transport.

## Impact

Code: a new Bluetooth device scheme beside `src/device_usb.c`, transport-neutral status reads in `src/driver.c`, configuration parsing in `src/main.c`, and polling control in `status_cb`. Tests: fake-socket adapter tests for the Bluetooth scheme and the polling setting, plus core regressions showing unchanged bytes. CI: `.github/workflows/deploy.yml` builds and publishes arm64. Runtime image: no new libraries (RFCOMM uses the kernel socket API). Docs: README, deployment notes, and a bounded Bluetooth acceptance gate.

No new client attribute, IPP behavior, rendering rule or dependency fork. USB deployments that leave both new variables unset keep their exact current behavior.

This proposal authorizes planning only. Implementation requires a subsequent apply request; physical tests require a concrete bounded gate, and Pi deployment remains a separate action.
