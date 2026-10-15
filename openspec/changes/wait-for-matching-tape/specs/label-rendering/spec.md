# Spec Delta

## MODIFIED Requirements

### Requirement: Advertised tape media is usable

The server SHALL advertise supported nominal tape widths and publish their actual imageable margins through media-ready and media-default after confirmation. Every advertised tape width SHALL be accepted by media publication and printing. Geometry validation SHALL use the physical tape's imageable window, rather than a different tape's cached margins.

#### Scenario: All advertised widths
- **WHEN** each advertised 6, 9, 12, 18, or 24 mm tape is reported loaded
- **THEN** media publication succeeds with that width and its correct margins

#### Scenario: Wrong tape
- **WHEN** a submitted page width differs from the loaded tape
- **THEN** no page setup or raster for that page reaches the printer until the loaded tape matches, as defined by "Wrong-tape jobs wait for matching tape"

#### Scenario: Raster page without explicit margins
- **WHEN** a PWG raster or URF job without explicit media margins submits a page for one supported tape width while the printer's default media describes another
- **THEN** geometry validation uses the published margins of the tape the page matches, not the default media's margins

#### Scenario: CUPS custom-size rounding
- **WHEN** CUPS rounds a custom nominal tape width by at most 0.01 mm
- **THEN** the advertised width range accepts the request while preserving its submitted geometry, explicit margins, and physical imageable window

#### Scenario: Conservative CUPS custom margins
- **WHEN** a custom-size request contains larger explicit margins than the tape requires
- **THEN** fitting content prints at 1:1 and oversized content fails before raster, without replacing the margins or shrinking the image

## ADDED Requirements

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
