## MODIFIED Requirements

### Requirement: Selected model controls the print protocol

The server SHALL send the configured model's implemented USB print protocol. PT-2730 SHALL retain its legacy command and raster encoding. PT-P710BT SHALL use normal 180 dpi, 128-dot TIFF PackBits raster, raster-mode setup, and extended print information with loaded tape width and actual transmitted print-area row count, counting its existing end feed inside total page length. The page indication SHALL preserve the physical printer run across IPP batches without requiring a total label count.

#### Scenario: P710BT page setup
- **WHEN** a valid page begins on a selected PT-P710BT
- **THEN** the output selects raster mode and automatic status notification, specifies that page's tape width and transmitted row count using the extended print-information command, selects TIFF PackBits, and sends complete 128-dot raster lines

#### Scenario: Different page lengths in one batch
- **WHEN** a PT-P710BT receives a streaming batch with complete pages of different heights and no declared total page count
- **THEN** each page's print information contains its own transmitted print-area height and appropriate physical-run page indication, and normal end of input determines finalization

#### Scenario: Healthy P710BT continuation
- **WHEN** a PT-P710BT batch follows a healthy unfinished matching-tape run
- **THEN** page setup preserves the physical run and its held end, using a continuation-compatible page indication rather than treating the IPP batch boundary as a fresh printer run

#### Scenario: PT-2730 regression
- **WHEN** an existing PT-2730 job is submitted after the P710BT length correction
- **THEN** its page setup and raster bytes remain unchanged in the PT-2730 encoding and contain no P710BT-only setup

#### Scenario: P710BT status failure
- **WHEN** the selected PT-P710BT reports an error or does not confirm receiving-ready after output
- **THEN** the job fails without replay or invented finalization, and uncertain output remains blocked under the existing recovery contract

#### Scenario: P710BT feed counted once
- **WHEN** a P710BT rendered page has N logical rows with blank 14-row strips at both ends
- **THEN** exactly N minus 28 rows are declared and transmitted, with the existing 14-dot end feed replacing those blank strips inside the N-row page
