## MODIFIED Requirements

### Requirement: Canonical physical media

Media SHALL describe nominal 6/9/12/18/24 mm tape widths with 32/52/76/120/128 imageable dots at 180 dpi. One geometry definition SHALL govern publication, validation, and extraction, including model-specific end margins and minimum physical length. Per-tape margins MAY be asymmetric to reproduce the canonical window. Custom width ranges SHALL allow only ±0.01 mm rounding; submitted dimensions and margins SHALL remain authoritative. Required feed SHALL be accounted for inside physical page length.

#### Scenario: Standard media selection
- **WHEN** a client selects a named preset or a supported custom length
- **THEN** the published media describes its physical tape and model-specific margins without enlarging the loaded tape's imageable window

#### Scenario: P710BT physical length constraints
- **WHEN** P710BT media is published or validated
- **THEN** its required 14-dot end margins and 24.5 mm minimum physical page length agree across publication, image placement, and output extraction, while PT-2730 geometry and output remain unchanged

### Requirement: Strict physical rendering

Wrong-tape page geometry SHALL fail before page setup or raster output. At print-scaling none, PNG content SHALL fit the selected page and physical window; a full-page canvas SHALL be accepted only when its non-imageable margins are blank, including required model-specific end margins. Explicit standard scaling SHALL use PAPPL placement. JPEG SHALL retain its standard selected/default media path. Only verified blank end rows SHALL be represented by equivalent printer feed inside the declared page.

#### Scenario: Content exceeds selected margins
- **WHEN** a 1:1 PNG exceeds the selected printable area, including larger explicit custom margins or required end margins
- **THEN** the job fails visibly without automatic cropping, shrinking, or replacing those margins

#### Scenario: Streaming margin violation
- **WHEN** a raster page contains ink in an end row that physical feed would replace
- **THEN** output fails without silently dropping that ink or normally finalizing the affected page, retaining the existing uncertain-output recovery policy

### Requirement: Public PNG route and explicit length

PNG SHALL use the public image/png format, an owned filter and matching printfile callback, libpng decoding, and PAPPL's image renderer. Submitted named media, media-col.media-size-name, or concrete y-dimension SHALL preserve explicit total physical length. Only omitted length SHALL derive from the oriented image within 5–300 mm and the selected model's physical range; source-only or margin-only collections SHALL retain this convenience. P710BT image-derived lengths below 24.5 mm SHALL fail rather than be lengthened.

#### Scenario: Named collection length
- **WHEN** a fitting 180-pixel PNG at 180 dpi selects 50 mm through media-col.media-size-name
- **THEN** its total physical page remains 50 mm rather than receiving the image-derived 25.4 mm length or additional feed margins
