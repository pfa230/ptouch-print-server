## ADDED Requirements

### Requirement: Physical label length includes feed margins

The server SHALL interpret page length as the complete physical label length, including required feed margins, within one 180 dpi dot of conversion rounding. It SHALL NOT add feed margins outside that length or alter 1:1 content scale to make it fit. On joined strips, each page SHALL occupy its requested length; an initial discarded leader SHALL NOT count as part of a label.

#### Scenario: Reported 36 mm label
- **WHEN** a fitting 36 mm template is submitted to the PT-P710BT path with default finishing and captured without hardware
- **THEN** its declared print-area rows and feed margins reconstruct a 36 mm page within one 180 dpi dot of rounding, with unchanged content size and positions, rather than a roughly 40 mm page

#### Scenario: Image-derived physical length
- **WHEN** a PNG with a 36 mm along-tape canvas at its declared DPI omits explicit media length and its content fits the physical window
- **THEN** total label length follows that canvas without adding end margins

#### Scenario: Joined pages and continuation
- **WHEN** fitting pages of different lengths print using any supported finishing preset, including a later batch continuing a held run
- **THEN** each page occupies its own requested length and finishing does not introduce per-page length additions

## MODIFIED Requirements

### Requirement: Explicit media geometry is authoritative

For image jobs, the server SHALL honor an explicitly selected IPP media size, media-col.media-size-name, or media-col dimensions as total physical page dimensions, subject to the selected model's usable range. PNG auto-length SHALL apply only when the submitted attributes omit a concrete media length. A source-only or margin-only media-col SHALL NOT disable that fallback.

#### Scenario: Explicit PNG length
- **WHEN** a 180-pixel-long PNG at 180 dpi requests 50 mm media length and fits the physical window
- **THEN** the printed page remains 50 mm long rather than being replaced with 25.4 mm or extended by feed margins

#### Scenario: PNG without length
- **WHEN** a valid PNG omits concrete media length
- **THEN** its length follows the along-tape pixel dimension and DPI, within the existing 5 to 300 mm limits and the selected model's physical minimum; PT-2730 retains its existing below-5-mm padding

#### Scenario: Collection without dimensions
- **WHEN** a valid PNG supplies media-col with only a source or margins
- **THEN** image-derived length remains available

#### Scenario: P710BT length below physical minimum
- **WHEN** an explicit or image-derived P710BT page length is below 24.5 mm
- **THEN** the request fails visibly before page setup or raster output rather than producing a longer label

### Requirement: Raster pages retain their submitted geometry

The server SHALL use each PWG raster or URF page's submitted dimensions and resolution, validating them against supported media and the loaded tape. It SHALL NOT replace those dimensions with the default page, or silently pad or discard raster lines. Verified blank rows in the P710BT's physical end margins SHALL be represented by equivalent printer feed inside the page length. Contradictory explicit job and page geometry SHALL fail visibly.

#### Scenario: Short raster page
- **WHEN** a fitting valid raster page is 25.4 mm long and omits explicit IPP length
- **THEN** exactly that total page length prints without padding to the 100 mm default or adding end margins

#### Scenario: Bit-depth independence
- **WHEN** equivalent supported 1-bit and 8-bit raster pages are submitted
- **THEN** conversion preserves their dimensions and content placement

#### Scenario: Nonblank raster end margin
- **WHEN** a P710BT raster page contains ink in a required non-imageable end margin
- **THEN** the job fails without silently discarding that ink or normally finalizing the affected page; already-transferred uncertain output follows the existing recovery contract

#### Scenario: Incomplete blank end margin
- **WHEN** a streamed page omits declared trailing rows that would have been blank feed margins
- **THEN** it still fails as incomplete input without normal finishing

### Requirement: Default 1:1 PNG rendering does not discard image content

With print-scaling none, the server SHALL reject PNG content that cannot fit the physical imageable area before sending page setup or raster. It SHALL accept a fitting imageable-area PNG, or a full-page canvas whose non-imageable margins are blank, retaining total canvas length and content placement. Physical margins SHALL include the selected model's end margins. It SHALL honor explicit orientation and retain the documented DPI fallback for PNGs without resolution metadata.

#### Scenario: Oversized source image
- **WHEN** a portrait 200 by 300 pixel PNG at 180 dpi is sent to 12 mm tape with print-scaling none
- **THEN** the job fails with a useful geometry error and no page raster is emitted

#### Scenario: Nominal-width canvas
- **WHEN** a correctly sized nominal-width image contains only blank pixels outside the tape's imageable window
- **THEN** its printable content is retained at the requested physical size

#### Scenario: Explicit page too short
- **WHEN** a PNG at 1:1 exceeds the explicitly requested page length
- **THEN** it is rejected rather than cropped

#### Scenario: Blank P710BT end margins
- **WHEN** a full-page P710BT PNG has blank required end margins after alpha is composited over white
- **THEN** those margins are accounted for once inside the total length, with the original ink scale, orientation, and physical positions preserved

#### Scenario: Nonblank P710BT end margins
- **WHEN** a full-page P710BT PNG has nonblank content within a required end margin
- **THEN** preflight rejects it before page setup or raster instead of clipping, shrinking, or lengthening the label

### Requirement: Advertised tape media is usable

The server SHALL advertise supported nominal tape widths and publish actual imageable margins through media-ready and media-default after confirmation. Every advertised tape width SHALL be accepted by media publication and printing. Geometry validation SHALL use the physical tape's imageable window, rather than a different tape's cached margins. P710BT publication SHALL include its required 14-dot end margins and a 24.5 mm minimum physical page length; PT-2730 publication SHALL retain existing geometry and limits.

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

#### Scenario: Consistent model-specific media
- **WHEN** a client queries the P710BT's media database, default, ready media, supported margins, and custom length range
- **THEN** all describe the same physical end margins and usable length range, allowing an ordinary client to render a fitting page
