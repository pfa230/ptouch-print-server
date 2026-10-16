# Design

## Context

See `proposal.md` for the reported regression and scope.

[verified] `r_startpage` in `src/driver.c` declares `cupsHeight` as the P710BT print-area row count and sends `ESC i d 0e 00`. `r_writeline` then sends every logical page row. `media_col_for` supplies no top/bottom margins, and `pt_media_attrs` publishes both as zero. PT-2730 uses neither this margin command nor the extended row-count command.

[verified] The existing PNG filter already checks blank cross-tape canvas margins, preserves orientation/DPI, and derives omitted length from the original canvas. The production-driver and PNG IPP tests decode captured rows; `scripts/smoke-image.py` exercises the built image without USB. These are the existing integration points.

[verified] Brother's [P710BT raster reference](https://download.brother.com/welcome/docp100064/cv_pte550wp750wp710bt_eng_raster_102.pdf), sections 2.3.3–2.3.4 and the margin command description, specifies 14-dot minimum end margins at 180 dpi and a 24.5 mm minimum physical feed length. The earlier integrated acceptance record explicitly did not measure actual consumed tape. The user's 36-to-40-mm report agrees with 28 extra dots, but there is no capture or calibrated measurement of that specific job yet.

## Goals / Non-Goals

Keep logical page geometry separate from the printer's smaller print area, while sharing one definition of model-specific physical constraints. Use the current PAPPL callbacks and PNG preflight; retain constant-size batch state and existing failure handling.

No template names enter production code. No calibration knob, alternate protocol, dependency upgrade, new page buffer, finishing policy, or runtime fallback is needed. Bluetooth work in another worktree is outside this change.

## Decisions

### 1. Preserve logical page length and account for feed inside it

For a validated P710BT page containing N logical rows, keep the PAPPL page header at N and use a print-area height of N minus 28. Declare that reduced height in extended print information. Consume all N input rows, verify the first/last 14 rows are blank, and transmit only the intervening rows. Continue sending the existing 14-dot hardware margin command.

Thus a 36 mm page at 180 dpi becomes 255 logical dots: 227 transmitted rows plus 28 feed dots, approximately 35.98 mm. It must not become 255 transmitted rows plus 28 feed dots. A known internal marker retains its physical position because hardware feed replaces an equal number of blank logical rows.

Use the callback's existing row index and header height to identify boundary rows. Do not add a row counter or buffered page. The raster decoder must still consume and validate the trailing blank rows before `r_endpage` can make the page pending. Any nonblank boundary row or incomplete input fails through existing failure/recovery handling. For streaming raster, a trailing violation can occur after output has begun, so no normal finishing is sent and recovery can be required.

Alternatives: setting feed to zero conflicts with the documented minimum and has no retained physical evidence; merely reducing the declared row count leaves the actual transmitted raster inconsistent; blindly dropping rows loses ink; shrinking the complete image breaks 1:1 printing. None meets the contract.

### 2. Extend existing geometry and media publication

Keep the existing cross-tape table. Extend its model-aware use with the P710BT end-margin and minimum-length values; PT-2730 contributes zero new end reservation and retains its current minimum. Keep these values in one pure geometry definition used by publication, PNG preflight, and output extraction, without a generalized printer-profile framework or persisted fields.

Publish 14-dot P710BT end margins using the smallest integral hundredth-mm value that maps back to 14 dots at 180 dpi (198), including database/default/ready media and supported-margin attributes. Make the P710BT minimum physical length 2450 hundredths-mm, retaining the 300 mm maximum and existing valid presets. Reject shorter explicit and image-derived P710BT pages instead of silently padding them to another size. Keep existing PT-2730 5 mm PNG padding.

Honor larger submitted margins. Reject explicit margins that promise an imageable area outside the physical window; do not silently overwrite them. Update the existing PAPPL media bridge only to carry driver-supplied top/bottom supported margins alongside the existing left/right attributes, if its generated attributes would otherwise contradict publication. Keep its activation and legacy-driver behavior.

Alternative: hard-code a 4 mm subtraction only at output. That leaves clients rendering into advertised printable ends and makes geometry/input validation disagree with the actual printer. A configurable correction duplicates fixed protocol geometry without a calibration need.

### 3. Accept full-page PNG canvases only when physical margins are blank

