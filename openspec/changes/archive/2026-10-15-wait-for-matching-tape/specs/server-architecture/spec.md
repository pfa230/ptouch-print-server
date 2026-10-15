# Spec Delta

## MODIFIED Requirements

### Requirement: Strict physical rendering

Wrong-tape page geometry SHALL NOT reach the printer as page setup or raster output; the job waits for matching tape or fails as defined by label-rendering. At print-scaling none, PNG content SHALL fit the selected page and physical window; a nominal-width canvas SHALL be accepted only when its non-imageable margins are blank. Explicit standard scaling SHALL use PAPPL placement. JPEG SHALL retain its standard selected/default media path.

#### Scenario: Content exceeds selected margins

- **WHEN** a 1:1 PNG exceeds the selected printable area, including larger explicit custom margins
- **THEN** the job fails visibly without automatic cropping, shrinking, or replacing those margins
