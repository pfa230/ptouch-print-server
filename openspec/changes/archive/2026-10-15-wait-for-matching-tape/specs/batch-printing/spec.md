# Spec Delta

## MODIFIED Requirements

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
