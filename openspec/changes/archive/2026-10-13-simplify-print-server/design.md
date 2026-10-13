# Design

## Context

See proposal.md for motivation and the three delta specifications for behavior. This design implements the audit as four related areas rather than nine independent fixes.

[verified] The audit's temporary socket/IPP probes reproduced a two-page PWG job ending aborted, a requested 50 mm PNG printing 25.4 mm, a 200-pixel-wide PNG losing both edge markers, and PAPPL rejecting 18 mm ready-media. Existing six container suites and seven capture tests passed, so those results do not cover the missing contracts.

[verified] The adapter already has useful protocol builders, checked USB writes, receive-ready waits, nominal tape dimensions, and no-reset handling for unfinished tape. Keep these. PAPPL owns IPP, discovery, scheduling, image placement, and raster conversion; the adapter owns the Brother device and label-specific decisions.

## Goals / Non-Goals

**Goals:** Constant-size lifecycle state; actual-label processing with explicit batch boundaries; four finishing presets and mandatory precut; no discarded leaders between healthy continued batches; truthful IPP capabilities; fewer production workarounds and operational processes.

**Non-Goals:** New public protocols or independent vendor job attributes, a label designer, new Brother model support, a general printer framework, multi-printer identity provisioning, persistent batch state, a new raster renderer, or automated production rollout. Keep unfinished-run restart limitations documented. Do not add queue inference, idle finalization, or automatic content retries.

## Decisions

### 1. Four finishing presets share one efficient tape run

One IPP job is one batch: all copies of its PNG or all actual labels in its raster document. Successive jobs remain separate batches even when they continue the same physical tape run. Internally the policy has two facts, `cut_between` and `cut_at_end`:

| Preset | Between labels | Final batch cut | Public IPP selection |
| --- | --- | --- | --- |
| Chain and continue | Yes | No | `finishings-col` template `ptouch-chain` |
| Chain and finish | Yes | Yes | `trim-after-pages` (60), with `trim` (11) as compatibility alias |
| No label cuts and continue | No | No | `none` (3) |
| Final cut only | No | Yes | `trim-after-job` (63) |

Initialize enum/collection defaults to the existing `trim` alias for chain-and-finish. Omission honors the current advertised default. The two internal facts are not exposed as independent public switches: registered 60 already includes the last label, so combining 60 and 63 remains chain-and-finish and must not emit two terminal cuts.

Precut is mandatory batch preparation, not another setting. Enable first-label autocut for every nonempty batch. Enable later-label autocut only when `cut_between` is true. A fresh run cuts its initial leader; on a healthy open run the next batch's precut must separate the previous held end with no discarded leader between batches. Consequently two no-label-cuts batches become two strips, separated by the second batch's precut. This explicitly revises the old joined none/none contract.

Keep chaining between labels. Final feed/cut happens only at successful batch end when requested; do not fully finalize each label. Remove `PTOUCH_PRECUT` from the supported interface, alongside the already-obsolete cut-mode and ineffective width-policy settings. Reject unsupported/conflicting finishing choices before output and never use queue length or elapsed time to select a policy.

### 2. Advertised IPP presets survive validation and decoding

