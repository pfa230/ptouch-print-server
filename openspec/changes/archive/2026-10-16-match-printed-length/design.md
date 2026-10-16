# Design

## Context

See `proposal.md` for the reported regression. Before this change, the P710BT driver declares and transmits every logical page row and sends a 14-dot feed-margin command. PT-2730 does not use that command. The existing raster callbacks already receive the logical row index and full page height, pack each row, and handle incomplete input and uncertain output.

The stopped implementation contains work from the previous broad plan. This rescope replaces that plan; its checked tasks and earlier passing checks do not establish completion of the revised work.

## Goals / Non-Goals

Correct the P710BT's duplicate feed allowance in the shared rendered-page path. Preserve content and the existing rendering pipeline. Use the fixed feed value already required by the protocol, without a model-geometry framework, page buffer, row counter, or new job state.

Media publication and limits, PNG length/orientation/DPI rules, explicit scaling, JPEG placement, dependencies, finishing, and model aliases retain their existing contracts. This change does not promise physical lengths below the printer's hardware limits or introduce a new minimum-length policy.

## Decisions

### Replace blank rendered end rows with equivalent feed

Keep the logical page header at N rows. For P710BT, declare N minus 28 print-area rows and send the same number. Consume all N rendered/input rows, check that the first and last 14 have no printable ink, and omit only those blank rows from transmission. Keep the existing 14-dot feed command. PT-2730 continues transmitting all N rows.

Use the existing row index, page height, packed row, and failure path. A single fixed feed constant in the driver can replace its current literal; no new geometry fields or per-page state are needed. Existing height validation already keeps supported pages larger than the 28-row reservation.

For 36 mm at 180 dpi, 255 logical rows become 227 transmitted rows plus 28 feed dots. For 50 mm, 354 become 326 plus 28. Reconstructing the omitted blank strips around the transmitted rows must reproduce the original pixels and positions. Reducing the count without reducing the transmitted rows is inconsistent; unconditional removal can lose ink; shrinking the image changes its scale.

### Keep image placement and validate the resulting rows once

The PNG and PAPPL renderers continue producing the same full logical page. Do not publish new end margins, compact source images, change media headers, or introduce a special scaled-image path. End-row validation runs in the shared driver after existing image placement, so PNG, JPEG, PWG and URF output use the same correction. A page whose resulting ink reaches a replaced end row fails; do not move, resize, or crop it to make it pass.

This keeps existing PNG preflight for selected dimensions and cross-tape margins. It adds no separate promise of early end-row rejection for images. A trailing end-row violation can be discovered after transfer has begun; reuse the same no-normal-finishing and uncertain-output handling as other transfer failures. All declared trailing input rows must still be consumed before a page can complete.

### Verify the correction with existing fixtures

Add direct 36 mm PNG captures with explicit and omitted length, and rendered raster captures at 36 and 50 mm. Assert declared/transmitted row counts and reconstruct end feed to check total length and an internal marker's physical position. Keep the existing PT-2730 byte expectations and failure coverage. Exercise nonblank end rows and input truncated inside otherwise blank trailing rows using existing fixtures.

Adjust only existing P710BT fixtures/decoders affected by the corrected row count or required blank ends; retain their assertions. Do not add another harness, a new finishing/orientation/DPI matrix, an 86 mm joined-pair acceptance task, or baseline/candidate build machinery. Existing finishing and continuation suites continue to run.

Run the normal core checks and the full adapter suite once against the current pinned container dependencies, plus strict OpenSpec validation. Reuse available build images and caches. An unrelated existing harness defect is reported separately rather than repaired or made a new completion gate here.

### Software-only completion

The user explicitly excludes physical acceptance and will validate separately. Repository fixtures are the complete acceptance inputs. No live template, user-provided render, printer access, SSH, service change, physical record, or certification exercise is required. Report software evidence as software evidence. A brief README note is sufficient; no certification/configuration-document reconciliation or separate validation record is required.

## Risks / Trade-offs

- Ink in a reserved end row cannot be preserved within the same physical length. Fail visibly instead of discarding it; already-transferred output follows existing recovery.
- The commanded feed model has not been physically validated by this change. Physical validation belongs to the user and does not block software completion.
- The previous implementation is broader than this plan. Reconcile it during implementation, removing the superseded additions rather than layering another workaround on them. Preserve separately authorized agent-alias configuration.

## Migration Plan

