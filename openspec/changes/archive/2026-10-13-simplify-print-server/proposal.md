# Proposal

## Why

The server should accept ordinary CUPS/IPP jobs with predictable media and finishing behavior, but the audit reproduced multi-page raster failure, silent PNG cropping, ignored explicit media length, and rejection of advertised 18 mm tape. Simplify the existing PAPPL adapter around actual pages and truthful capabilities rather than extending its special client contract.

## What Changes

- **BREAKING:** Support four finishing presets: cut between labels and continue, cut between labels and finish, no label cuts and continue, and only finish after the batch. Keep cut-between-and-finish as the initial default, using `trim` as its compatibility alias.
- **BREAKING:** Precut is mandatory once at the beginning of every nonempty batch, including the no-label-cuts and final-only presets. On a healthy open run it separates the previous batch's held end instead of generating a discarded leader; verify this on hardware. Remove the optional precut environment setting.
- One IPP job is one batch, covering PNG copies or actual raster labels. Preserve efficient chaining within and across unfinished batches, including idle gaps; introduce no queue inference, idle cutting, or per-label run finalization.
- Replace the copy-sized operation array with constant-size job state. Defer one label's print/feed command until the next label begins or the batch ends, handling unknown raster label counts without buffering another raster document. Hardware validation gates the late finalization sequence and final-only cutting.
- Expose the presets through standard IPP finishings: registered `none` (3), `trim-after-pages` (60), and `trim-after-job` (63), plus `trim` (11) as an alias. Advertise `ptouch-chain` as an implementation-defined finishing template for chain-only through standard `finishings-col`; do not invent an independent vendor job attribute or redefine 60 to exclude the final label.
- **BREAKING:** Honor explicitly requested IPP media geometry for PNG. Retain image-derived PNG length only when the client omitted a concrete media length; JPEG and standard raster continue using their standard page geometry.
- Reject oversized 1:1 PNG content before raster output instead of silently clipping it, and make all advertised tape widths and margins usable, including 18 mm. Retain JPEG's standard PAPPL placement path without adding another custom decoder.
- Remove the unsupported private raster MIME advertisement and the implementation's 64-copy planner limit; make advertised formats and format-specific copy ranges executable.
- **BREAKING:** Limit operational queues to the verified PT-2730 path, reject ambiguous USB matches, and fail invalid model configuration instead of silently selecting a fallback.
- Keep one application queue and give its state a clear owner; remove the global eight-slot registry while retaining required synchronization and unfinished-run reset protection.
- Centralize safe transport access, report unavailable/error states truthfully, and retain offline queuing and hotplug recovery. Distinguish a healthy unfinished run from uncertain partial output; block continuation from uncertain state until explicit recovery rather than blindly resetting the printer.
- Use normal-level stderr logging without a log-tail process; remove the ineffective width-policy configuration knob from the supported interface.
- Run the existing tests plus the audit regressions in CI before image publication, then require separate PT-2730 physical validation of changed output behavior.

## Capabilities

### New Capabilities

- `label-rendering`: Standard job/page geometry, image sizing, printable-area validation, tape margins, and truthful document/copy capabilities.
- `printer-device`: Supported USB device selection, safe transport access, status reporting, and offline recovery for the persistent queue.

### Modified Capabilities

- `batch-printing`: Four finishing presets, mandatory precut, job-defined batch boundaries, continuation without inter-batch leader waste, and failure handling; supersede the former no-precut rule and inaccurate document-format/copy-limit preservation.

## Impact

- Implementation: `src/driver.c`, `src/cutter.*`, `src/filter_png.*`, `src/driver.h`, `src/main.c`, `src/device_usb.*`; table entries may remain as recognition metadata but must not grant print support.
- Validation/operations: `tests/`, `CMakeLists.txt`, `Dockerfile.deploy`, `.github/workflows/deploy.yml`, `docker-entrypoint.sh`, README, and affected ADRs.
- Bounded patches to pinned PAPPL 1.4.11 must preserve raster geometry, reject truncated input and failed callbacks, and support the driver's advertised finishing presets in validation and defaults. A bounded CUPS raster-decoder correction must distinguish partial next headers from clean EOF, propagating failure through PAPPL before normal end-job. Pin compatible CUPS source while retaining packaged security corrections. Ship identical dependency patches and runtime libraries in development and deploy builds; add no raster parser, page spooler, or separate maintained fork.
- Keep PAPPL, libusb, libpng, the hardware-free protocol core, bounded USB I/O, and the working USB bind-mount deployment approach. No new network protocol, REST endpoint, model family, daemon, or persistent batch database.
- Existing clients that omitted finishings will now receive cutting; `none` still leaves the batch unfinished but now includes mandatory precut. Consecutive no-label-cuts batches become separate strips at the next batch's precut. PNG media dimensions are honored. The chain-only preset has a printer-defined meaning carried by standard IPP, and generic clients may need collection/template support to select it.
- This proposal authorizes planning only. Implementation remains local until specifications are synced and this change is archived; publication and production rollout require a separate user instruction.
