# Spec Delta

## Purpose

Let ordinary CUPS and IPP clients print labels using standard document formats, media dimensions, and truthful capabilities without silent resizing or content loss.

## ADDED Requirements

### Requirement: Explicit media geometry is authoritative

For image jobs, the server SHALL honor an explicitly selected IPP media size, media-col.media-size-name, or media-col dimensions. PNG auto-length SHALL apply only when the submitted attributes omit a concrete media length. A source-only or margin-only media-col SHALL NOT disable that fallback.

#### Scenario: Explicit PNG length
- **WHEN** a 180-pixel-long PNG at 180 dpi requests 50 mm media length
- **THEN** the printed page remains 50 mm long rather than being replaced with 25.4 mm

#### Scenario: PNG without length
- **WHEN** a valid PNG omits concrete media length
- **THEN** its length follows the along-tape pixel dimension and DPI, within the existing 5 to 300 mm limits

#### Scenario: Collection without dimensions
- **WHEN** a valid PNG supplies media-col with only a source or margins
- **THEN** image-derived length remains available

### Requirement: Raster pages retain their submitted geometry

The server SHALL use each PWG raster or URF page's submitted dimensions and resolution, validating them against supported media and the loaded tape. It SHALL NOT replace those dimensions with the default page, or silently pad or discard raster lines. Contradictory explicit job and page geometry SHALL fail visibly.

#### Scenario: Short raster page
- **WHEN** a valid raster page is 25.4 mm long and omits explicit IPP length
- **THEN** exactly that page length prints without padding to the 100 mm default

#### Scenario: Bit-depth independence
- **WHEN** equivalent supported 1-bit and 8-bit raster pages are submitted
- **THEN** conversion preserves their dimensions and content placement

### Requirement: Incomplete raster input fails visibly

The server SHALL reject short raster rows, incomplete declared page data, and malformed stream termination rather than completing the job with missing content. It SHALL withhold the affected label's normal print/feed or final-cut command, report job failure, and require recovery when already-transferred output is uncertain. Already printed labels SHALL NOT be replayed.

#### Scenario: Partial final raster label
- **WHEN** a raster stream stops before supplying all of its final label's declared rows
- **THEN** the job aborts and the partial label receives no normal finishing command

#### Scenario: Short row or malformed next header
- **WHEN** a raster row is incomplete or the next header is malformed
- **THEN** decode failure is not interpreted as successful end of the batch

### Requirement: Default 1:1 PNG rendering does not discard image content

With print-scaling none, the server SHALL reject PNG content that cannot fit the imageable area before sending page raster. It SHALL accept an imageable-area PNG that fits, or a nominal-width PNG canvas whose non-imageable margins are blank. It SHALL honor explicit orientation and retain the documented DPI fallback for PNGs without resolution metadata.

#### Scenario: Oversized source image
- **WHEN** a portrait 200 by 300 pixel PNG at 180 dpi is sent to 12 mm tape with print-scaling none
- **THEN** the job fails with a useful geometry error and no page raster is emitted

#### Scenario: Nominal-width canvas
- **WHEN** a correctly sized nominal-width image contains only blank pixels outside the tape's imageable window
- **THEN** its printable content is retained at the requested physical size

#### Scenario: Explicit page too short
- **WHEN** a PNG at 1:1 exceeds the explicitly requested page length
- **THEN** it is rejected rather than cropped

### Requirement: Advertised tape media is usable

The server SHALL advertise supported nominal tape widths and publish their actual imageable margins through media-ready and media-default after confirmation. Every advertised tape width SHALL be accepted by media publication and printing. Geometry validation SHALL use the physical tape's imageable window, rather than a different tape's cached margins.

#### Scenario: All advertised widths
- **WHEN** each advertised 6, 9, 12, 18, or 24 mm tape is reported loaded
- **THEN** media publication succeeds with that width and its correct margins

#### Scenario: Wrong tape
- **WHEN** a submitted page width differs from the loaded tape
- **THEN** the job fails before its raster reaches the printer

#### Scenario: CUPS custom-size rounding
- **WHEN** CUPS rounds a custom nominal tape width by at most 0.01 mm
- **THEN** the advertised width range accepts the request while preserving its submitted geometry, explicit margins, and physical imageable window

#### Scenario: Conservative CUPS custom margins
- **WHEN** a custom-size request contains larger explicit margins than the tape requires
- **THEN** fitting content prints at 1:1 and oversized content fails before raster, without replacing the margins or shrinking the image

### Requirement: Capabilities describe executable inputs

The server SHALL advertise only printable public document formats, with no private filter destination masquerading as an input. It SHALL honor its advertised format-specific copies range; PNG copies SHALL support 1 through 999, and streaming raster SHALL advertise one copy with multiple document pages supported.

#### Scenario: Format query
- **WHEN** a client queries document-format-supported
- **THEN** supported standard formats remain available and image/vnd.ptouch-raster is absent

#### Scenario: Former planner limit
- **WHEN** a valid PNG requests 65 copies
- **THEN** all 65 copies print without a planner-capacity failure
