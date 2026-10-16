# Spec Delta

## MODIFIED Requirements

### Requirement: Public IPP printer application

The server SHALL use a dedicated PAPPL application to expose one driverless IPP queue. PAPPL SHALL own job scheduling, standard image placement, and public IPP handling. Printer-specific transport, geometry, and finishing SHALL remain inside the application without requiring clients to implement a private printing protocol. An implementation-defined Ptouch-Cut printer operation SHALL provide an explicit device action independent of print-job submission; ordinary printing SHALL continue to use standard IPP operations and attributes.

#### Scenario: Ordinary client

- **WHEN** a CUPS or IPP client discovers the printer and submits a supported standard format
- **THEN** it uses the advertised IPP capabilities without a Brother-specific client transport

#### Scenario: Cut without submitting a print job

- **WHEN** an extension-aware IPP client requests Ptouch-Cut on a supported printer
- **THEN** the application handles the action without creating a job, filtering a document, or requiring Brother bytes from the client, even though the device action internally uses a white raster row

### Requirement: Healthy continuation

The queue SHALL own closed, healthy-open, and uncertain run state with media confirmation, distinguishing held user content from the blank continuation after explicit release. Only a known closed run SHALL be initialized. Healthy unfinished runs SHALL survive idle gaps; subsequent mandatory precut SHALL separate held user content without another discarded leader, or separate the remaining blank tail after explicit release. Rejected or canceled followers that sent no page output SHALL preserve the preceding healthy run. A successful explicit cut SHALL leave a healthy blank continuation that makes repeated cuts no-ops.

#### Scenario: Consecutive no-cut batches

- **WHEN** a healthy none batch is followed after an idle gap by another none batch
- **THEN** each batch remains a joined strip, separated by the later precut without discarded inter-batch leader waste

#### Scenario: Print after explicit cut

- **WHEN** a held label is released by a blank continuation and another valid print batch arrives
- **THEN** that batch continues without initialization or user-raster replay, uses its normal mandatory precut to separate the blank tail, and becomes eligible for a later explicit cut if its own user content is left held

#### Scenario: Rejected follower after explicit release

- **WHEN** a following job is rejected or canceled before page output while only the blank continuation is held
- **THEN** it preserves the healthy continuation and repeated Ptouch-Cut requests still emit no feed or cut output

## ADDED Requirements

### Requirement: Cut action reuses printer ownership and recovery

The cut action SHALL reuse exclusive printer ownership, checked transfers, bounded status handling, and the existing run-state recovery model. It SHALL NOT create a second scheduler, synthetic IPP job, persisted request, or saved user raster. A run SHALL be recorded as a healthy blank continuation only after the action's transfer and current-page receiving-ready notification are confirmed.

#### Scenario: Ambiguous explicit release

- **WHEN** an action has started output but completion is not confirmed
- **THEN** uncertainty blocks further cut and print output until the documented recovery, without automatic replay
