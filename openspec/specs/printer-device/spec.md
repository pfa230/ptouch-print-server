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

The server SHALL expose one application queue and consistently select its configured supported model. Multiple matching physical devices SHALL be reported as ambiguous; discovery or opening SHALL NOT silently choose the first match. USB renumbering of a single matching device SHALL NOT require queue replacement. PT-2730 SHALL match USB identity 04f9:2041 and PT-P710BT SHALL match 04f9:20af; another attached model SHALL NOT substitute for the selected one. These USB selection rules SHALL apply when no Bluetooth address is configured.

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

### Requirement: PT-P710BT can use Bluetooth instead of USB

When `PTOUCH_BT_ADDRESS` is set, the server SHALL reach the selected PT-P710BT only over Bluetooth at that address and SHALL NOT use or fall back to USB. It SHALL send the same print protocol as over USB, hold the connection only while jobs are queued or an idle status read runs, and treat an unreachable printer as offline. An address with another model, or a malformed address, SHALL fail startup.

#### Scenario: Bluetooth printer in range
- **WHEN** PT-P710BT is selected with a Bluetooth address and the paired printer is on and in range
- **THEN** jobs print with the same command and raster bytes the USB path would send

#### Scenario: Connection released when idle
- **WHEN** two jobs are queued together and the queue then becomes idle
- **THEN** both jobs use one connection, and the connection closes once no job remains

#### Scenario: Printer unreachable
- **WHEN** a job is queued while the configured printer is off, out of range, or connected to another Bluetooth client
- **THEN** the queue reports the printer unavailable, the job waits without failing, and it prints after the printer becomes reachable and media is confirmed

#### Scenario: USB printer also attached
- **WHEN** a Bluetooth address is configured and a PT-P710BT is also attached over USB
- **THEN** the server sends no status request or output over USB

#### Scenario: Address with an unsupported model
- **WHEN** a Bluetooth address is configured and the selected model is PT-2730 or the address is malformed
- **THEN** startup fails with a configuration error naming the problem

#### Scenario: Link lost during output
- **WHEN** the Bluetooth link fails after page raster has started, including during finalization
- **THEN** the existing uncertain-output recovery contract applies, cleanup sends nothing further, and a following job emits no raster until recovery

### Requirement: Idle status polling is configurable

`PTOUCH_IDLE_POLL_SECONDS` SHALL control idle status reads on every transport. Absent, idle reads SHALL follow PAPPL's status requests as before. `0` SHALL disable idle device access. A positive integer SHALL set the minimum seconds between idle device access attempts, successful or not. Skipped reads SHALL keep the last published status. Job status reads SHALL be unaffected. Any other value SHALL fail startup.

#### Scenario: Polling unset
- **WHEN** `PTOUCH_IDLE_POLL_SECONDS` is absent
- **THEN** idle status behavior is identical to the behavior before this setting existed

#### Scenario: Polling disabled
- **WHEN** `PTOUCH_IDLE_POLL_SECONDS` is `0` and clients query the idle printer
- **THEN** the server opens no device connection, and the next job still confirms live media before printing

#### Scenario: Polling rate-limited
- **WHEN** `PTOUCH_IDLE_POLL_SECONDS` is `300` and clients query the idle printer every few seconds
- **THEN** the server reads live status at most once per 300 seconds and reports the last published status between reads

#### Scenario: Rate limit with the printer unreachable
- **WHEN** `PTOUCH_IDLE_POLL_SECONDS` is `300`, the printer is unreachable, and clients query the idle printer every few seconds
- **THEN** the server attempts at most one connection per 300 seconds

#### Scenario: Invalid polling value
- **WHEN** `PTOUCH_IDLE_POLL_SECONDS` is negative, fractional, or not a number
- **THEN** startup fails with a configuration error
