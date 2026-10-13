# printer-device Specification

## Purpose

Expose one stable IPP queue for verified hardware, select its USB device predictably, and report availability and faults accurately across printer power cycles.

## Requirements

### Requirement: Only supported hardware receives print output

The server SHALL create operational queues only for implemented and verified model paths, initially PT-2730. Recognizing a USB ID SHALL NOT grant print support. Invalid or unsupported model configuration SHALL fail explicitly rather than fall back silently.

#### Scenario: Unsupported Brother attached
- **WHEN** discovery finds a model without an implemented print path
- **THEN** no PT-2730 raster is sent to that model and the unsupported device is identified in diagnostics

#### Scenario: Invalid configured model
- **WHEN** the configured model is unknown or unsupported
- **THEN** startup fails with a useful configuration error

### Requirement: Device selection is unambiguous

The server SHALL expose one application queue and consistently select its configured supported model. Multiple matching physical devices SHALL be reported as ambiguous; discovery or opening SHALL NOT silently choose the first match. USB renumbering of a single matching device SHALL NOT require queue replacement.

#### Scenario: Two matching devices
- **WHEN** two PT-2730 units match the configured queue
- **THEN** printing is blocked with an ambiguity diagnostic

#### Scenario: Device replug
- **WHEN** the sole matching printer is unplugged and reattached with another USB node number
- **THEN** the existing queue recovers without selecting a different model or requiring a server restart

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
