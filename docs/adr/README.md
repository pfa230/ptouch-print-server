# Architecture Decision Records

Decisions that shaped this project, with the reasoning and the consequences that followed. Several
record a mistake and its correction, because the correction is the useful part.

| ADR | Decision |
|-----|----------|
| [0001](0001-own-pappl-application.md) | Build our own PAPPL application rather than extending LPrint |
| [0002](0002-custom-ptouch-usb-scheme.md) | Custom `ptouch://` USB scheme over libusb, per-operation contexts |
| [0003](0003-pure-core-separate-from-adapter.md) | Pure C core, unit-testable without hardware |
| [0004](0004-strict-width-guard.md) | Strict width guard: reject mismatches, never scale silently |
| [0005](0005-nominal-media-size-with-margins.md) | Advertise nominal tape width with real margins |
| [0006](0006-own-png-filter.md) | Own the `image/png` filter to derive label length from the image |
| [0007](0007-offline-is-a-state.md) | Offline is a printer state, not an absent printer |
| [0008](0008-cross-job-chaining.md) | Chain consecutive queued jobs; never re-init mid-chain |
| [0009](0009-deployment-topology.md) | Caddy at the front, shared network for the client, bind-mounted USB |
| [0010](0010-gpl-licensing.md) | GPL-3.0-only, forced by derived protocol code |
