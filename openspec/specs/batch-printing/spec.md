# batch-printing Specification

## Purpose

Let clients choose cutting for each label job through the printer's advertised finishing option, allowing explicit continuation without timing-based batch inference.

## Requirements

### Requirement: Job finishing controls cutting

The server SHALL support four presets: cuts between labels with the batch left open; cuts between labels plus a final batch cut; no label cuts with the batch left open; and only a final batch cut. Mandatory precut is separate batch preparation. `none` selects no label cuts and continuation; `trim` aliases cuts between labels plus the final cut. Queue contents, idle time, and legacy environment settings SHALL NOT choose or override finishing.

#### Scenario: Explicit no-cut job
- **WHEN** a valid job requests finishings none
- **THEN** mandatory precut prepares the first label, and the batch prints without cuts between its labels or a final cut, even if the queue becomes empty

#### Scenario: Explicit trim job
- **WHEN** a valid job requests finishings trim
- **THEN** its labels are cut apart using one chained run, and only the batch's last label receives final feed/cut, even if another job is queued

#### Scenario: Finishings collection
- **WHEN** a job selects a supported finishing template through finishings-col
- **THEN** it uses that template's advertised preset, and registered templates behave like their corresponding finishings enums

#### Scenario: Chain cuts without final cut
- **WHEN** a three-label batch requests the chain-only preset
- **THEN** its first two labels are separated and its last label remains attached without terminal feed/cut

#### Scenario: Final cut only
- **WHEN** a three-label batch requests trim-after-job
- **THEN** mandatory precut occurs once, the three labels remain joined, and one final cut releases the strip

### Requirement: Advertised defaults are honored

The server SHALL initialize finishings-default and finishings-col-default consistently to trim. An omitted job selection SHALL use one canonical default, updated through standard management of either representation. Defaults SHALL support the three presets with enum equivalents, with collection no-value for enum none. Chain-only and contradictory default updates SHALL be rejected without changing defaults. Explicit job selections SHALL override the default without hidden environment or queue-based policy.

#### Scenario: No finishing option
- **WHEN** the client omits finishings and the advertised default is trim
- **THEN** its labels are cut apart and the batch finishes using the advertised trim default

#### Scenario: No finishing option with none default
- **WHEN** the client omits finishings and the current advertised default has been changed to none
- **THEN** mandatory precut occurs, followed by no inter-label or final batch cuts, honoring the advertised default

#### Scenario: Explicit none overrides default
- **WHEN** the client explicitly requests none
- **THEN** mandatory precut remains, but no inter-label or final batch cuts are introduced by the trim default

#### Scenario: Consistent default updates
- **WHEN** an operator sets the collection default to trim-after-job and later sets the enum default to none
- **THEN** each update republishes consistent enum/collection defaults, omitted jobs use the latest preset, and none has a no-value collection default

#### Scenario: Chain-only is an explicit job choice
- **WHEN** an operator attempts to set ptouch-chain as the printer default
- **THEN** the update is rejected without changing the existing defaults, while an explicit ptouch-chain job remains supported

### Requirement: Explicit continuation survives idle time

On a healthy matching-tape printer, the server SHALL preserve a deliberately unfinished run across idle time and subsequent batches. A later batch's mandatory precut SHALL separate the preceding batch's held end without generating another discarded leader. Its own finishing preset SHALL determine its internal and final cuts. Cleanup SHALL NOT resend label content.

#### Scenario: Idle gap between jobs
- **WHEN** an unfinished batch completes and another valid batch arrives after an idle gap
- **THEN** the open run is preserved without an idle cut or printer reset, and the next batch's precut separates the held end without discarded leader waste

#### Scenario: Terminal trim job
- **WHEN** one or more unfinished batches are followed by a trim job
- **THEN** the server continues the same tape run, applies the new batch's precut and internal cuts, and finishes its last label without an extra blank label

#### Scenario: Missing terminal job
- **WHEN** a terminal batch is never submitted, canceled, or rejected before printing
- **THEN** idle cleanup does not cut or reset the preceding healthy unfinished run

#### Scenario: Consecutive batches without internal cuts
- **WHEN** two batches request none
- **THEN** labels remain joined within each batch, while the second batch's precut separates the first batch's strip without generating a discarded leader

### Requirement: Failure does not override the finishing choice

A detected output, raster decode, or printer-status failure SHALL fail the job without replaying content or inventing a final cut. Finalization SHALL succeed only after its requested terminal command is transferred and readiness confirmed. Ambiguous partial output SHALL block automatic continuation pending explicit recovery. Rejected or canceled jobs that sent no output SHALL preserve a prior healthy run.

#### Scenario: No-cut job canceled during printing
- **WHEN** a no-label-cuts job is canceled after printing starts
- **THEN** cleanup introduces no final cut and uncertain buffered output is not silently reused by later work

#### Scenario: Terminal transfer fails
- **WHEN** the requested final cut cannot be confirmed
- **THEN** the job reports failure and repeated cleanup does not resend terminal commands or label content

#### Scenario: Rejected follower
- **WHEN** a subsequent batch is rejected before sending any page setup or raster
- **THEN** it neither precuts nor damages the prior healthy unfinished run

#### Scenario: Truncated final label
- **WHEN** a streamed batch ends with an incomplete raster label
- **THEN** the job aborts, the incomplete label receives no normal print/feed or final-cut command, and uncertain output requires explicit recovery

