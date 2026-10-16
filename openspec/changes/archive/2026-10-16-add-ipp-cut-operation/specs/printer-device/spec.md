# Spec Delta

## ADDED Requirements

### Requirement: Direct IPP cut action

PT-P710BT queues SHALL advertise and accept project-local Ptouch-Cut (0x5f01) using the same printing authorization as Print-Job/Create-Job, evaluated before device acquisition. Any client allowed to print SHALL be allowed to cut, including remote clients when no authentication service or callback is configured; configured authentication and print-group checks SHALL apply equally to printing and cutting, without an administrator-only restriction. It SHALL release healthy held user content without a submitted document, IPP job, or replay. An internal one-white-row continuation is permitted; visible content SHALL NOT be printed. PT-2730 SHALL neither advertise nor implement the action. No user feed-length or force parameter SHALL be added.

#### Scenario: Release the last chain label
- **WHEN** an authorized client sends Ptouch-Cut to an idle P710BT holding the last label of a healthy chain
- **THEN** a minimum-raster blank continuation releases that label, leaves the blank page unfinished, and returns success only after checked transfer and current-page receiving-ready confirmation

#### Scenario: Release a joined strip
- **WHEN** Ptouch-Cut targets a healthy held strip printed with finishings none
- **THEN** it releases the strip without introducing cuts between its user labels or replaying their content

#### Scenario: Discover the extension
- **WHEN** a client requests operations-supported on P710BT
- **THEN** one complete attribute lists 0x5f01 alongside the standard operations, and documentation identifies it as a project-local extension

#### Scenario: Unsupported model
- **WHEN** a PT-2730 queue is queried or receives Ptouch-Cut
- **THEN** discovery omits 0x5f01, the action returns server-error-operation-not-supported without device output, and ordinary printing remains unchanged

#### Scenario: Remote client without authentication
- **WHEN** PAPPL classifies a client as a non-localhost network peer and no authentication service or callback is configured
- **THEN** both Print-Job and Ptouch-Cut are authorized under the same printing policy, without requiring administrator access

#### Scenario: Invalid or unauthorized request
- **WHEN** the request contains document data or job attributes, lacks a valid printer target, or fails the configured printing authorization shared with Print-Job/Create-Job
- **THEN** it is rejected with an appropriate IPP or HTTP error before device output

### Requirement: Direct cuts use exclusive device access

The action SHALL execute synchronously through the configured transport under exclusive device ownership. Failed acquisition, whether busy or unavailable, SHALL return server-error-busy without deferred execution. CLOSED or healthy blank-only runs SHALL succeed without feed/cut output once owned. Uncertain output SHALL be refused. Faulted or unusable live status on an acquired device SHALL return server-error-device-error.

#### Scenario: Printing or maintenance owns the device
- **WHEN** a cut cannot obtain ownership because printing, polling or another cut owns the device
- **THEN** it returns server-error-busy without adding a job or sending page output, and no action runs later on its behalf

#### Scenario: Repeated request after success
- **WHEN** only the healthy blank continuation remains after a successful release, or the run is known closed
- **THEN** the next owned cut request succeeds without new raster, feed or cut output, including after a lost successful response

#### Scenario: Printer cannot be opened
- **WHEN** the configured printer cannot be opened
- **THEN** the request returns server-error-busy with a busy-or-unavailable explanation, without a queued retry or transport fallback

#### Scenario: Live status is unusable
- **WHEN** ownership succeeds but an open run's clean loaded-media status cannot be confirmed
- **THEN** the action returns server-error-device-error without sending its blank continuation

#### Scenario: Media invalidates the held run
- **WHEN** live confirmation invalidates the preceding run because the cassette changed
- **THEN** the action rechecks that state and sends no blank continuation for the invalidated held end

#### Scenario: Output already uncertain
- **WHEN** earlier output has latched recovery required
- **THEN** Ptouch-Cut fails with the existing power-cycle-then-restart instruction and sends no page output

