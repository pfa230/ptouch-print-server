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
so a URI stays valid across replug and bus renumbering. PT-2730 is the only implemented print path.
Validate `PTOUCH_MODEL` before discovery: omission or an empty value selects PT-2730; unknown or
recognized-but-unsupported values fail startup rather than falling back. Recognized unsupported
devices appear in discovery diagnostics but are never offered as printable devices or opened.

Count every matching device before discovery reports a candidate and before opening claims one.
Zero means offline; exactly one can be used; multiple PT-2730 units are ambiguous and none is
claimed. Keep the same queue and model URI when absent or ambiguous, so unplugging a duplicate or
replugging the sole supported printer can restore availability without queue replacement. Never
choose by enumeration order, serial number, or a device-ID probe.

## Consequences

- No dependence on IEEE-1284 device IDs, so no hang.
- Replug works without restarting the process.
- USB access is guarded centrally and at the short-timeout reader. `pt_usb_read_status` takes the
  owning printer's URI and rejects a non-supported transport before accessing connection data.
  PAPPL has no public accessor for a device connection's URI; callers pass
  `papplPrinterGetDeviceURI`, never an inferred URI based on USB presence. Socket test status uses
  the driver's explicit test path, and an attached Brother cannot change its transport type.
- Hardware-free adapter tests inject zero, one, or two devices, unsupported models, claim failures,
  and a changed node after replug. A real PAPPL socket fixture confirms rejected reads never
  inspect network connection data or issue a USB transfer. These tests do not certify hardware.

## Alternatives considered

- **Fix/avoid the `usb://` hang upstream.** Out of our control and slower than owning the transport.
