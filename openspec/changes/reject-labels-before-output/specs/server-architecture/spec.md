## MODIFIED Requirements

### Requirement: Constant-size deferred batch lifecycle

Batch state SHALL contain the resolved policy, actual-label progress, one pending print command, and failure state without an operation array; P710BT validation SHALL use at most one packed page buffer bounded by the supported maximum page length, independent of copy or batch count. PNG copies SHALL expand once; raster labels SHALL need no declared total. A next valid label SHALL release the prior complete label with FF and confirm readiness. Successful input completion SHALL release the final label with FF for continuation or late chain-off/EJECT for finishing.

#### Scenario: Unknown label count

- **WHEN** a valid streamed raster batch omits its total count
- **THEN** actual complete labels determine boundaries and successful end-of-input determines finalization
