# 9. Deployment topology: Caddy at the front, shared network for the client, bind-mounted USB

**Status:** Accepted

## Context

The printer runs as a container on an Unraid host. Three consumers exist: LAN clients (humans and
AirPrint), the labeler service on the same host, and maintenance.

An earlier iteration attached the container directly to the LAN (`br0`, its own IP) so mDNS discovery
would work. That exposed the printer directly and, because `br0` is an ipvlan-style network, the
container was unreachable from the host and from other containers.

## Decision

- **No `br0`.** The container is not exposed on the LAN.
- **LAN access via Caddy** at `ipps://ptouch.home.pfa.name`, reusing the existing wildcard cert.
  `header_up Host` is mandatory: PAPPL rejects any `Host` that is not an IP, `localhost`, or `*.local`
  with HTTP 400, and Caddy forwards the client's `Host` by default.
- **The labeler reaches it over a shared docker network** by alias, not by a bridge IP, which is
  dynamic.
- **USB is a bind mount** (`volumes: /dev/bus/usb:/dev/bus/usb`) plus a device cgroup rule, **not**
  compose `devices:`.
- `ulimits: nofile 65536`.

## Consequences

- The printer is no longer directly addressable on the LAN, and mDNS/AirPrint discovery is lost with
  it. Restoring discovery through the proxy means publishing unicast DNS-SD records (RFC 6763) - the
  same PTR/SRV/TXT records, in the normal DNS zone.
- **`devices:` maps only the device nodes present at container start.** Power-cycling the printer
  creates a new node the container can never see, so it retries a stale one forever and only a restart
  recovers it. A bind mount propagates new nodes. This was diagnosed only after it silently swallowed
  print jobs.
- **`nofile` matters:** dbus refuses to start when it cannot raise its fd limit to 65536, avahi dies
  with it, and DNS-SD then advertises nothing at all - which looked exactly like a multicast problem.
- IPP is HTTP over TCP, so proxying is unremarkable; `flush_interval -1` keeps raster streaming.
