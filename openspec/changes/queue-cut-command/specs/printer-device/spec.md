# Spec Delta

## MODIFIED Requirements

### Requirement: Direct cuts use exclusive device access

The action SHALL execute synchronously through the configured transport under exclusive device ownership. An authorized cut SHALL take its place behind already accepted print jobs and cuts, ahead of later accepted print jobs and cuts. It SHALL wait through ownership contention, including status polling, rather than return server-error-busy. CLOSED or healthy blank-only runs SHALL succeed without feed/cut output once owned. Uncertain output SHALL be refused. Faulted or unusable live status on an acquired device SHALL return server-error-device-error.

#### Scenario: Printing or maintenance owns the device
- **WHEN** prints A and B are accepted, cut C1 is admitted while A is still printing, print D is accepted, cut C2 is admitted, and print E is accepted
- **THEN** A and B finish before C1 executes, D prints between C1 and C2, and E prints after C2, with each cut response waiting for its own execution result

#### Scenario: Status polling owns the device
- **WHEN** a cut arrives while a status poll owns the device
- **THEN** it waits for that poll to release ownership and then executes without returning a contention error

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
