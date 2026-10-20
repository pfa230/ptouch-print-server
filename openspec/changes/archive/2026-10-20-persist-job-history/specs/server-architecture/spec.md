## MODIFIED Requirements

### Requirement: Conservative output recovery

Ambiguous transfer, decode, or readiness failure SHALL block later output without automatic reset, feed, cut, or replay. Cleanup SHALL be idempotent and emit no pending finalization. Recovery SHALL require printer power-cycle followed by server restart and explicit resubmission after inspection. Clean status or USB reopen SHALL NOT clear uncertainty; printer run state SHALL remain process-local, independently of saved job history.

#### Scenario: Incomplete label

- **WHEN** raster input ends partway through a label that has already reached the printer
- **THEN** the job aborts without finishing that label and subsequent output remains blocked pending explicit recovery
