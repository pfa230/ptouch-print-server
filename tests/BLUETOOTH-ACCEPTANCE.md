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
enum() { ipptool -t -d finishings=$1 -d copies=$2 -f label.png "$U" $G/print-enum.test; }
chain() { ipptool -t -d copies=$1 -f label.png "$U" $G/print-chain.test; }
```

On the Pi, in a second terminal, log the link once a second for the whole gate:

```sh
while sleep 1; do echo "$(date +%T) $(bluetoothctl info $ADDR | grep Connected)"; done | tee connected.log
```

## Steps

| Step | Action | Labels | Pass criteria |
|------|--------|-------:|---------------|
| 1 | `sudo docker exec ptouch ptouch-app status -d ptouch` with the printer on | 0 | Status shows no `offline`; `media-ready` reports 12 mm. The printer neither feeds nor cuts (status reads send no reset). |
| 2 | `enum 11 1`; record the submit-to-`completed` time from `ptouch-app jobs -d ptouch` | 1 | One intact 76-dot outline at 1:1, fresh leader removed, final cut. Job `completed`, not aborted. |
| 3 | Run `enum 11 1; enum 11 1` back to back | 2 | Two separate cut labels. `connected.log` shows one continuous `Connected: yes` span covering both jobs, then `no` once the queue is idle. |
| 4 | `chain 2`; wait for `Connected: no`, then 10 more seconds; `enum 60 2` | 4 | First job: internal cut, last end held. Second job: the held end is released without a new discarded leader, internal cut, final cut. If the held end was lost or a new leader appears, **stop**: that is a contract decision, not a retry. |
| 5 | Switch the printer off. `time sudo docker exec ptouch ptouch-app status -d ptouch` | 0 | Reports `offline`; the command returns within 11 seconds. Record the time. |
| 6 | With the printer still off, `enum 11 1`; wait 30 s; switch the printer on | 1 | The job waits (not aborted) while off and prints once the printer is on. Intact outline, final cut. |
| 7 | Recreate the container with `-e PTOUCH_IDLE_POLL_SECONDS=0`; run `ptouch-app status -d ptouch` five times over one minute | 0 | `connected.log` stays `Connected: no` for that minute; `docker logs` shows no new `not reachable` or status lines. |
| 8 | `sudo reboot` the Pi; after boot, without pairing again, `enum 11 1` | 1 | The container is running after boot and the label prints. |
| | **Total** | **9** | |

The "USB unit receives nothing" requirement is covered by software only: the Bluetooth container has no USB mount, so it cannot reach USB at all.

## Record

Keep `connected.log`, `sudo docker logs ptouch` (before step 7's recreate and at the end), every `ipptool` output, the step 2 and step 5 timings, and photos or notes of each strip. Then update README capabilities with the certified Bluetooth scope.
