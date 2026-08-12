# 2. Custom `ptouch://` USB device scheme

**Status:** Accepted

## Context

PAPPL ships a `usb://` device scheme. On the PT-2730 it hangs: the scheme issues an IEEE-1284
GET_DEVICE_ID request that the printer never answers, and the M0 spike reproduced the hang reliably.

A second problem appeared later: in a container with no hotplug uevents, a process-lifetime libusb
context goes stale across a replug, so the device is never found again.

## Decision

Register a custom `ptouch://` scheme (`papplDeviceAddScheme`) implemented directly on libusb, and
create a fresh `libusb_context` per operation rather than holding one for the process lifetime.

URIs are model-based (`ptouch://Brother/PT-2730`) and resolved to a device by VID/PID at open time,
so a URI stays valid across replug and bus renumbering. An empty model matches any supported P-touch.

## Consequences

- No dependence on IEEE-1284 device IDs, so no hang.
- Replug works without restarting the process.
- Anything that touches the device must be gated on the scheme: `pt_usb_read_status` casts the PAPPL
  device's data pointer to our libusb struct, so calling it on a `socket://` device is undefined
  behaviour. This gate is repeated at every call site and must not be forgotten.

## Alternatives considered

- **Fix/avoid the `usb://` hang upstream.** Out of our control and slower than owning the transport.