Keep this rescope local. Implement the revised tasks, then let the loop's archive stage sync delta specs and archive after software checks and review. Do not restart the stopped loop as part of this planning request. No deployment is included.

## Cut Pass

Removed: new model-specific minimum lengths and boundary policies; geometry fields and model geometry helper; published end margins and PAPPL bridge changes; PNG end compaction and special scaling paths; new CUPS metadata work and unrelated harness repairs; expanded finishing/orientation/DPI/86 mm acceptance matrices; certification paperwork and separate validation records. Physical and live-template gates remain excluded by the user's decision.

Each retained item below names its authority and the simplest implementation; the last column explains why a smaller version fails.

| Retained item | Authority | Simplest version; failure if reduced further |
| --- | --- | --- |
| Existing 14-dot feed value | printer-device: Selected model controls the print protocol | Reuse one fixed driver value; changing feed to zero changes the existing printer protocol. |
| N minus 28 declared and transmitted rows | label-rendering: Page length includes P710BT end feed | Derive both from the logical page; changing only one leaves count and data inconsistent. |
| Blank end-row validation | label-rendering: Page length includes P710BT end feed | Inspect the existing packed row; skipping without checking can discard ink. |
| Existing row index | Existing server-architecture: Constant-size deferred batch lifecycle | Reuse the callback index; a new row counter duplicates it. |
| Existing logical page height | label-rendering: Page length includes P710BT end feed | Reuse the header height; a stored copy adds state that can disagree. |
| Existing failure and completion state | Existing label-rendering: Incomplete raster input fails visibly | Reuse current failure path and full-input completion; normal finishing could hide missing trailing input or lost ink. |
| Shared rendered-page correction | Existing server-architecture: Public IPP printer application | Correct the existing driver path once; per-format corrections duplicate the accounting. |
| Explicit-length 36 mm PNG capture | User's reported 36 mm template; label-rendering: Page length includes P710BT end feed | One existing PNG fixture; driver-only tests miss the reported image route. |
| Image-derived 36 mm PNG capture | Existing label-rendering: Explicit media geometry is authoritative | Reuse that fixture without a length; the explicit case alone does not exercise PNG-derived length. |
| 36 mm raster capture with marker reconstruction | label-rendering: Page length includes P710BT end feed | Extend the existing capture; row counts alone can miss shifted ink. |
| 50 mm raster capture | label-rendering: A second length | Reuse the same fixture at another length; one length can hide a hard-coded output height. |
| Nonblank-end failure case | label-rendering: Page length includes P710BT end feed | Extend an existing fixture; success-only checks do not establish content preservation. |
| Truncated trailing-row case | Existing label-rendering: Incomplete raster input fails visibly | Truncate one existing fixture within blank end rows; otherwise omitted input could be mistaken for successful feed replacement. |
| Existing PT-2730 byte expectations | printer-device: PT-2730 regression | Keep current assertions; successful job completion alone can miss changed legacy output. |
| Affected P710BT fixture/decoder updates | Existing regression coverage | Correct only blank-end inputs and feed-aware expectations; stale fixtures fail for geometry they no longer represent. |
| Core checks | Existing loop configuration | Run existing commands; removing them bypasses configured checks. |
| Pinned container adapter suite | Project apply guidance | Reuse the full current suite; core checks do not exercise PAPPL callbacks. |
| Strict OpenSpec validation | Repository OpenSpec workflow | Run the existing validator; unchecked deltas may not sync coherently. |
| Brief README behavior note | User's length correction; label-rendering: Page length includes P710BT end feed | Explain feed accounting and rejection; more documentation adds no behavior or evidence. |
| Task 1.1 | printer-device: Selected model controls the print protocol | Reconcile the existing driver correction and its direct raster/failure regressions together; leaving the superseded production additions preserves the over-scoped implementation. |
| Task 1.2 | label-rendering: Page length includes P710BT end feed | Verify the existing PNG route and adjust affected fixtures; a separate renderer or expanded matrix is unnecessary. |
| Task 2.1 | Project apply guidance and repository OpenSpec workflow | Run existing integration checks after the correction; compilation alone misses adapter behavior. |
| Existing check output as evidence | User's software-only validation decision | Report actual results; a separate committed evidence record duplicates them. |
| Previously authorized model aliases | User's explicit model-alias requests | Preserve existing tooling changes; reverting them would undo separately requested work. |
