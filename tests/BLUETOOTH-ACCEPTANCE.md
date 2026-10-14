# PT-P710BT Bluetooth acceptance gate

Status: **prepared, not run; awaiting approval.** Preparation used only socket-sink containers and printed nothing. The software evidence (fake-socket driver and adapter tests, byte identity with USB, both image smokes) does not replace these physical observations.

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
dbus-monitor --system "type='signal',path='/org/bluez/hci0/dev_${ADDR//:/_}'" | tee connected.log
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
