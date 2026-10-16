# Raspberry Pi Bluetooth deployment

This guide runs the server on a wall-powered Raspberry Pi Zero 2 W and reaches a battery-powered PT-P710BT over Bluetooth, so the printer can move around the house. The Bluetooth path passed its [hardware gate](../tests/BLUETOOTH-ACCEPTANCE.md) on a Pi Zero 2 W with 12 mm tape.

## What you need

- Raspberry Pi Zero 2 W running **64-bit** Raspberry Pi OS Lite (Bookworm or Trixie). The published image has `linux/arm64` and `linux/amd64` variants; a 32-bit OS cannot run it.
- The PT-P710BT, charged, within about 10 m of the Pi. Walls shorten the range.

## Prebuilt SD card

`deploy/pi/` builds a card that does everything below unattended, with a read-only root, built to tolerate power loss. Reboots are tested; pulling power is not. Use it instead of the manual steps.

What the card does:

- **Power loss:** `overlayroot` keeps `/` in RAM over a read-only root, and `/boot/firmware` is read-only. Everything that must persist lives on the ext4 partition labelled `ptdata`, mounted at `/data`: Docker (`/data/docker`), Bluetooth pairing (`/var/lib/bluetooth`), the journal (`/var/log/journal`) and the server configuration (`/data/ptouch/ptouch.env`). Pairing, updates and configuration changes therefore never need the overlay disabled.
- **Service:** `ptouch.service` runs the container as described below, reading `/data/ptouch/ptouch.env`. It skips itself until `PTOUCH_BT_ADDRESS` is set.
- **Logs:** the container logs to the persistent journal, synced every 10 seconds. `ptouch-logs` shows the server log and `ptouch-logs jobs` only its `[Job N]` lines; both accept `journalctl` options such as `-f` or `-b -1`.
- **Updates:** `sudo ptouch-update` pulls `:edge` and restarts; `sudo ptouch-update sha-<commit>` pins or rolls back.

Build it on macOS with Homebrew OpenSSL and Docker:

1. Download and decompress the 64-bit Raspberry Pi OS Lite (Trixie) image.
2. Build the arm64 server image and save it: `docker build --platform linux/arm64 -f Dockerfile.deploy -t ghcr.io/pfa230/ptouch-print-server:edge . && docker save ghcr.io/pfa230/ptouch-print-server:edge | gzip -1 > ptouch-image.tar.gz`. First boot loads it, so the card needs no registry pull.
3. Write a settings file with `WIFI_SSID=`, `WIFI_PSK=`, `WIFI_COUNTRY=`, `LOGIN_PASSWORD=` and optionally `BT_ADDRESS=`, and save the public key that should log in as `pfa`, for example `ssh-add -L | grep <name> > login-key.txt`.
4. Insert the card, find it with `diskutil list`, and run `sudo deploy/pi/build-card.py diskN raspios.img ptouch-image.tar.gz settings.env login-key.txt`. It refuses any disk that is not a removable SD card, then erases it.

First boot takes about 15 minutes and reboots once into read-only mode. Then log in as `pfa`, pair with the printer switched on, and print:

```sh
sudo ptouch-pair            # scans for PT-P710BT; or: sudo ptouch-pair XX:XX:XX:XX:XX:XX
```

The card accepts the password over SSH until you turn it off. Do that only after a key login works, and write it to the read-only root:

```sh
sudo mount -o remount,rw /media/root-ro
echo 'PasswordAuthentication no' | sudo tee /media/root-ro/etc/ssh/sshd_config.d/50-cloud-init.conf
sudo mount -o remount,ro /media/root-ro && sudo reboot
```

Any other permanent change to the OS goes through `/media/root-ro` the same way, or `sudo raspi-config nonint do_overlayfs 1` followed by a reboot to make the root writable again.

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
pairable on
scan on            # wait for PT-P710BT and note its address
pair XX:XX:XX:XX:XX:XX
trust XX:XX:XX:XX:XX:XX
scan off
pairable off
info XX:XX:XX:XX:XX:XX   # must show Bonded: yes
quit
```

Pairing needs no PIN. Without `pairable on` the Pi pairs but does not bond, so every connection pairs again and takes up to 10 seconds instead of about 2. BlueZ stores the pairing under `/var/lib/bluetooth` on the SD card, so it survives reboots. The container needs no BlueZ of its own.

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

Stop the Pi server: `sudo docker rm -f ptouch` for a manual install, or `sudo systemctl stop ptouch` on a prebuilt card, whose service otherwise recreates the container (to keep it off across reboots, disable it through `/media/root-ro` as above). Re-enable Avahi if the Pi needs it (`sudo systemctl enable --now avahi-daemon`), and return to the USB deployment.
