# server-architecture Specification

## Purpose

Keep the Brother P-touch server small and predictable while exposing ordinary CUPS-compatible IPP printing, truthful physical media, and explicit batch finishing.

Detailed behavior is defined by [label-rendering](../label-rendering/spec.md),
[batch-printing](../batch-printing/spec.md), and [printer-device](../printer-device/spec.md).

## Requirements

### Requirement: Public IPP printer application

The server SHALL use a dedicated PAPPL application to expose one driverless IPP queue. PAPPL SHALL own job scheduling, standard image placement, and public IPP handling. Printer-specific transport, geometry, and finishing SHALL remain inside the application without requiring clients to implement a private printing protocol.

#### Scenario: Ordinary client

- **WHEN** a CUPS or IPP client discovers the printer and submits a supported standard format
- **THEN** it uses the advertised IPP capabilities without a Brother-specific client transport

### Requirement: Pure protocol core

Command builders, device/tape tables, raster packing, status parsing, and the two-flag cut policy SHALL remain pure C without PAPPL, libusb, or I/O. PAPPL callbacks, PNG decoding, and device access SHALL remain in the adapter. The core SHALL support hardware-free builds and tests; it SHALL NOT plan jobs through copy-sized operation arrays.

#### Scenario: Core-only build

- **WHEN** the project is built with PTOUCH_CORE_ONLY enabled
- **THEN** protocol and cut-policy tests run without printer dependencies or hardware

### Requirement: Supported and unambiguous hardware

Only the implemented PT-2730 path SHALL receive print output. Missing or empty `PTOUCH_MODEL` SHALL select PT-2730; unknown or recognized unsupported models SHALL fail configuration. The stable model URI SHALL resolve by VID/PID, accepting exactly one matching device and blocking zero or multiple matches without selecting by enumeration order.

#### Scenario: Duplicate supported printers

- **WHEN** two PT-2730 devices match the configured model
- **THEN** the existing queue reports ambiguity and neither device is claimed for printing

### Requirement: Private USB transport and fresh discovery

The adapter SHALL use the internal `ptouch://` scheme with bounded libusb transfers and a fresh context per discovery/open operation. It SHALL avoid the built-in USB device-ID probe that hangs on PT-2730. Status readers SHALL validate the owning printer URI before accessing USB connection data; attached USB hardware SHALL NOT change another transport's type.

#### Scenario: Replug and transport isolation

- **WHEN** the sole printer returns with a different USB node while a socket test transport also exists
- **THEN** the stable USB queue can recover, and socket status never invokes USB-specific operations

### Requirement: Canonical physical media

Media SHALL describe nominal 6/9/12/18/24 mm tape widths with 32/52/76/120/128 imageable dots at 180 dpi. One geometry definition SHALL govern publication, validation, and extraction. Per-tape margins MAY be asymmetric to reproduce the canonical window. Custom width ranges SHALL allow only ±0.01 mm rounding; submitted dimensions and margins SHALL remain authoritative.

#### Scenario: Standard media selection

- **WHEN** a client selects a named preset or a supported custom length
- **THEN** the published media describes its physical tape and margins without enlarging the loaded tape's imageable window

### Requirement: Strict physical rendering

Wrong-tape page geometry SHALL fail before page setup or raster output. At print-scaling none, PNG content SHALL fit the selected page and physical window; a nominal-width canvas SHALL be accepted only when its non-imageable margins are blank. Explicit standard scaling SHALL use PAPPL placement. JPEG SHALL retain its standard selected/default media path.

#### Scenario: Content exceeds selected margins

- **WHEN** a 1:1 PNG exceeds the selected printable area, including larger explicit custom margins
- **THEN** the job fails visibly without automatic cropping, shrinking, or replacing those margins

### Requirement: Public PNG route and explicit length

PNG SHALL use the public image/png format, an owned filter and matching printfile callback, libpng decoding, and PAPPL's image renderer. Submitted named media, media-col.media-size-name, or concrete y-dimension SHALL preserve explicit length. Only omitted length SHALL derive from the oriented image within 5–300 mm; source-only or margin-only collections SHALL retain this convenience.

#### Scenario: Named collection length

- **WHEN** a 180-pixel PNG at 180 dpi selects 50 mm through media-col.media-size-name
- **THEN** its page remains 50 mm rather than receiving the image-derived 25.4 mm length

### Requirement: PNG orientation and resolution

PNG SHALL preserve explicit orientation and square DPI; missing DPI SHALL mean 180 dpi and non-square DPI SHALL fail. Automatic orientation SHALL choose portrait for tall/square sources and reverse-landscape for wide sources before deriving length. Complete decoding and 1:1 preflight SHALL precede page output, and all length-derived header fields SHALL change together.

