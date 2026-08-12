# 9. Container USB hotplug and reverse-proxy requirements

**Status:** Accepted

## Context

A containerized printer application needs access to USB devices after unplug,
replug, and power cycles. Deployments may also place IPP behind a reverse proxy
or share a container network with printing clients.

## Decision

- Bind-mount `/dev/bus/usb` and allow USB character devices with
  `device_cgroup_rules: ['c 189:* rwm']`. Individual `devices:` mappings cover only
  nodes present when the container starts and cannot handle new hotplug nodes.
- Provide `nofile` limits of 65536 for the image's dbus and Avahi services.
- If using a reverse proxy, forward a Host value accepted by PAPPL, such as
  `ptouch.local:8000`. PAPPL rejects other hostnames with HTTP 400.
- Printing clients on a shared container network can use a stable network alias.

## Consequences

USB hotplug works without recreating the container for each new device node.
Network exposure, DNS-SD discovery, TLS termination and client access are choices
for each deployment, independent of the application's printing contract.
