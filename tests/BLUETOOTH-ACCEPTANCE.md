# PT-P710BT Bluetooth acceptance gate

Status: **passed 2026-10-16**, all 8 steps, 9 labels on 12 mm tape. See [Results](#results).

## Bound

Print **9 complete labels** on 12 mm tape, all from `label.png` (76×180 dots, 25.4 mm). Content totals 22.9 cm plus leaders; have at least 0.5 m loaded. There are no helper prints and no retries. Stop at the first failed criterion, unexpected output, job error, or `Output uncertain` message, and report it. A changed or repeated step needs a revised gate.

## Setup

On the Pi, follow [docs/PI-DEPLOYMENT.md](../docs/PI-DEPLOYMENT.md) through pairing, but start the server with idle polling **unset** (drop `-e PTOUCH_IDLE_POLL_SECONDS=0`). Load 12 mm tape and charge the printer. Keep the phone app closed.

On the client (any host with `ipptool` and Pillow, a checkout of this repository, and the Pi reachable):

```sh
ADDR=XX:XX:XX:XX:XX:XX                       # the printer's Bluetooth address
U=ipp://<pi-hostname>.local:8000/ipp/print/ptouch
G=tests/bluetooth-gate
python3 -I $G/make-fixture.py label.png
enum() { ipptool -t -d finishings=$1 -d copies=$2 -f label.png "$U" $G/print-enum.ipp; }
chain() { ipptool -t -d copies=$1 -f label.png "$U" $G/print-chain.ipp; }
state() { ipptool -tv "$U" $G/get-printer-state.ipp | grep -E 'printer-state|media-ready'; }
times() { ipptool -tv "$U" $G/get-job-times.ipp | grep -E 'job-id|job-state|time-at'; }
```

On the Pi, in a second terminal, record every connect and disconnect for the whole gate. BlueZ signals each change of the device's `Connected` property, so no event is missed between samples:

```sh
sudo dbus-monitor --system "type='signal',path='/org/bluez/hci0/dev_${ADDR//:/_}'" | tee connected.log
```

`grep -A1 '"Connected"' connected.log` lists the changes in order; each signal header line carries its time.

## Steps

| Step | Action | Labels | Pass criteria |
|------|--------|-------:|---------------|
| 1 | With the printer on, `state` | 0 | A fresh connection answers status: `printer-state-reasons` is `none` and `media-ready` is a 12 mm size. The printer neither feeds nor cuts (status reads send no reset). |
| 2 | `enum 11 1`, then `times` | 1 | One intact 76-dot outline at 1:1, fresh leader removed, final cut. The job is `completed`, not aborted; record `time-at-completed` minus `time-at-processing`. The job only completes after the printer reports receiving-ready, which the server waits up to 20 s for, so this duration bounds the receiving-ready time. |
| 3 | Switch the printer off. Run `enum 11 1` twice; both jobs wait. Switch the printer on | 2 | Both queued jobs print as two separate cut labels. `connected.log` shows exactly one connect before the first label and one disconnect after the second, with none between them. |
| 4 | `chain 2`; wait until `connected.log` shows the disconnect, then 10 more seconds; `enum 60 2` | 4 | First job: internal cut, last end held. Second job, on a new connection: the held end is released without a new discarded leader, internal cut, final cut. If the held end was lost or a new leader appears, **stop**: that is a contract decision, not a retry. |
| 5 | Switch the printer off. `time state` | 0 | Reports `offline` and returns within 11 seconds. Record the time. |
| 6 | With the printer still off, `enum 11 1`; wait 30 s; switch the printer on | 1 | The job waits (not aborted) while off and prints once the printer is on. Intact outline, final cut. |
| 7 | Recreate the container with `-e PTOUCH_IDLE_POLL_SECONDS=0`; with the printer on and idle, run `state` five times over one minute | 0 | `connected.log` records no connect during that minute. |
| 8 | `sudo reboot` the Pi; after boot, without pairing again, `enum 11 1` | 1 | The container is running after boot and the label prints. |
| | **Total** | **9** | |

The "USB unit receives nothing" requirement is covered by software only: the Bluetooth container has no USB mount, so it cannot reach USB at all.

## Record

Keep `connected.log`, `sudo docker logs ptouch` (before step 7's recreate and at the end), every `ipptool` output, the step 2 and step 5 timings, and photos or notes of each strip. Then update README capabilities with the certified Bluetooth scope.

## Results

Run 2026-10-16 04:01 to 04:17 UTC on a Raspberry Pi Zero 2 W (Debian 13, kernel 6.18, BlueZ 5.82, arm64) with a battery-powered PT-P710BT and 12 mm tape. Steps 1 to 7 ran the `ptouch-app` binary built from `e2c6758` with host networking; step 8 ran the Pi's systemd-managed `ghcr.io/pfa230/ptouch-print-server:edge` container, whose `ptouch-app` has the same sha256 (`729f7f22…`). Times are UTC.

| Step | Result | Evidence |
|------|--------|----------|
| 1 | Pass | `none`, `om_12x100mm_12x100mm` from a live read; the connection lasted 2.6 s (04:05:00.9 to 04:05:03.5). No feed or cut reported. |
| 2 | Pass | Job 1 completed; `time-at-completed` minus `time-at-processing` = 11 s. Operator: intact outline, leader removed, final cut. |
| 3 | Pass | Jobs 2 and 3 waited (connect attempts failed at 04:06:48 and 04:06:58), then printed in order on one connection: connect 04:07:07.3, disconnect 04:07:28.3, none between. Operator: two separate cut labels. |
| 4 | Pass | Job 4 (`ptouch-chain`, 2 copies) connected 04:08:06 to 04:08:21; job 5 (`enum 60`, 2 copies) sent 04:08:39 on a new connection, 04:08:43 to 04:09:01. Operator: one leader (job 4's fresh run), then four cut labels; no new leader for job 5. |
| 5 | Pass | `offline` in 5.4 s. The radio page timeout (about 5.1 s) bounds it, not the 10 s connect deadline. |
| 6 | Pass | Job 6 queued 04:11:09 while off, retried about every 10 s without aborting, completed 04:12:16.9 after power-on. Operator: intact outline, final cut. |
| 7 | Pass | With `PTOUCH_IDLE_POLL_SECONDS=0`, five status queries 04:12:55 to 04:13:52 recorded no connect. |
| 8 | Pass | After `systemctl reboot` the service was up by 04:16:34 with the bond intact (`Bonded: yes`); job 1 completed 04:16:50.0 to 04:17:00.6 without pairing again. Operator: intact outline, final cut. |

Deviations, none of which changed a printed step:

- `dbus-monitor` as an ordinary user is denied monitoring and records nothing; the command above now uses `sudo`.
- The first step 1 queries ran against a container started with idle polling `0`, so they read no status. They printed nothing and are not counted.
- The Pi first paired without bonding (`Pairable: no`), so each connection paired again and step 1's first live read took 9.5 s to connect. The Pi was then bonded as [docs/PI-DEPLOYMENT.md](../docs/PI-DEPLOYMENT.md) now describes, and step 1 was repeated.
- `connected.log` lived on the Pi's tmpfs overlay and was lost at the step 8 reboot; the timings above were read from it before then.

Software-only: 9 and 18 mm tape over Bluetooth (the driver sends the same bytes as USB, which is certified on 9/12/18 mm), and a USB-attached unit receiving nothing.