### Requirement: Existing rendering is preserved

The server SHALL retain 1:1 default printing, strict tape-width matching, and offline media recovery as defined by label-rendering and printer-device. The finishing choice SHALL NOT change rendered pixels or page dimensions. Advertised formats and copy limits SHALL describe executable behavior. PT-P710BT hardware success SHALL require integrated physical acceptance. PT-2730 SHALL retain byte-identical print regressions; changed PT-2730 print bytes or physical command policy SHALL additionally require a separately bounded physical check before claiming preservation.

#### Scenario: Copies
- **WHEN** a valid PNG requests copies within its advertised supported range
- **THEN** all requested copies use the resolved finishing policy and the same image geometry

#### Scenario: Offline printer returns
- **WHEN** queued jobs resume after a printer returns with matching tape
- **THEN** media is confirmed before printing and each job retains its requested finishing choice

#### Scenario: P710BT finishing equivalence
- **WHEN** a PT-P710BT receives valid PNG-copy or multi-page raster batches using any of the four advertised finishing presets
- **THEN** mandatory first-label precut, internal cuts, held ends, final cuts, and healthy continuation match the existing batch-printing requirements

#### Scenario: Hardware claims require integrated evidence
- **WHEN** P710BT support is documented as hardware-certified
- **THEN** retained evidence includes real-printer observations of the integrated server's finishing, continuation, rendering, offline recovery, and uncertain-output recovery; direct geometry probes and socket captures alone do not establish that certification

#### Scenario: Unchanged PT-2730 output
- **WHEN** adding P710BT support leaves PT-2730's emitted print bytes and physical command policy unchanged
- **THEN** legacy capture and adapter regressions establish preservation without requiring new PT-2730 labels

#### Scenario: Changed PT-2730 output
- **WHEN** implementing support changes PT-2730's emitted print bytes or physical command policy
- **THEN** the change identifies that difference and verifies it through a separately bounded PT-2730 physical check before claiming preservation

### Requirement: Actual pages determine label boundaries

One IPP job SHALL define one batch. PNG copies and actual PWG raster or URF labels SHALL receive the selected batch policy without treating copies as document pages. Successful end of input SHALL determine the final label even when the total count is omitted. The server SHALL distinguish normal completion from abort cleanup and never infer batches from queued jobs or idle time.

#### Scenario: Two-page raster with one copy
- **WHEN** a valid two-page PWG raster job requests one copy and trim
- **THEN** both labels print in one chain, their shared boundary is cut, the final label is cut free, and the job completes successfully

#### Scenario: Unknown page count
- **WHEN** a valid streaming raster job omits its total page count
- **THEN** every complete label uses the requested policy, with the final feed/cut decided at successful end of the batch, without requiring custom count metadata

#### Scenario: No-cut multi-page job
- **WHEN** a valid multi-page job explicitly requests none
- **THEN** precut prepares only the first label, the labels remain joined, and the batch stays open for explicit continuation

### Requirement: Precut is mandatory batch preparation

The server SHALL prepare the first label of every nonempty batch with precut, including no-label-cuts and final-only batches. Precut SHALL NOT enable cuts between later labels when that preset forbids them. A healthy continuation SHALL use the existing tape boundary without producing discarded leaders between batches. A new closed run can require one initial leader.

#### Scenario: Fresh batch
- **WHEN** a nonempty batch begins on a closed run with any preset
- **THEN** its initial leader is cut off before the first label

#### Scenario: Continuing batch
- **WHEN** a nonempty batch follows a healthy unfinished batch
- **THEN** precut separates the prior held end without creating a discarded inter-batch leader

#### Scenario: Mandatory precut does not imply cut-each
- **WHEN** a three-copy PNG requests none or trim-after-job
- **THEN** precut applies to the first copy only and no cuts are introduced between the copies

### Requirement: Finishing presets use truthful IPP meanings

The server SHALL advertise none (3), trim (11), trim-after-pages (60), and trim-after-job (63). `trim` and 60 select cuts between labels plus the final cut; 63 selects only the final cut. Non-none enums SHALL have matching templates; collection no-value SHALL select none. Chain-only SHALL use the advertised implementation-defined `ptouch-chain` template through standard finishings-col. No private job attribute SHALL supply an independent final-cut switch.

#### Scenario: Registered selections
- **WHEN** a client selects none by enum or collection no-value, or trim-after-pages or trim-after-job by enum or template
- **THEN** it receives that advertised policy without silently collapsing the selection to none or trim

#### Scenario: Chain-only selection
- **WHEN** a client selects finishing-template ptouch-chain through finishings-col
- **THEN** cuts occur between labels while the final label remains attached

#### Scenario: Redundant registered combination
- **WHEN** a client requests finishings 60 and 63 together
- **THEN** all labels are cut apart with one final batch cut, without a duplicate terminal cut or a claim that these values are independent switches

#### Scenario: None alongside another finishing
- **WHEN** a client supplies enum none alongside supported finishing values
- **THEN** none is ignored as required by IPP and the remaining values determine the selected preset

#### Scenario: Conflicting or unsupported selection
- **WHEN** a client requests an unsupported preset, a conflicting preset combination, or both finishings and finishings-col
- **THEN** the request is rejected with an appropriate standard IPP error before any print output
