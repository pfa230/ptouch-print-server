# printer-device Specification

## Purpose

Expose one stable IPP queue for verified hardware, select its USB device predictably, and report availability and faults accurately across printer power cycles.

## Requirements

### Requirement: Only supported hardware receives print output

The server SHALL create operational queues only for implemented and verified model paths: PT-2730 and PT-P710BT. Recognizing a USB ID SHALL NOT grant print support. Invalid or unsupported model configuration SHALL fail explicitly rather than fall back silently. The default model SHALL remain PT-2730; PT-P710BT SHALL require explicit selection with `PTOUCH_MODEL=PT-P710BT`.

#### Scenario: Unsupported Brother attached
- **WHEN** discovery finds a model without an implemented print path
- **THEN** no PT-2730 or PT-P710BT raster is sent to that model and the recognized unsupported device is identified in diagnostics

#### Scenario: Invalid configured model
- **WHEN** the configured model is unknown or unsupported
- **THEN** startup fails with a useful configuration error

#### Scenario: Default remains compatible
- **WHEN** PTOUCH_MODEL is absent or empty
- **THEN** the queue selects PT-2730 and retains its existing identity and print behavior

#### Scenario: P710BT explicitly selected
- **WHEN** PTOUCH_MODEL is PT-P710BT
- **THEN** the stable queue identifies itself as PT-P710BT and uses the implemented PT-P710BT print path

#### Scenario: Recognized metadata does not enable another printer
- **WHEN** a recognized P-touch model other than PT-2730 or PT-P710BT is configured
- **THEN** startup rejects it even if its metadata shares protocol flags with a supported model

### Requirement: Device selection is unambiguous

The server SHALL expose one application queue and consistently select its configured supported model. Multiple matching physical devices SHALL be reported as ambiguous; discovery or opening SHALL NOT silently choose the first match. USB renumbering of a single matching device SHALL NOT require queue replacement. PT-2730 SHALL match USB identity 04f9:2041 and PT-P710BT SHALL match 04f9:20af; another attached model SHALL NOT substitute for the selected one.

#### Scenario: Two matching devices
- **WHEN** two units of the selected supported model match the configured queue
- **THEN** printing is blocked with an ambiguity diagnostic

#### Scenario: Device replug
- **WHEN** the sole matching printer is unplugged and reattached with another USB node number
- **THEN** the existing queue recovers without selecting a different model or requiring a server restart

#### Scenario: Both supported models attached
- **WHEN** one PT-2730 and one PT-P710BT are attached and either model is selected
- **THEN** discovery, opening, status, and printing consistently use only the selected model

#### Scenario: Selected model absent
- **WHEN** PT-P710BT is selected but only PT-2730 is attached, or the reverse
- **THEN** the selected queue remains present and unavailable, diagnostics name the attached but unselected model and the applicable PTOUCH_MODEL selection, and no output is sent to the other printer

#### Scenario: USB identity conflicts with queue identity
- **WHEN** an explicit supported USB URI names a different model from the configured queue identity
- **THEN** queue creation or opening fails before status or print commands are sent

### Requirement: Availability and faults are reported truthfully

The server SHALL retain the queue while hardware is absent and report standard IPP reasons for unavailability, missing tape, and printer faults. USB presence alone SHALL NOT establish readiness, and parsed printer error bits SHALL be checked before page raster output. Test transports SHALL NOT invoke USB operations on incompatible connection data.

#### Scenario: Present device cannot open
- **WHEN** USB enumeration finds the printer but the server cannot claim it
- **THEN** the queue does not report the printer as ready merely because USB is present

#### Scenario: Fault with recognized tape
- **WHEN** status reports a recognized tape and a printer error
- **THEN** that error is exposed and new page raster is withheld

#### Scenario: Socket test transport
- **WHEN** a socket sink is configured while Brother USB hardware is present
- **THEN** status processing never passes the socket connection to USB-specific operations

### Requirement: Offline recovery confirms media before printing

The server SHALL accept queued work while the printer is off and resume valid work after it returns. Image page geometry derived from loaded media SHALL be constructed only after live media confirmation. Explicit raster page geometry SHALL be validated against the confirmed tape rather than silently changed to match it.

#### Scenario: Start while off
- **WHEN** a PNG without explicit media geometry is queued while the server starts with the printer off, then the printer returns with supported tape
- **THEN** geometry uses that confirmed tape and the job can complete without a server restart

#### Scenario: Existing unfinished run
- **WHEN** a healthy chain-only or no-label-cuts run remains open during status polling or a subsequent valid job
- **THEN** cassette detection continues without a printer reset that destroys the unfinished run

### Requirement: Uncertain output requires explicit recovery

When interrupted output leaves the printer's buffered label or final-cut result uncertain, the server SHALL report recovery required and block new raster output. It SHALL NOT blindly reset, print, cut, or append to that buffer. The documented conservative recovery SHALL power-cycle the printer and then restart the server. A clean status response or USB reopen alone SHALL NOT clear the uncertainty.

#### Scenario: Interrupted label followed by another job
- **WHEN** raster transfer fails partway through a label and another job arrives
- **THEN** the next job emits no raster or automatic reset and the queue reports the required recovery action

#### Scenario: Explicit recovery
- **WHEN** the operator power-cycles the printer and restarts the server after uncertain output
- **THEN** a new job confirms media and starts a fresh run without replaying the interrupted content

#### Scenario: Rejected job before output
- **WHEN** validation rejects a job before page setup or raster is sent
- **THEN** a previously healthy unfinished run is not reclassified as uncertain merely because the job was rejected

### Requirement: Selected model controls the print protocol

The server SHALL send the configured model's implemented USB print protocol. PT-2730 SHALL retain its legacy command and raster encoding. PT-P710BT SHALL use normal 180 dpi, 128-dot TIFF PackBits raster, raster-mode setup, and extended print information with loaded tape width and actual page row count. The page indication SHALL preserve the physical printer run across IPP batches without requiring a total label count.

#### Scenario: P710BT page setup
- **WHEN** a valid page begins on a selected PT-P710BT
- **THEN** the output selects raster mode and automatic status notification, specifies that page's tape width and row count using the extended print-information command, selects TIFF PackBits, and sends complete 128-dot raster lines

#### Scenario: Different page lengths in one batch
- **WHEN** a PT-P710BT receives a streaming batch with complete pages of different heights and no declared total page count
- **THEN** each page's print information contains its own height and appropriate physical-run page indication, and normal end of input determines finalization

#### Scenario: Healthy P710BT continuation
- **WHEN** a PT-P710BT batch follows a healthy unfinished matching-tape run
- **THEN** page setup preserves the physical run and its held end, using a continuation-compatible page indication rather than treating the IPP batch boundary as a fresh printer run

#### Scenario: PT-2730 regression
- **WHEN** an existing PT-2730 job is submitted after P710BT support is added
- **THEN** its page setup and raster bytes retain the PT-2730 encoding and contain no P710BT-only setup

#### Scenario: P710BT status failure
- **WHEN** the selected PT-P710BT reports an error or does not confirm receiving-ready after output
- **THEN** the job fails without replay or invented finalization, and uncertain output remains blocked under the existing recovery contract
