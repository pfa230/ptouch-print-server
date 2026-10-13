# Design

## Context

See [proposal.md](proposal.md) for motivation and the capability deltas for the behavioral contract. The implementation baseline is refactor commit `0603578` (#47), with the current architectural constraints consolidated in `openspec/specs/server-architecture/spec.md`. The older ADR subtree has been retired.

[verified] `src/device_usb.c` accepts only the PT-2730 name and exact URI; discovery/opening select its PID directly. `src/main.c` builds its identity for both real USB and socket overrides, and `src/driver.c` repeats the identity restriction and PID-specific status count. Adding a device-table row alone cannot enable P710BT support.

[verified] The current print callbacks deliberately defer each complete label's print command until the next actual page or successful end-job. They call the legacy raster-mode and eight-byte print-information builders and send uncompressed rows. `pt_printer_state` belongs to the queue; `pt_batch` has constant-size policy/lifecycle state. Page height is already known at `r_startpage`, including streamed jobs with an unknown total number of labels.

### Prior P710BT evidence

The USB probe ran on `vault` through the personal user's SSH connection on 2026-10-12. It selected only `04f9:20af`, queried clean loaded media, and sent normal 180 dpi data. Each label had 180 rows, explicitly selected TIFF PackBits, complete 16-byte head lines, autocut, chain-off, and a terminal EJECT. Status reported print-completed followed by receiving-ready with no errors.

| Loaded tape | Separately printed rectangle widths | Operator observation | Existing imageable width retained |
| --- | --- | --- | --- |
| 9 mm | 50, 51, 52 dots | "all 3 printed fine" | 52 dots |
| 12 mm | 74, 75, 76 dots | "all 3 printed just fine" | 76 dots |
| 18 mm | 112, 116, 120 dots | "all 3 printed fine" | 120 dots |

The first 76-dot fixture used intentional dashed reference lines and edge steps; its appearance was not a continuous-rectangle failure. The later three continuous rectangles are the relevant 12 mm observation. These sessions establish usable tested windows, not the absolute largest possible width or integrated batch/recovery behavior. The temporary probe initialized each separate label and therefore did not validate cross-job continuation or late chain-off.

The source/captures/logs are on the local development VM, hostname `openclaw`, at `/tmp/p710bt-geometry-probe.py` and `/tmp/p710bt-{rectangles,9mm,18mm}/`; they are not paths on `vault`. Their existence and hashes were checked on 2026-10-13. This planning revision leaves the raw files there and explicitly accepts their temporary-storage lifetime until task 1.1 preserves them during apply. The durable planning record retains the operator's observations and hashes even if raw files disappear; missing files must be reported, not presented as retained captures. Their widest-label captures have SHA-256:

- 9 mm / 52 dots: `711d2337f43c23fcd3eea84dcf5527cc03ef11451c02336f2e1b820b0353e323`.
- 12 mm / 76 dots: `7eb5be170e12c0f7c574c725b998655efb6f5e6ba6791aedc4d98e1e6bf13718`.
- 18 mm / 120 dots: `bc07c514b4cbdf0978e42bb75e12d80e54a96a4dfe9ae6d2a63964718032b591`.

[Brother's raster reference](https://download.brother.com/welcome/docp100064/cv_pte550wp750wp710bt_eng_raster_102.pdf) supplies the P710BT USB identity, mode selection, extended page information, compression, and status definitions. Its narrower published print areas for these tapes differ from the demonstrated windows. The cause is undocumented; this design does not invent a firmware restriction or reduce successfully tested geometry. P710BT 6 and 24 mm physical geometry remains untested, with 32/128-dot values supported by documentation and software coverage.

## Goals / Non-Goals

**Goals:**

- Carry one validated model identity from configuration through queue creation, USB access, status, and printing.
- Add the smallest model-specific protocol boundary needed by P710BT while reusing canonical geometry, packing, PNG placement, and finishing state.
- Preserve PT-2730 default behavior and byte-level regressions.
- Produce separately attributable core, adapter/socket, and physical acceptance evidence.

**Non-Goals:**

- A generic printer plug-in framework, simultaneous model queues, automatic model fallback, serial-number selection, or support inferred from catalog flags.
- Bluetooth, higher-resolution mode, half-cut, 3.5 mm tape, wide-head printers, or unrelated dependency-patch changes.
- Production configuration, publishing, or deployment as part of this planning workflow.

## Decisions

### 1. Select the model explicitly and use it throughout the existing USB scheme

Extend the supported-model lookup to PT-2730 and PT-P710BT only. Keep absent/empty `PTOUCH_MODEL` selecting PT-2730. Add the P710BT metadata row with PID `20af`, 128 dots, 180 dpi, and the existing raster-mode/PackBits flags. Flags describe encoding; they do not grant support to other catalog entries.

Use small helpers for supported URI/model resolution and identity formatting. Discovery counts only the configured model; opening resolves the supported model encoded by the queue's URI. Queue creation rejects a conflicting explicit USB override, while socket overrides remain usable for either selected model without USB status operations. Store the immutable selected `pt_dev` in the printer-owned extension so registration, idle status, and page callbacks use the same model. Derive diagnostics and synthetic device IDs from it; never identify a P710BT as PT-2730.

Retain fresh libusb contexts, bounded bulk I/O, exactly-one matching VID/PID, and the guard before accessing opaque USB connection data. One P710BT plus one PT-2730 is not ambiguous; two units of the configured model are. Absence does not switch models.

Discovery also identifies a supported but unselected attached model and explains its PTOUCH_MODEL selection. This is especially useful when the default PT-2730 queue is offline while only P710BT is attached. Emit the diagnostic during discovery, not as repeated idle-poll noise, and never substitute that printer for the configured one.

Update the existing `probe` and USB `smoke` subcommands with the same selected identity, VID/PID, URI, and diagnostic text. `probe` stays enumeration-only. USB `smoke` intentionally sends initialization and can destroy a held run; document that it is used only on a known closed run and is not the read-only preflight for physical acceptance. Cover these subcommands using fake USB rather than adding physical smoke labels.

Alternative: add a table row and broaden discovery indiscriminately. Rejected because it leaves model-specific opening, status, and queue identity inconsistent and could expose unimplemented catalog models.

### 2. Add narrow protocol dispatch at page setup and raster output

Preserve the legacy PT-2730 builders and their byte sequences. Add pure P710BT command builders for extended print information and explicit compression/automatic-status selection; reuse the existing raster-mode and PackBits-line builders. Select the extended setup explicitly for the implemented P710BT path, rather than assuming every catalog model with a common flag uses it. No generalized profile framework is needed for two supported paths.

| Operation | PT-2730 | PT-P710BT |
| --- | --- | --- |
| Raster mode | Existing `ESC i R 01` | `ESC i a 01` |
| Page information | Existing eight-byte `ESC i c` | Thirteen-byte `ESC i z`: width, 32-bit little-endian page row count, first/subsequent indicator |
| Raster data | Existing uncompressed rows | Explicit `M 02`; 16-byte literal PackBits run per line |
| Automatic status | Existing behavior | Explicit notification enabled with `ESC i ! 00` |
| Feed margin | Existing behavior | Tested 14-dot normal-resolution margin |

The P710BT setup uses the tested width-valid/recovery flag `84`, unspecified media-kind field, actual tape width, zero fixed-length field, and reserved bytes zero. Take row count from the validated raster page height; do not use job totals, source PNG height before placement, or the default media length. Enlarge setup command buffers to accommodate the extended command.

Use page indication 0 for the first actual page of a freshly initialized closed printer run, and 1 for later pages or the first page of an IPP batch continuing a healthy open run. Compute this from actual-label progress and the prior run state, not the IPP batch boundary alone. The manual defines starting/other-page values; applying its other-page value to a held cross-batch run is a design assumption that the single-label probes did not test. Stages 2 and 4 explicitly verify it. If it fails physically, stop and revise the protocol decision before preparing another bounded trial; do not switch values or replay automatically. The spec requires continuation behavior rather than prescribing an unverified batch-reset rule.

A fresh P710BT run uses the tested invalidate/initialize preparation; healthy continuation omits both. Ensure raster mode and automatic notification for each batch and send extended information, normal-resolution mode, feed margin, and compression selection before that page's rows. The immutable model in queue state drives both setup and line encoding.

Do not send the unsupported cut-every-N or half-cut commands. Existing auto-cut, chain-on, FF, and late chain-off/EJECT are integrated through the current callbacks; their P710BT physical behavior is an acceptance item, not an assumption established by the single-label probe.

Alternative: merely change raster-mode flags or try the existing raw path. Rejected as the default design because it omits the extended page command and does not reproduce the complete path already demonstrated on this printer. Brother documents uncompressed output, but testing it is not necessary to implement the verified PackBits path.

### 3. Keep one geometry and one batch lifecycle

Retain `pt_tape_geometry`, the 16-byte raster packer, strict width/content checks, and published nominal dimensions with pixel-aligned margins. The observed P710BT 9/12/18 mm windows match the existing table. Both supported models advertise the existing 6/9/12/18/24 mm media; documentation distinguishes the available physical widths from software/documented ones.

Keep the current `pt_cut_policy`, pending-print lifecycle, mandatory first-label precut, and closed/open/uncertain state. Do not add a second P710BT batch planner, total-count metadata, or page buffer. Status parsing remains the existing 32-byte layout; tests include P710BT's actual replies and fragmented completion/ready notifications. Any failed write or unconfirmed output follows the same conservative recovery latch.

Alternative: fork the driver or use narrower model-specific margins from the manual. Rejected because the common rendering works on the tested printer, and a second lifecycle would duplicate the refactor's failure and continuation logic.

### 4. Verify each boundary separately

- Core: model lookup; exact legacy and extended commands; first/subsequent flags; page lengths; compression selection; PackBits decoding and all 128 pin positions. Keep all five geometry widths covered.
- Fake USB: default/explicit model, both models attached, absent selected model with an actionable unselected-model diagnostic, two same-model devices, renumbering, claim/enumeration errors, URI/identity mismatches, selected-model probe/smoke behavior, and socket guard behavior.
- Adapter/socket: parameterize the existing production-driver/PNG fixtures and runtime image smoke for both selected models, and run both selections in `.github/workflows/deploy.yml` before publication. This is the isolated IPP/socket image smoke, not the initializing USB `smoke` subcommand. Decode the P710BT wire by command lengths and compression mode. Cover four presets, mixed page heights, physical-run page indication across IPP batches, unknown total count, full-width 9/12/18 mm designs, explicit geometry, oversized/wrong tape rejection before setup, incomplete streams, cancellation, status faults, and no replay. Assert that PT-2730 never receives P710BT-only commands.
- Physical: use an isolated P710BT candidate on `vault` with a separately presented gate. The proposed gate contains **11 complete labels**, one incomplete-stream negative trial, wrong-media rejection, and status-only checks. Do not add uncounted helper prints or retries.

Proposed integrated matrix:

| Stage | Tape / input / policy | Complete labels | Observation |
| --- | --- | ---: | --- |
| 1 | 12 mm PNG copies, chain-only | 2 | Fresh leader, internal separation, last end held |
| 2 | 12 mm PNG copies, chain-and-final after an idle gap with USB present | 2 | Prior held end released, no discarded new leader, final end cut |
| 3 | 12 mm PNG copies, none | 2 | Fresh leader, joined strip held |
| 4 | 12 mm unknown-count PWG, final-only, two different page heights | 2 | Prior strip released, joined labels retained at their page lengths, one final cut |
| 5 | 12 mm incomplete PWG and blocked valid follower, then power-cycle/candidate restart and explicit full-width recovery PNG | 1 | No terminal command for incomplete input, no automatic replay; only the new 76-dot label prints |
| 5a | On a closed run, load/confirm 9 mm tape and submit a complete job explicitly requesting 12 mm media | 0 | Job aborts; only status requests are allowed, with no initialization, page setup, raster, feed, or cut; the next valid job remains usable |
| 6 | 9 mm full-width PNG queued while offline on a closed run, default trim | 1 | Queue resumes after reconnect/media confirmation; complete 52-dot rectangle at 1:1 |
| 7 | 18 mm full-width PNG, default trim | 1 | Complete 120-dot rectangle at 1:1 |

Use stage 2 for a 10-second healthy idle gap with USB and power continuously present, and the counted stage-6 label for queued offline recovery after stages 5/5a leave the run closed. Current idle status clears healthy-open state when USB disappears; this change does not promise preservation of a held end across disconnection or power loss. Cassette transitions reuse stages 5a/6/7. Record actual leader waste, cuts, orientation, frame sizes, job status, command bytes, and the operator's physical observations. The geometry-only sessions remain separate evidence.

[Brother's user guide](https://download.brother.com/welcome/docp100362/pt-p710bt_eng_ug_03.pdf) documents button-based shutdown and separately configurable USB/battery auto-off timers. Before the gate, confirm adequate charge/USB power, close competing mobile printing apps, and disconnect Bluetooth clients. Record the auto-off setting if available, and confirm that the stage-2 power LED and USB presence persist through the 10-second gap without changing printer settings. An unexpected shutdown invalidates that continuation trial and stops the gate; it is not evidence of a page-flag failure.

For stage 5, actually power off by holding the Power button until the Power LED goes out, then turn it on with a single press and restart only the candidate server. Unplugging USB is not a power-cycle of this battery-powered device. Do not double-press the button, which feeds/cuts tape. For stage 6, disconnect only USB while the already closed-run printer remains powered; verify the candidate reports unavailable, submit the counted job once, then reconnect USB and let that same queued job resume. Neither action adds a helper print or automatically resubmits the job.

PT-2730 must retain byte-identical print output for equivalent jobs across all four policies, including canonical rendering and command ordering. Those capture/adapter regressions are sufficient when emitted print bytes and physical policy are unchanged. If either changes, identify the difference and prepare a separately bounded PT-2730 physical check before claiming preservation; editing shared callback code alone does not impose that additional printing requirement.

All P710BT physical acceptance is through the integrated server. Do not extend the existing PT-2730 cut probe: its additional live path is unnecessary for this matrix, and the production callback/socket captures already supply protocol verification.

## Risks / Trade-offs

- [Single-label probes do not establish late finalization or healthy continuation] → Keep those behaviors as explicit integrated hardware acceptance; a failing policy blocks certification rather than silently dropping a preset.
- [Broader discovery accidentally drives recognition-only models] → Preserve the explicit two-model support gate and negative tests independently of protocol flags.
- [Extended page information disagrees with actual output after scaling or orientation] → Use the validated destination raster header and assert row counts against decoded captures for each format/page.
- [Reset/invalidate erases an unfinished P710BT run] → Apply fresh-run preparation only to confirmed closed state; continue healthy runs without reset, and preserve uncertain-state blocking.
- [Starting-page semantics during held cross-batch continuation are untested] → Use the explicit fresh-run/continued-run rule above, capture it in software, and verify it in counted stages 2/4; stop rather than silently trying an alternative if physical behavior disagrees.
- [Battery operation or auto-off confuses offline/recovery tests] → Define USB disconnection separately from button power-cycle, and verify uninterrupted power during the bounded idle gap.
- [6/24 mm have no P710BT physical observations] → Keep documentation explicit about software/documentation coverage and the widths actually inspected; do not invent an all-width hardware claim.
- [Temporary diagnostic evidence can disappear before apply] → The planning record names the development VM and intentionally records this retention limitation. Task 1.1 preserves the available files and hashes before implementation changes the probe; absent files are reported while the operator observations remain separately attributable.

## Migration Plan

1. Implement locally after an apply request. Complete core, container adapter, and runtime socket checks for both models. Prepare the exact physical fixtures/captures and tape budget before presenting the 11-label gate.
2. Run the authorized gate on an isolated candidate through pfa's SSH connection to `vault`. Verify the previous queue is idle and record its image/configuration; release the P710BT from any competing process only if needed. Keep the old service available where it exclusively owns PT-2730. Stop on any failure, retain evidence, and restore services after confirmed finalization/recovery.
3. Document the successful scope, sync the deltas, validate, and archive. No push, pull request, or merge before archival; publishing and rollout require separate authorization.
4. For an authorized rollout, use the existing container configuration on `vault` to select `PTOUCH_MODEL=PT-P710BT` and the tested image. Retain the previous image/configuration for rollback. Finish any held run before switching models; rollback to PT-2730 requires its explicit model selection and matching hardware, never an automatic fallback.
