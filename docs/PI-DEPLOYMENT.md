# Raspberry Pi Bluetooth deployment

This guide runs the server on a wall-powered Raspberry Pi Zero 2 W and reaches a battery-powered PT-P710BT over Bluetooth, so the printer can move around the house. The Bluetooth path is software-tested; its physical acceptance is pending (see [README](../README.md#capabilities)).

## What you need

- Raspberry Pi Zero 2 W running **64-bit** Raspberry Pi OS Lite (Bookworm). The published image has `linux/arm64` and `linux/amd64` variants; a 32-bit OS cannot run it.
- The PT-P710BT, charged, within about 10 m of the Pi. Walls shorten the range.

## 1. Install Docker

```sh
sudo apt update
sudo apt install -y docker.io
```

## 2. Pair the printer once

Switch the printer on, then on the Pi:

```sh
bluetoothctl
power on
scan on            # wait for PT-P710BT and note its address
pair XX:XX:XX:XX:XX:XX
trust XX:XX:XX:XX:XX:XX
scan off
quit
```

If pairing asks for a PIN, try `0000`. BlueZ stores the pairing under `/var/lib/bluetooth` on the SD card, so it survives reboots. The container needs no BlueZ of its own.

## 3. Hand mDNS to the container

The container must use the host network: Linux refuses Bluetooth sockets inside a container's own network namespace. The container then runs its own Avahi on the host's mDNS port, so stop the Pi's:

```sh
sudo systemctl disable --now avahi-daemon.socket avahi-daemon.service
```

The container advertises the printer under the Pi's hostname.

## 4. Run the server

```sh
sudo docker run -d --name ptouch --restart unless-stopped \
  --network host \
  --ulimit nofile=65536:65536 \
  -e PTOUCH_MODEL=PT-P710BT \
  -e PTOUCH_BT_ADDRESS=XX:XX:XX:XX:XX:XX \
  -e PTOUCH_IDLE_POLL_SECONDS=0 \
  ghcr.io/pfa230/ptouch-print-server:edge
```

No USB mount, device rule, or added capability is needed. `PTOUCH_IDLE_POLL_SECONDS=0` stops idle status reads, which would otherwise reconnect to the printer and drain its battery; tape width and offline status then update only when a job runs. Use a number of seconds instead to poll at most that often.

Check it:

```sh
sudo docker logs ptouch | grep 'Created printer'   # ptouch-bt://Brother/PT-P710BT/<address>
sudo docker exec ptouch ptouch-app status -d ptouch
```

The queue is reachable at `ipp://<pi-hostname>.local:8000/ipp/print/ptouch`.

## Day to day

- **Switch the printer on to print.** A job sent while it is off or out of range waits in the queue and prints once the printer is reachable.
- **The connection lasts while jobs are queued.** It closes when the queue goes idle; the phone app can then connect. While the phone app holds the printer, the server sees it as offline and jobs wait.
- **Leaving range mid-print leaves the label uncertain.** The queue blocks new output. Power-cycle the printer, run `sudo docker restart ptouch`, then resubmit the job.
- **A held job keeps the connection open.** Plain `lp` and AirPrint jobs are not held. If a client holds a job (`job-hold-until`), the connection stays open even after that job is canceled; `sudo docker restart ptouch` releases it.

## Roll back

Stop the Pi container (`sudo docker rm -f ptouch`), re-enable Avahi if the Pi needs it (`sudo systemctl enable --now avahi-daemon`), and return to the USB deployment.