#### Scenario: Cut outcome becomes uncertain
- **WHEN** transfer is incomplete, status reports an error, or current-page receiving-ready cannot be confirmed
- **THEN** the request fails, the run remains uncertain, and repeated requests and cleanup emit no replay

### Requirement: Cut support reflects physical evidence

Documentation SHALL distinguish direct physical release evidence from software captures and integrated IPP checks. Initial direct proof SHALL identify PT-P710BT, Bluetooth and 12 mm tape, the white-row sequence, zero error bits and operator-confirmed release. USB/other-width captures SHALL NOT be described as physical certification. A successful write alone SHALL NOT establish cutting or measured tape consumption.

#### Scenario: Candidate has no physical effect
- **WHEN** successful command transfer leaves the label attached
- **THEN** it is recorded as a failed candidate rather than proof of a cut; the unsuccessful zero-raster candidates are distinguished from the successful blank-row mechanism

#### Scenario: Evidence retained for the selected mechanism
- **WHEN** documenting the selected blank continuation
- **THEN** retained evidence includes exact bytes, print-completed and receiving-ready statuses, and the operator's release confirmation, while stating that total feed and blank tail length were not measured

## MODIFIED Requirements

### Requirement: PT-P710BT can use Bluetooth instead of USB

When `PTOUCH_BT_ADDRESS` is set, the server SHALL reach the selected PT-P710BT only over Bluetooth at that address and SHALL NOT use or fall back to USB. It SHALL send the same print and explicit-cut protocol as over USB, hold the connection only while jobs are queued, an idle status read runs, or an explicit cut action executes, and treat an unreachable printer as offline. An address with another model, or a malformed address, SHALL fail startup.

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
- **WHEN** the Bluetooth link fails after page raster has started, including during finalization or an explicit cut action
- **THEN** the existing uncertain-output recovery contract applies, cleanup sends nothing further, and following output is blocked until recovery

#### Scenario: Explicit cut after the idle connection closed
- **WHEN** a healthy held end survives idle Bluetooth disconnect and Ptouch-Cut is requested
- **THEN** the server reconnects to that address, confirms live status without initialization, sends its blank continuation, and releases connection ownership after completion

### Requirement: Idle status polling is configurable

`PTOUCH_IDLE_POLL_SECONDS` SHALL control automatic idle status reads on every transport. Absent, idle reads SHALL follow PAPPL's status requests as before. `0` SHALL disable automatic idle device access. A positive integer SHALL set the minimum seconds between automatic idle access attempts, successful or not. Skipped reads SHALL keep the last published status. Job and explicit cut access SHALL be unaffected; cut access SHALL neither consume nor reset the idle-poll interval. Any other value SHALL fail startup.

#### Scenario: Polling unset
- **WHEN** `PTOUCH_IDLE_POLL_SECONDS` is absent
- **THEN** idle status behavior is identical to the behavior before this setting existed

#### Scenario: Polling disabled
- **WHEN** `PTOUCH_IDLE_POLL_SECONDS` is `0` and clients query idle printer status
- **THEN** automatic polling opens no device connection, and the next job still confirms live media before printing

#### Scenario: Polling rate-limited
- **WHEN** `PTOUCH_IDLE_POLL_SECONDS` is `300` and clients query the idle printer every few seconds
- **THEN** the server reads live status at most once per 300 seconds and reports the last published status between reads

#### Scenario: Rate limit with the printer unreachable
- **WHEN** `PTOUCH_IDLE_POLL_SECONDS` is `300`, the printer is unreachable, and clients query the idle printer every few seconds
- **THEN** automatic polling attempts at most one connection per 300 seconds

#### Scenario: Invalid polling value
- **WHEN** `PTOUCH_IDLE_POLL_SECONDS` is negative, fractional, or not a number
- **THEN** startup fails with a configuration error

#### Scenario: Explicit cut with polling disabled or rate-limited
- **WHEN** Ptouch-Cut is requested while polling is disabled or its next automatic read is not due
- **THEN** the action can acquire the device and confirm live media without waiting for, consuming or resetting the automatic idle-poll slot
