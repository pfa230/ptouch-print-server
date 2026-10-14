# Spec Delta

## ADDED Requirements

### Requirement: PT-P710BT can use Bluetooth instead of USB

When `PTOUCH_BT_ADDRESS` is set, the server SHALL reach the selected PT-P710BT only over Bluetooth at that address and SHALL NOT use or fall back to USB. It SHALL send the same print protocol as over USB, hold the connection only while a job or status read owns the device, and treat an unreachable printer as offline. An address with another model, or a malformed address, SHALL fail startup.

#### Scenario: Bluetooth printer in range
- **WHEN** PT-P710BT is selected with a Bluetooth address and the paired printer is on and in range
- **THEN** jobs print with the same command and raster bytes the USB path would send, and the connection closes after each job

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
- **WHEN** the Bluetooth link fails after page raster has started
- **THEN** the existing uncertain-output recovery contract applies and no new raster is sent until recovery

### Requirement: Idle status polling is configurable

`PTOUCH_IDLE_POLL_SECONDS` SHALL control idle status reads on every transport. Absent, idle reads SHALL follow PAPPL's status requests as before. `0` SHALL disable idle device access. A positive integer SHALL set the minimum seconds between idle device reads. Skipped reads SHALL keep the last published status. Status read by jobs SHALL be unaffected. Any other value SHALL fail startup.

#### Scenario: Polling unset
- **WHEN** `PTOUCH_IDLE_POLL_SECONDS` is absent
- **THEN** idle status behavior is identical to the behavior before this setting existed

#### Scenario: Polling disabled
- **WHEN** `PTOUCH_IDLE_POLL_SECONDS` is `0` and clients query the idle printer
- **THEN** the server opens no device connection, and the next job still confirms live media before printing

#### Scenario: Polling rate-limited
- **WHEN** `PTOUCH_IDLE_POLL_SECONDS` is `300` and clients query the idle printer every few seconds
- **THEN** the server reads live status at most once per 300 seconds and reports the last published status between reads

#### Scenario: Invalid polling value
- **WHEN** `PTOUCH_IDLE_POLL_SECONDS` is negative, fractional, or not a number
- **THEN** startup fails with a configuration error
