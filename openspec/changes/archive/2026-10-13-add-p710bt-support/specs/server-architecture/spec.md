# Spec Delta

## MODIFIED Requirements

### Requirement: Supported and unambiguous hardware

Only the implemented PT-2730 and PT-P710BT paths SHALL receive print output. Missing or empty `PTOUCH_MODEL` SHALL select PT-2730; PT-P710BT SHALL require explicit selection. Unknown or recognized unsupported models SHALL fail configuration. The stable model URI SHALL resolve by the selected VID/PID, accepting exactly one matching device and blocking zero or multiple matches without selecting by enumeration order.

#### Scenario: Duplicate supported printers

- **WHEN** two devices of the selected supported model match the configured queue
- **THEN** the existing queue reports ambiguity and neither device is claimed for printing

#### Scenario: Two different supported models

- **WHEN** one PT-2730 and one PT-P710BT are attached and a model is configured
- **THEN** the queue selects only that model, and the other printer does not create ambiguity or receive its output

#### Scenario: Compatible default

- **WHEN** no model is configured
- **THEN** the queue retains PT-2730 identity and behavior without automatically adopting an attached PT-P710BT
