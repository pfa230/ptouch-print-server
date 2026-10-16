# Spec Delta

## MODIFIED Requirements

### Requirement: Public IPP printer application

The server SHALL use a dedicated PAPPL application to expose one driverless IPP queue. PAPPL SHALL own job scheduling, standard image placement, and public IPP handling. Printer-specific transport, geometry, and finishing SHALL remain inside the application without requiring clients to implement a private printing protocol. An implementation-defined Ptouch-Cut printer operation SHALL provide an explicit device action independent of print-job submission; ordinary printing SHALL continue to use standard IPP operations and attributes.

#### Scenario: Ordinary client

- **WHEN** a CUPS or IPP client discovers the printer and submits a supported standard format
- **THEN** it uses the advertised IPP capabilities without a Brother-specific client transport

#### Scenario: Cut without submitting a print job

- **WHEN** an extension-aware IPP client requests Ptouch-Cut
- **THEN** the application handles the printer action without creating a job, passing through a document filter, or requiring Brother bytes from the client

### Requirement: Healthy continuation

The queue SHALL own closed, healthy-open, and uncertain run state with media confirmation. Only a known closed run SHALL be initialized. Healthy unfinished runs SHALL survive idle gaps; subsequent mandatory precut SHALL separate the held end without another discarded leader. Rejected or canceled followers that sent no page output SHALL preserve the preceding healthy run. A successful explicit cut SHALL close the run; the next print batch SHALL start a fresh run.

#### Scenario: Consecutive no-cut batches

- **WHEN** a healthy none batch is followed after an idle gap by another none batch
- **THEN** each batch remains a joined strip, separated by the later precut without discarded inter-batch leader waste

#### Scenario: Print after explicit cut

- **WHEN** a healthy held run is successfully cut and another valid print batch arrives
- **THEN** that batch starts from closed-run initialization and normal mandatory precut, without replaying content from the prior run

## ADDED Requirements

### Requirement: Cut action reuses printer ownership and recovery

The cut action SHALL reuse the application's exclusive printer connection, checked transfers, bounded status handling, and closed/open/uncertain state. It SHALL NOT introduce a second scheduler, synthetic print jobs, persisted cut requests, or raster storage. A run SHALL become closed only after the cut's transfer and completion are confirmed.

#### Scenario: Ambiguous standalone cut

- **WHEN** a cut has started but completion is not confirmed
- **THEN** the existing uncertain state blocks further cut and print output until the documented recovery, without automatic retry
