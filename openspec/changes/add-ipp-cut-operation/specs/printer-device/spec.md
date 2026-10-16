# Spec Delta

## ADDED Requirements

### Requirement: Direct IPP cut action

The server SHALL expose the implementation-defined `Ptouch-Cut` printer operation (`0x5f01`) in `operations-supported`. A request targeting the queue SHALL release its healthy held label or strip without document data, job creation, raster replay, or a blank helper label. The operation SHALL require only the feed needed for the terminal cut and SHALL use the server's configured access policy.

#### Scenario: Release the last chain label
- **WHEN** an authorized client sends Ptouch-Cut to an idle printer holding the last label of a healthy chain
- **THEN** the printer releases that label with one terminal cut, no new label is printed, and the response is successful only after transfer and device completion are confirmed

#### Scenario: Release a joined strip
- **WHEN** Ptouch-Cut targets a healthy held strip printed with finishings none
- **THEN** the strip is released by one terminal cut without introducing cuts between its labels

#### Scenario: Discover the extension
- **WHEN** a client requests operations-supported
- **THEN** one attribute lists 0x5f01 alongside the standard operations, and client documentation identifies it as a project extension

#### Scenario: Invalid or unauthorized request
- **WHEN** the request has document data, lacks a valid printer target, or fails the configured access policy
- **THEN** it is rejected with an appropriate IPP or HTTP error before any cut command is sent

### Requirement: Direct cuts use exclusive device access

The cut action SHALL execute synchronously through the configured printer transport under exclusive device ownership. Busy requests SHALL fail immediately without deferred execution. A known closed run SHALL succeed without feeding or cutting. Uncertain output SHALL reject the action. Offline or faulted hardware SHALL return an error rather than retain a pending cut.

#### Scenario: Printing or maintenance owns the device
- **WHEN** a cut request races with printing, polling, or another cut and cannot obtain exclusive device ownership
- **THEN** it returns server-error-busy without adding a job or sending cut bytes, and no cut runs later on its behalf

#### Scenario: Repeated request after success
- **WHEN** the run is already known closed and the device is otherwise idle
- **THEN** Ptouch-Cut succeeds without feed or cut output, including when repeated after a lost successful response

#### Scenario: Printer unavailable before cutting
- **WHEN** the configured printer cannot be opened or clean loaded-media status cannot be confirmed for an open run
- **THEN** the request returns a device-unavailable error without a cut, a queued retry, or transport fallback

#### Scenario: Output already uncertain
- **WHEN** an earlier transfer or completion failure has latched recovery required
- **THEN** Ptouch-Cut fails with the existing power-cycle-then-restart recovery instruction and sends no cut command

#### Scenario: Cut outcome becomes uncertain
- **WHEN** cut command transfer is incomplete or its completion cannot be confirmed
- **THEN** the response reports failure, the run stays uncertain, and neither repeated requests nor cleanup replay the cut

### Requirement: Cut-only support requires physical evidence

PT-2730 and PT-P710BT cut-only support SHALL require integrated physical evidence that a held label and a joined strip are released without new raster data or a blank label. PT-P710BT SHALL be checked over USB and Bluetooth. Software captures SHALL establish emitted bytes and failure behavior; successful transfer alone SHALL NOT establish physical cutting.

#### Scenario: Standalone command has no physical effect
- **WHEN** an apparently successful command transfer leaves the label attached
- **THEN** the hardware check fails and cut-only support is not claimed; printing a helper label does not satisfy the requirement

#### Scenario: Physical acceptance succeeds
- **WHEN** each supported model and transport passes the integrated cut-only check
- **THEN** retained evidence identifies the command sequence, completion status, actual release, extra feed, and absence of a new blank label

## MODIFIED Requirements

### Requirement: PT-P710BT can use Bluetooth instead of USB

When `PTOUCH_BT_ADDRESS` is set, the server SHALL reach the selected PT-P710BT only over Bluetooth at that address and SHALL NOT use or fall back to USB. It SHALL send the same print and cut-only protocol as over USB, hold the connection only while jobs are queued, an idle status read runs, or an explicit cut action executes, and treat an unreachable printer as offline. An address with another model, or a malformed address, SHALL fail startup.

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

#### Scenario: Explicit cut after the idle connection closed
- **WHEN** a healthy held end survives an idle Bluetooth disconnect and Ptouch-Cut is requested
- **THEN** the server reconnects to the configured address, confirms status without resetting the run, cuts without new raster, and releases its connection ownership afterward