Derive PNG automatic length from the original oriented canvas, before any blank-margin compaction. Extend the existing blank-margin preflight to both axes: accept content already fitting the selected imageable area, or compact verified white margins from a full-page canvas into that area before PAPPL placement. Validate after transparency is composited over white, using the existing source DPI/orientation rules. Account for larger selected margins without changing them.

At 1:1, reject nonblank required margins before page setup or raster. Compact only known page-margin strips, not an arbitrary ink bounding box, so asymmetric whitespace and content positions remain intentional. Cover both normal/reverse orientations and square DPI conversion. All length-derived header fields continue to describe the full logical page.

JPEG and explicit scaling continue through PAPPL placement using the truthful imageable area; output extraction accounts for feed exactly once for every format. Streaming raster uses the same physical window and validates the actual boundary rows as they arrive.

Alternative: only add advertised margins. That would reject every existing full-page PNG canvas even when its ends are already blank. Trimming any whitespace indiscriminately would change intentional positioning and image-derived length.

### 4. Reuse existing checks and measure complete labels

Extend the existing core/adapter/PNG/CUPS/image-smoke checks. Reconstruct the physical page in capture assertions as 14 blank dots, decoded print-area rows, and 14 blank dots. Compare both total length and internal pixels, rather than treating fewer transmitted rows as sufficient proof. Preserve PT-2730 byte-identical captures and keep incomplete-stream, wrong-tape, finishing, and recovery checks.

Use an isolated candidate and a bounded six-label physical check on 12 mm tape, after separate operator authorization:

| Stage | Input / finishing | Labels | Measurement |
| --- | --- | ---: | --- |
| 1 | Two copies of a fitting 36 mm PNG, default trim | 2 | Each cut label is 36 mm; internal reference span and both ends are intact. |
| 2 | PWG pages of 36 mm and 50 mm, final-only | 2 | Joined strip is 86 mm; markers identify the 36/50 mm page boundary without an extra 4 mm per page. |
| 3 | One fitting 36 mm PNG, chain-only | 1 | Hold the end; retain the run through a status-only idle interval. |
| 4 | One fitting 50 mm PNG, chain-and-final, continuing stage 3 | 1 | Release and measure the preceding 36 mm label and final 50 mm label without a new leader. |

Cover none with software captures so the physical gate ends closed without a helper print. There are no automatic retries or extra button feed/cuts. Intended complete length is 244 mm plus initial leaders; prepare at least 0.4 m of tape. Measure released lengths and the strip within ±0.5 mm using a ruler/caliper, and a known internal reference span to the same tolerance. Digital captures must satisfy the stricter one-dot conversion bound. Retain requested lengths, measured lengths, decoded row counts, source fixtures, and job outcomes together. Stop on any failure and record it without claiming physical success.

The six labels exercise the actual P710BT length change in separated, joined, and continued output. This does not rerun the entire previous hardware gate or certify previously untested tape widths. Use the unchanged PT-2730 capture baseline rather than printing unnecessary legacy labels.

## Risks / Trade-offs

- [Existing templates may put ink within 2 mm of an end] → Reject with a clear physical-window error; do not silently alter the template or crop it. The live `brother_12mm_item` operator template is not stored in this repository; implementation acceptance must inspect its rendered output when available, without adding its name as a special case.
- [Minimum feed and margin behavior could differ from the manual in chained output] → Measure the bounded integrated gate before claiming success. A failure requires revisiting this design, not an automatic alternative command policy.
- [Centimm, dot, and image-box rounding can disagree by a row] → Derive all boundaries from the same physical geometry and assert reconstructed pixels/lengths across orientations, DPI, minimum/maximum lengths, and explicit margins.
- [New end margins can be hidden by CUPS caches or PAPPL generated attributes] → Check the real CUPS media metadata and document refreshing an existing queue's cached media. Do not replace user margins at runtime.
- [Trailing raster ink is discovered after transfer begins] → Preserve no-finalization and recovery-required behavior; buffering an entire page solely to reject earlier is unnecessary.

## Migration Plan

Implement and verify locally under the apply workflow. Prepare the six-label gate with concrete fixtures and commands, then obtain authorization before any physical output. Access `vault` through pfa's SSH connection and use an isolated candidate; retain the original service image/configuration. Finish any healthy open run before changing services. Restore the original service after testing, and report whether the length and marker measurements passed.

Update README and acceptance documentation to distinguish total page length from transmitted print area and explain new P710BT fit/minimum-length rejection. Sync specs and archive only after required validation is complete. Keep work local until then; production rollout is separate from this planning request.

