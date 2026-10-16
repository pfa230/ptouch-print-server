# label-rendering Specification

## Purpose

Let ordinary CUPS and IPP clients print labels using standard document formats, media dimensions, and truthful capabilities without silent resizing or content loss.

## Requirements

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
- **THEN** no page setup or raster for that page reaches the printer until the loaded tape matches, as defined by "Wrong-tape jobs wait for matching tape"

#### Scenario: Page without explicit margins
- **WHEN** a PNG, PWG raster, or URF job without explicit media margins submits a page for one supported tape width while the printer's default media describes another
- **THEN** PNG preflight, image placement, and geometry validation use the published margins of the tape the page matches, not the default media's margins

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

### Requirement: Wrong-tape jobs wait for matching tape

When a job's first label is valid for a supported tape width other than the loaded one, the server SHALL wait, with no time limit, until the loaded tape matches. While waiting, the server SHALL send the printer only status requests, never initialization, page setup, or raster, and SHALL NOT mark the run's output uncertain; publishing a changed tape MAY close a healthy run as idle tape polling does. While waiting, the job SHALL stay processing with a message naming the needed and loaded widths, the printer SHALL report media-needed, and later jobs SHALL queue behind it. Cancellation SHALL end the wait. A status read that fails with an I/O error SHALL abort the job without print output; a status request that receives no reply SHALL NOT end the wait.

#### Scenario: Tape swapped while waiting
- **WHEN** a 12 mm label is submitted while 18 mm tape is loaded, and 12 mm tape is loaded later
- **THEN** the job reports "Waiting for 12 mm tape; 18 mm loaded" and media-needed until the swap, then prints the label normally on 12 mm tape, for PNG, PWG raster, and URF submissions alike

#### Scenario: Cover open or no cassette during the swap
- **WHEN** a waiting job sees a printer fault or no cassette before the matching tape appears
- **THEN** the job keeps waiting and sends the printer only status requests

#### Scenario: Canceled while waiting
- **WHEN** a waiting job is canceled
- **THEN** it ends canceled, has sent the printer only status requests, and the next queued job starts

#### Scenario: Printer status lost while waiting
- **WHEN** a status read fails with an I/O error during the wait, including on a transport without a status path
- **THEN** the job aborts with a message saying the loaded tape could not be read, and no print command is sent to the printer

#### Scenario: No status reply while waiting
- **WHEN** a status request during the wait receives no reply
- **THEN** the job keeps waiting

#### Scenario: Geometry matching no supported tape
- **WHEN** a page width matches no supported tape width, or its resolution or length is unsupported
- **THEN** the job fails before page setup without waiting

#### Scenario: Later label for another width
- **WHEN** a later label in the same job has a different width than the tape its first label printed on
- **THEN** the job fails before that label's page setup without waiting, and labels already printed are not replayed
