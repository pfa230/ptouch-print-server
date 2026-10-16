# Spec Delta

## MODIFIED Requirements

### Requirement: Explicit continuation survives idle time

On a healthy matching-tape printer, the server SHALL preserve a deliberately unfinished run across idle time and subsequent batches. A later batch's mandatory precut SHALL separate held user content without another discarded leader, or separate the remaining blank tail after explicit release. Its own finishing preset SHALL determine its internal and final cuts. Cleanup SHALL NOT resend label content.

#### Scenario: Idle gap between jobs
- **WHEN** an unfinished batch completes and another valid batch arrives after an idle gap
- **THEN** the open run is preserved without an idle cut or printer reset, and the next batch's precut separates held user content without discarded leader waste, or separates the remaining blank tail after explicit release

#### Scenario: Terminal trim job
- **WHEN** one or more unfinished batches are followed by a trim job without an intervening explicit release
- **THEN** the server continues the same tape run, applies the new batch's precut and internal cuts, and finishes its last label without an extra blank label

#### Scenario: Missing terminal job
- **WHEN** a terminal batch is never submitted, canceled, or rejected before printing
- **THEN** idle cleanup does not cut or reset the preceding healthy unfinished run

#### Scenario: Consecutive batches without internal cuts
- **WHEN** two batches request none without an intervening explicit release
- **THEN** labels remain joined within each batch, while the second batch's precut separates the first batch's strip without generating a discarded leader

### Requirement: Precut is mandatory batch preparation

The server SHALL prepare the first label of every nonempty batch with precut, including no-label-cuts and final-only batches. Precut SHALL NOT enable cuts between later labels when that preset forbids them. A healthy continuation SHALL use the existing tape boundary to separate held user content without producing discarded leaders between batches, or separate the remaining blank tail after explicit release. A new closed run can require one initial leader.

#### Scenario: Fresh batch
- **WHEN** a nonempty batch begins on a closed run with any preset
- **THEN** its initial leader is cut off before the first label

#### Scenario: Continuing batch
- **WHEN** a nonempty batch follows a healthy unfinished run
- **THEN** precut separates prior held user content without creating a discarded inter-batch leader, or separates the remaining blank tail after explicit release

#### Scenario: Mandatory precut does not imply cut-each
- **WHEN** a three-copy PNG requests none or trim-after-job
- **THEN** precut applies to the first copy only and no cuts are introduced between the copies
