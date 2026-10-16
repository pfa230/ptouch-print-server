# Spec Delta

## MODIFIED Requirements

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

## ADDED Requirements

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