#### Scenario: Wide source without explicit orientation

- **WHEN** a wide PNG omits orientation and page length
- **THEN** it rotates 90 degrees clockwise and derives physical length from the resulting along-tape axis

### Requirement: Stable offline queue and media confirmation

The queue SHALL exist while hardware is absent, accept queued jobs, and resume after supported hardware returns. Presence alone SHALL NOT imply readiness. Live media SHALL be confirmed before PNG geometry is constructed, and idle polling SHALL share fault publication with job-owned refresh. Busy processing SHALL NOT be reported offline merely because the job owns the device.

#### Scenario: Printer returns with another cassette

- **WHEN** a PNG queued while offline resumes with supported tape
- **THEN** geometry uses confirmed loaded media rather than the offline placeholder

### Requirement: Explicit batch policies

One IPP job SHALL define one batch. Standard finishings SHALL select chain-and-final (60, alias 11), final-only (63), or no label cuts (3 or collection no-value); `finishings-col` template `ptouch-chain` SHALL select chain-only. Every nonempty batch SHALL have mandatory initial precut. Omitted selection SHALL use the synchronized printer default, initially trim (11), as specified in batch-printing.

#### Scenario: Chain-only batch

- **WHEN** a client requests ptouch-chain through finishings-col
- **THEN** labels are cut apart and the last end remains held for continuation

### Requirement: Constant-size deferred batch lifecycle

Batch state SHALL contain the resolved policy, actual-label progress, one pending print command, and failure state without an operation array or extra raster buffer. PNG copies SHALL expand once; raster labels SHALL need no declared total. A next valid label SHALL release the prior complete label with FF and confirm readiness. Successful input completion SHALL release the final label with FF for continuation or late chain-off/EJECT for finishing.

#### Scenario: Unknown label count

- **WHEN** a valid streamed raster batch omits its total count
- **THEN** actual complete labels determine boundaries and successful end-of-input determines finalization

### Requirement: Healthy continuation

The queue SHALL own closed, healthy-open, and uncertain run state with media confirmation. Only a known closed run SHALL be initialized. Healthy unfinished runs SHALL survive idle gaps; subsequent mandatory precut SHALL separate the held end without another discarded leader. Rejected or canceled followers that sent no page output SHALL preserve the preceding healthy run.

#### Scenario: Consecutive no-cut batches

- **WHEN** a healthy none batch is followed after an idle gap by another none batch
- **THEN** each batch remains a joined strip, separated by the later precut without discarded inter-batch leader waste

### Requirement: Conservative output recovery

Ambiguous transfer, decode, or readiness failure SHALL block later output without automatic reset, feed, cut, or replay. Cleanup SHALL be idempotent and emit no pending finalization. Recovery SHALL require printer power-cycle followed by server restart and explicit resubmission after inspection. Clean status or USB reopen SHALL NOT clear uncertainty; process-local queue/run persistence SHALL NOT be promised.

#### Scenario: Incomplete label

- **WHEN** raster input ends partway through a label that has already reached the printer
- **THEN** the job aborts without finishing that label and subsequent output remains blocked pending explicit recovery

### Requirement: Bounded dependency corrections

Dependency changes SHALL stay pinned, narrow, and documented in [patches/README.md](../../../patches/README.md). They SHALL preserve original IPP finishing selections, synchronized representable defaults, truthful media, and raster geometry/error handling through existing PAPPL/CUPS APIs. They SHALL NOT introduce another spooler, raster parser, or generic finishing engine.

#### Scenario: Dependency update

- **WHEN** a pinned dependency is updated
- **THEN** the documented correction and removal criteria are reviewed against ordinary IPP and raster regressions

### Requirement: Container hotplug and addressing

Container deployment SHALL bind-mount `/dev/bus/usb` and allow USB character devices with `c 189:* rwm` so newly numbered nodes remain accessible. dbus/Avahi SHALL receive a nofile limit of 65536. Reverse proxies SHALL forward a Host accepted by PAPPL; shared-network clients MAY use a stable alias. Discovery, TLS termination, and network exposure SHALL remain deployment choices.

#### Scenario: Hotplug behind a proxy

- **WHEN** the printer is replugged in a container reached through a reverse proxy
- **THEN** the new USB node is accessible and the proxy supplies an accepted Host without changing the print contract

### Requirement: GPL license and attribution

The project SHALL retain GPL-3.0-only licensing and attribution for the Brother protocol, device/tape tables, and cutter-derived work from ptouch-print and its macOS fork. LICENSE, README.md, and THIRD_PARTY_NOTICES.md SHALL preserve the licensing and upstream credit appropriate to their roles.

#### Scenario: Distribution

- **WHEN** the server's source or container is distributed
- **THEN** its GPL license and upstream attribution remain available
