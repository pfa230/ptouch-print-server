#!/bin/sh
set -e
# DNS-SD needs a system dbus + avahi inside the container (container network).
mkdir -p /run/dbus /run/avahi-daemon
rm -f /run/dbus/pid /run/avahi-daemon/pid
dbus-daemon --system --fork
avahi-daemon -D            # NOTE: -D syslogs; avahi lines may not reach docker logs
exec ptouch-app server
