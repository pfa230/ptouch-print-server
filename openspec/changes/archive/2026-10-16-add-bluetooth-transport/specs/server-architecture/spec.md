# Spec Delta

## MODIFIED Requirements

### Requirement: Supported and unambiguous hardware

Only the implemented PT-2730 and PT-P710BT paths SHALL receive print output. Missing or empty `PTOUCH_MODEL` SHALL select PT-2730; PT-P710BT SHALL require explicit selection. Unknown or recognized unsupported models SHALL fail configuration. The stable model URI SHALL resolve by the selected VID/PID, accepting exactly one matching device and blocking zero or multiple matches without selecting by enumeration order. When a Bluetooth address is configured, the stable URI SHALL resolve by that address alone.

#### Scenario: Duplicate supported printers

- **WHEN** two devices of the selected supported model match the configured queue
- **THEN** the existing queue reports ambiguity and neither device is claimed for printing

#### Scenario: Two different supported models

- **WHEN** one PT-2730 and one PT-P710BT are attached and a model is configured
- **THEN** the queue selects only that model, and the other printer does not create ambiguity or receive its output

#### Scenario: Compatible default

- **WHEN** no model is configured
- **THEN** the queue retains PT-2730 identity and behavior without automatically adopting an attached PT-P710BT

#### Scenario: Bluetooth address configured

- **WHEN** PT-P710BT is selected with a Bluetooth address
- **THEN** the queue's device URI names that address, and USB enumeration plays no part in selection

### Requirement: Container hotplug and addressing

USB container deployment SHALL bind-mount `/dev/bus/usb` and allow USB character devices with `c 189:* rwm` so newly numbered nodes remain accessible. dbus/Avahi SHALL receive a nofile limit of 65536. Reverse proxies SHALL forward a Host accepted by PAPPL; shared-network clients MAY use a stable alias. Discovery, TLS termination, and network exposure SHALL remain deployment choices.

#### Scenario: Hotplug behind a proxy

- **WHEN** the printer is replugged in a container reached through a reverse proxy
- **THEN** the new USB node is accessible and the proxy supplies an accepted Host without changing the print contract

## ADDED Requirements

### Requirement: Private Bluetooth transport

The adapter SHALL reach a Bluetooth printer through an internal scheme over a kernel RFCOMM socket, without a host serial bridge or in-server pairing. A connect attempt SHALL give up within 10 seconds. Status readers SHALL validate the owning printer URI before accessing Bluetooth connection data, as for USB.

#### Scenario: Transport isolation

- **WHEN** USB, Bluetooth, and socket test URIs are each evaluated by status processing
- **THEN** each reaches only its own transport's operations

#### Scenario: Absent printer

- **WHEN** the configured printer is off or out of range
- **THEN** each connect attempt fails within 10 seconds and the queue reports the printer offline

### Requirement: Bluetooth container deployment

A Bluetooth deployment SHALL run the container in the host network namespace, because the kernel refuses Bluetooth sockets elsewhere. It SHALL need no USB device mount. Pairing SHALL be a host step whose keys persist across host reboots. The container SHALL need no BlueZ and no added capability. Exactly one mDNS responder SHALL serve the host.

#### Scenario: Pi reboot

- **WHEN** the Pi hosting a paired Bluetooth deployment reboots
- **THEN** the container restarts and prints to the printer without re-pairing

### Requirement: Multi-architecture runtime image

Published image tags SHALL include `linux/amd64` and `linux/arm64` variants built from the same `Dockerfile.deploy`. Both architectures SHALL build and pass the build's CTest run on pull requests, before anything is published.

#### Scenario: Pull request build

- **WHEN** a pull request changes the image
- **THEN** CI builds both architectures and runs CTest in each without publishing

#### Scenario: Pull on a Pi

- **WHEN** a Raspberry Pi Zero 2 W running a 64-bit OS pulls a published tag
- **THEN** it receives the arm64 variant of the same build
