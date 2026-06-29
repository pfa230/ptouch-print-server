#!/bin/sh
set -e
# DNS-SD needs a system dbus + avahi inside the container (isolated br0 IP).
mkdir -p /run/dbus /run/avahi-daemon
rm -f /run/dbus/pid /run/avahi-daemon/pid
dbus-daemon --system --fork
avahi-daemon -D            # NOTE: -D syslogs; avahi lines may not reach docker logs
# the app logs to /tmp/ptouch.log (system_cb, not stderr); surface it to `docker logs` (Codex #3)
touch /tmp/ptouch.log
tail -F /tmp/ptouch.log &
exec ptouch-app server