## Cut Pass

Removed the draft physical stage that would leave a final none page held and need an extra terminal job. The final stage now closes the run within the six-label bound; none remains covered by existing software checks. Rejected calibration settings, template offsets, a second print pipeline, added row counters, and a page buffer because the existing geometry, row index, and batch lifecycle suffice.

Each retained mechanism, check, record, and task has a concrete authority below. Requirement names refer to this change's deltas unless identified as an existing contract.

| Retained item | Authority | Simplest version and why a smaller one fails |
| --- | --- | --- |
| Model-specific end-margin value | printer-device: Selected model controls the print protocol | One fixed value through existing geometry; a shared nonzero reservation changes PT-2730 output. |
| Model-specific minimum-length value | label-rendering: Advertised tape media is usable | One physical minimum; retaining the 5 mm P710BT claim permits labels the documented feed limit lengthens. |
| Reduced transmitted height | printer-device: P710BT feed counted once | Derive N minus 28; declaring N repeats the reported length error. |
| Existing row index for extraction | server-architecture: Strict physical rendering | Reuse y and the header; a new counter or buffer duplicates available information. |
| Streaming blank-end check | label-rendering: Nonblank raster end margin | Check only replaced boundary rows; unconditional skipping loses ink. |
| PNG end preflight and compaction | label-rendering: Default 1:1 PNG rendering does not discard image content | Extend the existing blank-strip check; publication alone rejects valid full-page canvases and blind compaction loses ink. |
| Original-canvas length derivation | label-rendering: Image-derived physical length | Retain length before compaction; deriving it afterward shortens the requested page. |
| Truthful standard media publication | server-architecture: Canonical physical media | Extend the current media path and bridge only as necessary; zero published end margins invite incompatible client rendering. |
| Explicit-margin fit rejection | label-rendering: Conservative CUPS custom margins | Reuse fit errors; overwriting caller margins violates submitted geometry. |
| Existing pending/run/failure state | Existing batch-printing: Failure does not override the finishing choice | Reuse current state with no added fields; normal finishing after a boundary-row failure hides incomplete output. |
| Reconstructed length and pixel assertions | label-rendering: Physical label length includes feed margins | Extend current decoders; row-count-only checks can pass while moving or shrinking ink. |
| PT-2730 byte comparison | printer-device: PT-2730 regression | Compare existing captures; a test that merely completes can miss changed legacy bytes. |
| Existing CUPS and container/image checks | label-rendering: Consistent model-specific media; project config apply guidance | Reuse existing suites; core-only checks miss PAPPL publication and placement. |
| Six-label physical measurement | label-rendering: Reported 36 mm label; Joined pages and continuation | Six labels cover copies, mixed joined lengths, and continuation with a closed end; a single label misses boundary-dependent additions. |
| Acceptance record with fixtures, captures, lengths, marker spans, and outcomes | Existing batch-printing: Hardware claims require integrated evidence | Extend the existing record; captures alone cannot establish physical length or 1:1 scale. |
| Task 1.1 | server-architecture: Canonical physical media | One geometry change with boundary tests; separate per-callback constants can diverge. |
| Task 1.2 | label-rendering: Advertised tape media is usable | One media-publication change with CUPS checks/docs; output-only correction leaves false capabilities. |
| Task 2.1 | label-rendering: Default 1:1 PNG rendering does not discard image content | Extend existing preflight/tests/docs together; output-only trimming loses PNG ink or position. |
| Task 2.2 | printer-device: Selected model controls the print protocol | One callback correction with raster/legacy checks; changing the height alone leaves transmitted rows inconsistent. |
| Task 2.3 | Project config: adapter validation in the container | Extend the existing image smoke; a second harness duplicates it and omitting it misses deployed integration. |
| Task 3.1 | Project config apply guidance; printer-device: PT-2730 regression | Run required integration checks once after implementation; core compilation alone does not demonstrate adapter behavior. |
| Task 3.2 | User request: printed length matches template; existing hardware acceptance workflow | Prepare the bounded gate in the current workflow; improvised printing has no fixed fixtures or measurement expectations. |
| Task 3.3 | label-rendering: Physical label length includes feed margins | Measure the prepared labels and restore services; software captures cannot verify actual cuts and feed. |
| Task 3.4 | Repository OpenSpec lifecycle and cut-pass instructions | Reconcile and validate before syncing/archive; premature archival misstates incomplete physical verification. |
