## ADDED Requirements

### Requirement: Page length includes P710BT end feed

For a supported P710BT page with blank required end rows, the server SHALL count its existing 14-dot feed per end inside the requested page length, within one 180 dpi dot of conversion rounding. It SHALL preserve rendered ink scale and physical position. Ink in a replaced end row SHALL fail visibly without silent cropping or normal page finishing; uncertain output SHALL follow existing recovery.

#### Scenario: Reported 36 mm PNG
- **WHEN** a fitting PNG requests a 36 mm P710BT page with blank end rows
- **THEN** 227 transmitted rows plus 28 feed dots reconstruct its 255-row logical page, with unchanged ink size and position, rather than adding approximately 4 mm

#### Scenario: Image-derived length
- **WHEN** the same fitting 36 mm PNG omits a concrete media length
- **THEN** length remains derived from its original canvas and DPI, and feed is counted inside that length

#### Scenario: A second length
- **WHEN** a fitting 50 mm P710BT page is rendered as 354 logical rows
- **THEN** 326 transmitted rows plus 28 feed dots reconstruct the same logical page and internal pixel positions

#### Scenario: Nonblank end row
- **WHEN** a rendered P710BT page has printable ink in a row that end feed would replace
- **THEN** the job fails without silently dropping that ink or normally finalizing the affected page, retaining existing uncertain-output recovery

## MODIFIED Requirements

### Requirement: Raster pages retain their submitted geometry

The server SHALL use each PWG raster or URF page's submitted dimensions and resolution, validating them against supported media and the loaded tape. It SHALL NOT replace those dimensions with the default page, or silently pad or discard raster lines. Verified blank P710BT end rows SHALL instead be represented by equivalent feed inside the same page length. Contradictory explicit job and page geometry SHALL fail visibly.

#### Scenario: Short raster page
- **WHEN** a valid fitting raster page is 25.4 mm long and omits explicit IPP length
- **THEN** exactly that page length prints without padding to the 100 mm default or adding end feed outside the page

#### Scenario: Bit-depth independence
- **WHEN** equivalent supported 1-bit and 8-bit raster pages are submitted
- **THEN** conversion preserves their dimensions and content placement

#### Scenario: Incomplete blank trailing rows
- **WHEN** a P710BT raster stream omits declared trailing rows that end feed would replace
- **THEN** it still fails as incomplete input and receives no normal finishing, retaining existing uncertain-output recovery
