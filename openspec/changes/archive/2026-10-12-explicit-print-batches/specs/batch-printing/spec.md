# Client-controlled label finishing

## Purpose

Let clients choose cutting for each label job through the printer's advertised finishing option, allowing explicit continuation without timing-based batch inference.

## ADDED Requirements

### Requirement: Job finishing controls cutting

The server SHALL use the job's resolved finishing option as its cut policy. `none` SHALL disable all cuts and leave the tape run open. `trim` SHALL cut the job's labels individually and finalize its last label. Queue contents and legacy cut-mode environment settings SHALL NOT override this choice.

#### Scenario: Explicit no-cut job
- **WHEN** a valid job requests finishings none
- **THEN** it prints without precut, intermediate cuts, or terminal cutting, even if the queue becomes empty

#### Scenario: Explicit trim job
- **WHEN** a valid job requests finishings trim
- **THEN** it cuts and finalizes its labels, even if another job is queued

#### Scenario: Finishings collection
- **WHEN** a job selects a supported none or trim finishing-template through finishings-col
- **THEN** it behaves like the corresponding finishings enum selection

### Requirement: Advertised defaults are honored

A job that omits its finishing selection SHALL use the printer's advertised finishing default. The server SHALL NOT substitute a hidden environment or queue-based cut policy.

#### Scenario: No finishing option with none default
- **WHEN** the client omits finishings and the advertised default is none
- **THEN** the job prints without cutting

### Requirement: Explicit continuation survives idle time

On a healthy matching-tape printer, the server SHALL preserve an unfinished run across subsequent jobs without resetting it or cutting due to elapsed idle time. A later trim job SHALL finalize its own final label using its explicit policy. No label content SHALL be resent during cleanup.

#### Scenario: Idle gap between jobs
- **WHEN** a no-cut job completes and another valid job arrives after an idle gap
- **THEN** the open run is preserved without an automatic cut or printer reset between jobs

#### Scenario: Terminal trim job
- **WHEN** one or more no-cut jobs are followed by a trim job
- **THEN** the server continues the existing run and finishes the trim job with a terminal cut without an extra blank label

#### Scenario: Missing terminal job
- **WHEN** a terminal job is never submitted, canceled, or rejected before printing
- **THEN** idle cleanup does not cut the preceding no-cut run

### Requirement: Failure does not override the finishing choice

A detected output or printer-status failure SHALL fail the job without replaying its content or inventing a terminal cut. Successful finalization SHALL only be recorded after the requested terminal command is transferred. If continuation state cannot be retained, a no-cut job SHALL fail before printing rather than cut implicitly.

#### Scenario: No-cut job canceled during printing
- **WHEN** a no-cut job is canceled after printing starts
- **THEN** cleanup does not introduce a terminal cut

#### Scenario: Terminal transfer fails
- **WHEN** the requested terminal cut cannot be transferred
- **THEN** the job reports failure and repeated cleanup does not resend the terminal command or label content

### Requirement: Existing rendering is preserved

The server SHALL retain advertised document formats, 1:1 PNG geometry, strict tape-width rejection, supported copy limits, and offline media recovery. The finishing choice SHALL NOT change rendered pixels or dimensions. Physical printing and cutting SHALL be verified on the PT-2730 before claiming the leader issue fixed.

#### Scenario: Copies
- **WHEN** a valid PNG requests supported copies
- **THEN** all requested copies use the same finishing policy and existing image geometry

#### Scenario: Offline printer returns
- **WHEN** queued jobs resume after a printer returns with matching tape
- **THEN** media is confirmed through the existing recovery path and each job retains its requested finishing choice