[verified] [IPP Finishings 3.0](https://ftp.pwg.org/pub/pwg/candidates/cs-ippfinishings30-20220527-5100.1.pdf), section 5.1.5, assigns 60 to trimming after pages and 63 to trimming after the job. Section 5.2.5 allows implementation-defined finishing-template names. `ptouch-chain` uses that standard collection mechanism with a printer-defined meaning; it is not a registered portable chain flag. Some generic dialogs will only expose registered choices, so prove collection submission through CUPS and document the discoverable template.

[verified] Pinned PAPPL validates and resolves finishings through fixed none/punch/staple/trim bits. Merely adding advertised values will not work: `printer-ipp.c` rejects unsupported bit mappings, and `job-process.c` cannot preserve 60, 63, or the named chain-only choice in `options->finishings`.

Use one small driver preset table for advertised enums/templates, aliases, defaults, and resolution to the two internal facts. Resolve the original job attributes using `papplJobGetAttribute` and the advertised defaults using `papplPrinterGetDriverAttributes`; do not depend on the lossy bitmask. Extend the pinned PAPPL boundary so the driver's advertised finishing values are accepted, capability overrides are unambiguous, and default updates remain consistent. Keep ordinary handling for other drivers unchanged. Reject unknown values and conflicting presets even when a client permits attribute substitution; unsupported finishing must not quietly become no-cut.

[verified] FIN section 6.10 requires enum and collection defaults to describe the same finishing processes; for enum none the collection default must be the out-of-band `no-value`. Keep one canonical default preset, limited to the three presets with enum equivalents. An enum or equivalent collection default update changes that preset and republishes both representations together; reject contradictory simultaneous updates without changing either. Reject ptouch-chain as a printer default because there is no lossless registered enum equivalent. It remains available explicitly per job, including a client queue preference that submits the template with each job. Omitted job selections use the canonical default, while explicit enum or collection requests override it. Add no fallback enum or hidden precedence rule.

Advertise matching supported/ready/template/database values and useful labels for the choices. Use collection `no-value` for the no-label-cuts choice rather than inventing a registered none template. Accept supported templates and registered combinations with equivalent meaning, including 60 plus 63. FIN section 5.1 requires ignoring enum none when accompanied by other finishing values; normalize it before resolving the remaining selection. Reject both enum and collection attributes in the same job request. Test requests and defaults through real IPP; do not treat parsing a private option in the driver as equivalent to library acceptance.

Alternative considered: Claude's proposed `ptouch-tape-run=finish|continue` vendor attribute. It makes the two facts independent at the cost of a new public contract and changes the meaning of existing none submissions. Do not add it. Do not reinterpret 60 as all-except-last or assign an unregistered numeric enum to chain-only.

### 3. Small owned state replaces operation arrays and global slots

Allocate one printer state object in `pappl_pr_driver_data_t.extension`, free it in `delete_cb`, and retain its mutex for status/job concurrency. Track closed, healthy open, or uncertain run state plus media confirmation; do not introduce a general registry. The application continues creating one queue and retains PAPPL's ordinary web interface.

[verified] Retain `PAPPL_SOPTIONS_MULTI_QUEUE`: pinned PAPPL also uses that flag to select the queue resource and permit multiple pending jobs. Removing it reduces active-job capacity to one and would break existing offline queuing. It is a framework setting with necessary behavior, not a reason to keep a global driver-state registry. Preserve `/ipp/print/ptouch` and test two or more queued jobs while offline.

Keep job state for the two policy facts, first-label/progress tracking, tape geometry, one pending print command, and a latched failure. The first-label fact comes from this state, not the callback page number: [verified] PAPPL's image copy loop passes page number zero for every copy (`job-filter.c:362`). Otherwise mandatory precut would accidentally cut every copy in a final-only batch.

Proposed lifecycle:

1. Start the job only after policy and clean media validation. Reset only a known closed run, never a healthy open or uncertain run. Uncertain state blocks page output pending explicit recovery.
2. At label start, validate its geometry. If the preceding complete label has a pending print command, send FF and consume receive-ready before starting the next label. Configure first-label autocut, or later-label autocut only for cut-between, and chain-on before raster.
3. Write checked raster; latch every failure and stop later writes after it. Mark printer state uncertain while a label is being transferred or its output is unconfirmed.
4. At label end, retain a pending print command for the complete label. Do not send FF/EJECT yet. Receiving the next valid label proves that this one is intermediate; normal job end proves that it is the final one. No declared total label count or application raster buffer is required.
5. At successful job end, send FF for a continuation preset, or late chain-off plus EJECT for a finishing preset, and consume receive-ready. Set healthy open or closed only after confirming the transfer and readiness. Normal job completion must follow successful device output.
6. On cancellation, decode/transfer/status failure, or abort cleanup, send no pending print/feed or final-cut command. Preserve uncertain state when output is ambiguous, free job state, and make repeated cleanup idempotent. Latch failures on every callback exit; checking only job-state is insufficient because PAPPL's image abort path can call rendjob before marking the job aborted.

[verified] Streaming PAPPL calls rendpage before trying the next header, so it cannot reveal the true final label there. Both its raster and image paths check normal rendjob failure; the image abort path can call rendjob again. The proposed deferred command must distinguish these paths and never replay a terminal attempt.

[verified] The approved eight-label PT-2730 protocol gate passed on 24 mm tape: chain-only then chain-and-final, and none then final-only, with two labels per batch. The operator confirmed mandatory initial precut, late finalization of multi-label batches, final-only cutting with autocut disabled on later labels, and no discarded leaders during healthy continuation. See validation.md for captures and observations. This permits local production-lifecycle implementation; integrated PNG/raster and recovery acceptance remains pending. Retain other counts in software captures and authorize the larger physical acceptance matrix separately. A failed candidate is grounds to revise its implementation, not to silently drop a required mode, require custom count metadata, or weaken unknown-count support.

Remove `pt_batch`, the operation array, scans, and the planner-derived 64-copy ceiling. Retire the array representation while preserving the now-required end-only and cross-batch semantics in a small pure policy helper. Existing `PT_CUT_END` is not reusable unchanged: unconditional `do_precut` currently enables autocut on every label. Test observable policy rather than the retired representation.

### 4. One tape-geometry definition serves publication and validation

Use the existing supported tape table to derive nominal width, exact imageable pixel width, canonical pixel window, and physical margins. The printer-wide minimum margin must permit every advertised tape, including 18 mm; actual media-ready/default carry the loaded tape's margins. Validate client-selected margins against the physical imageable window so a minimum advertised margin cannot expand what the hardware can print.

Check page width against confirmed nominal tape width, and source content against the canonical imageable window. Keep bounded rounding conversions in one geometry helper instead of scattering full-page/imageable conversions and correction comments. Validate all five advertised widths in tests. Treat publication failure as a real readiness failure, never silently retain placeholder media as known good.

For PNG jobs at print-scaling none, accept designs that fit the imageable area. Also accept nominal-width canvases with blank edge margins, extracting only verified blank margins before placement when needed; reject nonblank content outside the physical window. Reject excessive along-tape PNG content when explicit media length is shorter. Keep explicit orientation and the current documented automatic PNG orientation; test asymmetric markers so flips cannot hide in symmetric images. JPEG continues through PAPPL's standard placement path; this change does not promise PNG source-content preflight for JPEG or add another decoder to implement it.

This change adds no server-selected fit/crop policy. Existing explicitly requested standard scaling must either be honored as advertised or rejected visibly; do not reinterpret it as a silent fallback. The no-content-loss regressions specifically exercise print-scaling none.

### 5. PNG length is a fallback, and its public format is really PNG

Keep the working libpng decode and `papplJobFilterImage` path. Identify concrete submitted media length from `papplJobGetAttribute`: a resolved named `media`, or `media-col.media-size.y-dimension`. A source-only/margin-only collection is not an explicit page size. Build options after live tape confirmation. Preserve explicit size; otherwise derive length from the oriented PNG at its DPI, retaining the 180 dpi fallback and 5 to 300 mm range.

[verified, static API inspection] A minimal supported MIME route exists in pinned PAPPL: set driver format to `image/png`, register `image/png -> image/png` to the owned callback, and provide a real PNG `printfile_cb` wrapper. PAPPL accepts that distinct source/destination pair, selects it before its built-in PNG-to-PWG path, and deduplicates PNG in the public format list. Verify this with an actual IPP upload before removing the current private MIME route. PNG remains a normal input, and neither a private format nor an always-rejecting raw callback is needed.

[verified] The file-processing path checks the matching filter before raw printfile handling (`job-process.c:534-544`). `papplJobFilterImage` loops over copies and calls normal rendjob after the loop (`job-filter.c:356-573`), so one PNG job remains one batch, rather than copies becoming separate batches or being multiplied twice. Retain an IPP routing/copy test for this boundary and abort-path cleanup.

Do not rewrite PNG decoding just to reduce its line count. Focus on removing duplicate policy and incompatible geometry behavior. JPEG keeps its existing standard media-based sizing; automatic image length is a documented PNG convenience, not a promise for every format. Explicit geometry has the same meaning across formats.

### 6. Bound dependency changes to raster correctness and finishing acceptance

[verified] In PAPPL 1.4.11 `pappl/job-process.c`, incoming raster geometry is copied to callback options only when input and output are both at least 8 bits per pixel. This adapter forces 1-bit output, so callbacks receive default-derived geometry instead. Raster submissions bypass MIME filters. Moving checks into rstartpage or changing the PNG filter cannot fix that boundary.

Keep documented patches under `patches/` against pinned PAPPL. The raster correction retains incoming per-label dimensions, resolution, and coherent page/image-box geometry in callback options while preserving PAPPL's selected output pixel format and conversion. Recompute output bytes-per-line and allocation sizes from that geometry; make loop bounds follow the actual label so no default-length padding/truncation occurs. Validate geometry before output, including contradictory explicit job/page dimensions.

[verified] The partial-next-header regression still completed after adding one byte to a valid URF stream: CUPS 2.4.x returns the same read-header result for clean EOF and a truncated next header, without a distinct error. Its compressed input buffer can read ahead, so PAPPL-side HTTP byte counters cannot resolve this safely. The user approved extending dependency scope to a bounded CUPS decoder correction. Report partial-header failure through the existing CUPS error mechanism and propagate it in PAPPL before normal end-job, retaining valid unknown-count streams. Pin a compatible CUPS source with the packaged security corrections; build and ship the corrected shared libraries consistently in both images. Do not add a private decoder, byte-at-a-time HTTP workaround, or unrelated CUPS changes.

[verified] The existing raster discard loop advances its row counter even when reads fail (`job-process.c:777-784`), hiding truncated input. Require complete raster rows and declared page data, distinguish decoding errors from normal EOF, and mark abort before normal label completion on malformed input. Check scanline callback failures immediately instead of ignoring them. Retain the driver failure latch for repeated cleanup and image-path failures; no pending label may be printed/finalized after a malformed stream or failed callback.

The finishing correction implements Decision 2: accurate driver capability overrides, validation against advertised enums/templates, and preservation of chosen/defaulted preset semantics. The patch scope is explicitly broader than geometry-only, but bounded to these demonstrated PAPPL integration defects. Keep policy in the driver and add no generic finishing engine.

Apply identical PAPPL/CUPS patches during Dockerfile and Dockerfile.deploy builds, failing when they no longer apply, and ensure the runtime links the corrected CUPS libraries. Test 1-bit and 8-bit PWG/URF, different lengths in one job, unknown total counts, malformed dimensions, short rows, bad/truncated stream termination, all four finishing choices, default changes, and output failures through IPP rather than only calling callbacks. Keep upstream attribution and removal conditions beside the patches. Remove a patch only when the pinned dependency supplies equivalent verified behavior.

Alternative considered: a private raster parser or page-buffering pipeline. It would duplicate PAPPL and add more long-term maintenance than the bounded dependency corrections. A broad dependency upgrade is outside this change; no upgrade is assumed to solve the missing callback geometry.

### 7. Device support, readiness, and uncertain output have one safe boundary

Validate the configured model before discovery, whether hardware is attached or absent. Default to PT-2730; known-but-unimplemented models must never become printable. Count all matching supported USB devices during discovery and opening. Zero matches means offline; one is usable; more than one means ambiguous and no device is claimed. Preserve fresh libusb contexts, bounded I/O, VID/PID identity, and hotplug-safe model URIs. Do not add descriptor/serial probes that could reintroduce the known device-ID hang.

Gate USB-specific connection access centrally and also at the low-level reader so arbitrary PAPPL connection data cannot be cast as a USB handle. Keep the socket override as a test facility with a defined non-USB status path; production USB presence must not change its transport type.

Use one interpretation/publication path for parsed tape and errors, shared by idle polling and job-owned refresh. Distinguish absence, inability to open/claim, missing tape, unreadable/unsupported media, and printer error bits. Publish appropriate existing standard IPP reasons plus useful diagnostics. Confirm clean live status before page raster, including after an offline interval; transient malformed reads must not turn unknown state into ready state.

Keep the stable offline queue and PAPPL scheduling. A job whose content cannot be safely retried fails visibly; physical content is never automatically replayed during cleanup.

Uncertain output is a separate readiness condition, not an ordinary healthy open run. Conservatively block new page setup/raster and report recovery required. The documented recovery is to power-cycle the printer, then restart the server, and resubmit explicitly as needed; existing process-local queues are not promised to survive that restart. Do not clear this latch on a clean status reply or USB reopen alone, and do not adopt Claude's automatic ESC @ recommendation: ADR-0008 records that resetting an unfinished run can wedge the PT-2730. Rejection before any page output leaves a prior healthy run intact. Hardware probes must confirm recovery clears buffered content without ghost labels or replay.

### 8. Build, logging, and delivery remain small

Use PAPPL log filename `-` (its documented stderr target), INFO for normal operation, and its existing diagnostic controls for debugging. Remove `/tmp/ptouch.log` and the tail process. Keep the dbus/Avahi dependencies required by this deployment; adding a general process supervisor is outside this simplification. Their startup failures must remain explicit in diagnostics.

Remove the optional precut and ineffective width-policy knobs from the supported interface and example deployment; mandatory first-label precut and strict geometry validation are ordinary behavior. Document retirement with the already-obsolete cut-mode setting. Add an explicit CMake core-only option, preserving hardware-free builds while making a requested/full app build fail when dependencies are missing.

CI must run the complete app/core tests before pushing an image on main, as well as on PRs. Keep the existing IPP smoke as a deployment wiring check and extend it with a real socket capture submission; it is not physical certification. Ensure the capture tests run too, rather than being silently omitted when Python is unavailable. Use existing test tools instead of adding another test framework.

## Risks / Trade-offs

- Default trim changes output for existing clients that omitted finishings. Migration documentation must say to request none explicitly for continuation; expose no hidden compatibility toggle.
- Deferred last-label printing and late finalization change a hardware sequence. Gate production use on all four presets, especially final-only and no discarded leaders between healthy continued batches. A fresh run after deliberate finalization may need its own leader; do not demand one leader across two separately finished runs. Socket bytes alone cannot certify these outcomes.
- Mandatory precut changes none: it still has no internal/final cuts, but the next batch can separate the preceding strip. Document this migration explicitly and remove optional-precut compatibility toggles.
- Dependency patches add maintenance. Bound them to demonstrated raster and finishing integration defects, apply reproducibly, and test real IPP acceptance as well as output; do not grow a separate PAPPL fork.
- The chain-only template has printer-specific meaning. Preserve standard enum semantics, advertise/localize the template, and prove CUPS collection submission rather than assuming every generic dialog can expose it.
- Unfinished/uncertain runs remain process-local and shared within the queue. Explicit recovery may discard unprinted buffered content and requires a printer power-cycle plus server restart; report failures and resubmission needs honestly, adding no persistence or automatic replay subsystem.
- Automatic PNG orientation and physical rounding can move edge content. Test asymmetric portrait/landscape markers, all tape widths in software, and physically verify the tape widths actually available.
- Error mapping must use supported IPP reasons. Preserve raw error information in diagnostics where device-specific meanings are unverified; do not invent a ready state to avoid uncertainty.

## Migration Plan

1. Implement locally in the ordered tasks, beginning with regression coverage and bounded protocol probes before committing to the production deferred-finalization sequence. Do not alter the running printer configuration as part of implementation.
2. Run full container/core/capture and CUPS integration validation against an isolated sink; record software evidence separately from physical evidence.
3. With an instruction authorizing physical validation, run the PT-2730 gates for late finalization and final-only cutting, then the integrated four-preset/continued-batch/recovery matrix. Revise an unsuccessful candidate before claiming the required presets work; do not silently drop a mode or replace unknown-count streams with a private count requirement. Leave unavailable tape-width certification explicit.
4. Update README and supersede ADR details for the four presets, mandatory precut, batch lifecycle, geometry, MIME routing, state ownership/recovery, and logging. Review compatibility changes and verify all capabilities.
   Document that existing CUPS queues may carry a cached or explicitly configured none default; refresh their standard queue defaults when adopting trim. The server must still honor an explicit none sent by those clients.
5. Sync the delta specs, validate, and archive only when implementation and required validation are complete. Keep commits/branches local until then.
6. Publishing or changing the deployed service requires a separate instruction. For an authorized rollout, retain the previous immutable image and quiesce any explicit unfinished no-cut run before restart; rollback restores that image, not ambiguous physical tape state.

## Review corrections

Concrete collection media-size-name participates in image length precedence and
raster contradiction checks just like its resolved dimensions. Idle status skips
active printing and rechecks a busy OpenDevice failure before publishing offline.
Custom width ranges admit only ±0.01 mm around each nominal tape width, retaining
submitted geometry and the unchanged physical printable window. CUPS 2.4's PPD
represents all custom tape widths with one maximum margin: preserve those explicit
margins and reject oversized content; do not invent a client protocol, override
margins, or resize content. Correct per-width margins remain available through
named presets and standard media-col submissions.
