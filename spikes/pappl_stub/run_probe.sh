#!/bin/bash
# M0 Probe B/C/F harness (THROWAWAY). Runs inside the spike container.
# The stub creates printer 'spike' in-process on a socket sink (PAPPL USB
# auto-discovery hangs on the PT-2730), so no `add` CLI is needed.
set -u
PURI=ipp://127.0.0.1:8000/ipp/print/spike

mkdir -p /run/dbus; dbus-daemon --system --fork 2>/dev/null
avahi-daemon --no-chroot -D 2>/dev/null
python3 /host/repo/spikes/pappl_stub/sink.py >/tmp/sink.log 2>&1 & SINK=$!
cd /host/stub
: > /tmp/stub.err
./build/stub server >/tmp/stub.err 2>&1 & SRV=$!
sleep 3

echo "=== printers ==="
timeout 10 ipptool -tv "$PURI" get-printer-attributes.test 2>&1 | grep -iE "printer-state |media-ready|media-default|pwg-raster-document|finishings-supported|document-format-supported" | head

echo "=== submit fixtures ==="
for fx in arrow exact tall short gradient; do
  echo "--- $fx ---"
  timeout 25 ./build/stub submit -u "$PURI" "/host/repo/spikes/fixtures/$fx.png" 2>&1 | head -2
  sleep 2
done
sleep 2

echo "=== STUB callbacks ==="
grep "\[STUB\]" /tmp/stub.err | grep -v "starting" | tail -40
kill "$SRV" "$SINK" 2>/dev/null
exit 0
