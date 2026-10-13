# P710BT integrated acceptance gate

Status: **passed on 2026-10-13; candidate removed, original image/configuration retained**. Preparation sent
IPP jobs to an isolated socket sink only. It did not access `vault`, mount USB,
print labels, deploy a service, or certify physical behavior.

The candidate is `ptouch-p710bt:local`, explicitly selected with
`PTOUCH_MODEL=PT-P710BT`. The staged bundle is
`openclaw:/tmp/p710bt-integrated-gate-20261013/`; its durable copy is
`openclaw:~/.local/share/ptouch-validation/p710bt-20261013/integrated-gate/`.
It contains `candidate-image.tar.gz`, marked fixtures, generators, an existing
transparent libusb capture shim, per-stage ipptool requests, pixel expectations,
SHA-256 manifests, and isolated socket captures. Retain both software evidence
and the later physical evidence; neither replaces the operator's observations.

## Bound and observations

Print **11 complete labels**, plus one deliberately incomplete PWG upload,
one blocked valid follower, and one zero-label wrong-media request. There are
no helper labels, button feed/cuts, automatic resubmissions, or retries. Stop
on any failed criterion, unexpected output, USB/power loss in a continued run,
or unconfirmed status. A changed gate or retry requires a revised concrete gate.

Operator amendments on 2026-10-13 explicitly requested “lets repeat step 1” and
then “now step 2”, adding one two-copy repeat of each batch. The actual bound
and outcome are **15 complete labels**: the original 11 plus four requested
repeats. Original/repeated captures are retained separately as `stage1-repeat1.*`
and `stage2-repeat1.*`; the second repeat closed the held run before stage 3.
No automatic retry or uncounted helper print occurred.

Complete content totals **28.448 cm**: ten 180-row labels at 25.4 mm and one
216-row label at 30.48 mm. Initial leaders, feed/cut allowances, and the incomplete
fragment are additional consumption, whose physical amount is not yet measured.
Have at least 0.5 m available across the loaded 12/9/18 mm cassettes; this is a
preflight budget, not a claim about actual consumed tape. Record actual waste.

Including the four requested repeats, complete printed content totals
**38.608 cm**. Actual tape consumption and leader/feed waste were not measured.
The completed scope includes all four finishing policies, continuous held-run
continuation, unknown-count mixed lengths, 76/52/120-dot rectangles, zero-output
wrong-media rejection, queued USB-only recovery, and explicit uncertainty recovery.
The operator confirmed the expected strips/cuts and the final intact S7 outline.
The retained bundle contains incoming status packets and 186 hashed physical
evidence files; later operator-observation records have their own updated manifest.
The test commands below are the completed execution record, not an instruction
to print additional labels. Physical 6/24 mm coverage remains untested.

| Stage | Tape / fixture / request | Complete labels | Required observation |
| --- | --- | ---: | --- |
| 1 | 12 mm, `stage1.png`, chain-only, two copies | 2 | Fresh leader removed, internal separation, last end held. Extended page indications 0/1. |
| 2 | 12 mm, `stage2.png`, chain-and-final, two copies after 10 seconds idle | 2 | Prior held end released, no discarded new leader, final end cut. No invalidate/init; page indications 1/1. |
| 3 | 12 mm, `stage3.png`, none, two copies | 2 | Fresh leader removed, two joined labels held. Page indications 0/1. |
| 4 | 12 mm, `stage4.pwg`, final-only, unknown total, 180/216 rows | 2 | Prior strip released, joined labels retain individual lengths, one final cut. No invalidate/init; page indications 1/1. |
| 5 negative | 12 mm, `stage5-incomplete.pwg`, then blocked `stage5.png` | 0 | Fragment aborts without FF/EJECT; valid follower remains blocked with no output or replay. |
| 5 recovery | Button power-cycle and candidate restart, then full-width `stage5.png` | 1 | Only the new S5 label prints, intact 76-dot outline, no S51 fragment/ghost. Fresh page indication 0. |
| 5a | On a closed run, load 9 mm; submit `stage5.png` explicitly requesting 12 mm | 0 | Job aborts before initialization/setup/raster/feed/cut; only status requests allowed. Next valid job remains usable. |
| 6 | 9 mm, powered but USB disconnected, queue `stage6.png` once, default trim | 1 | Candidate reports unavailable; the same queued job resumes after reconnect and confirmed 9 mm media. Intact 52-dot outline at 1:1; final cut. |
| 7 | 18 mm, `stage7.png`, default trim | 1 | Intact 120-dot outline at 1:1; final cut, closed run. |
| | **Total** | **11** | |

All PNGs have 180 rows at 180 dpi, complete edge outlines, a solid upper-left
marker, a lower-right open box, an arrow, and a stage identifier. Stage 4 uses
S41/S42 and 180/216-row PWG pages with declared total zero. S51 identifies the
incomplete fragment; its file decodes 60 of 180 rows. PNG widths are 76 dots for
stages 1/2/3/5, 52 for stage 6, and 120 for stage 7. Templates request exact
25.4 mm pages and canonical tape margins (12 mm: 63/71, 9 mm: 83/85,
18 mm: 53/57 in 1/100 mm). The stage-4 PWG headers supply their own lengths.

For each complete capture, require the exact page widths/heights/indications
in the matrix, 16 decoded bytes per row, `M 02`, `ESC i a 01`, automatic status
`ESC i ! 00`, and 14-dot margin `ESC i d 0e 00`. Require fresh invalidation/init
only at closed-run starts, the policy's FF/EJECT count, and exact pixels matching
`socket-evidence/*-page*.head.raw`. `analyse.py` follows command lengths so
raster payload bytes cannot become false commands. Incoming 32-byte statuses
must report no errors and receiving-ready after complete output. Require no
FF/EJECT for the fragment, no page output for its follower or trial 5a, and no
unrequested replay. Compare recorded byte sequences to the corresponding socket
capture after removing only `ESC i S` status requests; unexplained differences
stop the gate.

## Authorized execution setup

The following are reviewable commands, **not commands already executed on vault**.
After approval, transfer the retained bundle and image through pfa's SSH connection
to `vault`, using a newly created directory. From openclaw, run:

```sh
GATE=$(ssh vault mktemp -d /tmp/p710bt-acceptance.XXXXXX)
scp -r "$HOME/.local/share/ptouch-validation/p710bt-20261013/integrated-gate/." "vault:$GATE/"
printf 'On vault, export GATE=%s\n' "$GATE"
ssh -t vault
```

On vault, substitute the exact directory just printed, export it for the helper,
verify every retained file, and load only the candidate image:

```sh
export GATE=/tmp/p710bt-acceptance.ACTUAL_SUFFIX
cd "$GATE"
sha256sum -c SHA256SUMS
gzip -dc candidate-image.tar.gz | sudo docker load
sudo docker image inspect ptouch-p710bt:local > "$GATE/physical-evidence/candidate-image.json"
sudo docker ps > "$GATE/physical-evidence/original-containers.txt"
sudo docker inspect ptouch > "$GATE/physical-evidence/original-service.json"
sudo docker inspect -f '{{.State.Running}}' ptouch > "$GATE/physical-evidence/original-running.txt"
sudo docker exec ptouch ptouch-app jobs -d ptouch > "$GATE/physical-evidence/original-jobs.txt"
```

If and only if the recorded idle original `ptouch` container owns this P710BT,
yield it and record that action before starting the candidate:

```sh
sudo docker stop ptouch
printf 'ptouch\n' > "$GATE/physical-evidence/yielded-services.txt"
```

Record other competing owners and their restoration commands before yielding
them; do not stop unrelated services.

Before starting, record `docker ps`, `docker inspect ptouch`, the previous image,
configuration, running state, and existing job/queue state. Confirm the previous
queue is idle. Identify the sole `04f9:20af` USB device and its current owner.
Yield only a competing process that owns this P710BT, recording how it was stopped;
an idle PT-2730 service may remain running if it owns different hardware.

Confirm 12 mm tape, adequate battery charge/USB power, clean idle status, no
competing mobile printing app, and disconnected Bluetooth printing clients. Record
USB identity, media, status, candidate image, and auto-off setting if available.
Do not alter auto-off settings. Never use the initializing USB `smoke` command
as preflight. Enumeration-only `probe` and status requests are sufficient.

The candidate uses a separate local port and a fresh process state, with no
production configuration changes:

```sh
mkdir -p "$GATE/physical-evidence"
touch "$GATE/physical-evidence/usb-out.bin"
sudo docker run -d --name ptouch-p710bt-acceptance \
  -p 127.0.0.1:18000:8000 \
  --ulimit nofile=65536:65536 \
  --device-cgroup-rule 'c 189:* rwm' \
  -e PTOUCH_MODEL=PT-P710BT \
  -e LD_PRELOAD=/gate/tools/usb-capture.so \
  --mount "type=bind,src=$GATE,dst=/gate,readonly" \
  --mount "type=bind,src=$GATE/physical-evidence,dst=/evidence" \
  --mount type=bind,src=/dev/bus/usb,dst=/dev/bus/usb \
  ptouch-p710bt:local
```

The copied ipptool/capture binaries were checked against the candidate runtime
libraries without USB. Confirm that vault uses this prepared architecture; the
first status request verifies ipptool loading before any document is submitted. The IPP URI within that container is
`ipp://127.0.0.1:8000/ipp/print/ptouch`; host status access uses port 18000.
`tools/run-stage.sh` records USB output offsets, runs exactly one named request,
saves its IPP log, slices its command capture, and produces length-aware analysis.
It never automatically retries, recovers, or submits another stage. It requires
`GATE` to identify this copied bundle. `status`/`jobs` requests have no document.

## Exact stage sequence

Run the status-only preflight and inspect `media-col-ready`, errors, power, USB
presence, and job state before releasing a stage. Inspect physical output and
capture analysis after every stage; do not run the next stage on failure.

```sh
"$GATE/tools/run-stage.sh" status
"$GATE/tools/run-stage.sh" jobs
"$GATE/tools/run-stage.sh" stage1 stage1.png
```

Leave USB and power continuously present. Record the Power LED and USB presence
before and after a **10-second idle gap**; then release stage 2 once. An unexpected
shutdown invalidates continuation and stops this gate; it does not establish a
page-indication failure. One candidate process spans stages 1 through 4.

```sh
sleep 10
"$GATE/tools/run-stage.sh" stage2 stage2.png
"$GATE/tools/run-stage.sh" stage3 stage3.png
"$GATE/tools/run-stage.sh" stage4 stage4.pwg
"$GATE/tools/run-stage.sh" stage5-incomplete stage5-incomplete.pwg
"$GATE/tools/run-stage.sh" stage5-blocked stage5.png
"$GATE/tools/run-stage.sh" jobs
```

Inspect aborted jobs and the uncertainty latch before recovery. **Hold the Power
button until the Power LED goes out, then press once to turn it on.** Unplugging
USB does not power-cycle this battery-powered printer. Do not double-press the
button, which feeds/cuts tape. Restart only the candidate after the actual
power-cycle; do not replay failed jobs. Confirm clean 12 mm status, then explicitly
submit the single new recovery label:

```sh
sudo docker restart ptouch-p710bt-acceptance
"$GATE/tools/run-stage.sh" status
"$GATE/tools/run-stage.sh" stage5-recovery stage5.png
```

On this confirmed closed run, load 9 mm and confirm status. Run trial 5a with the
same S5 fixture and an explicit **12 mm** request. Its capture must contain status
requests only, its job must abort, and it must not set output-uncertain.

```sh
"$GATE/tools/run-stage.sh" status
"$GATE/tools/run-stage.sh" stage5a-wrong-media stage5.png
```

For stage 6, disconnect **USB only**, leaving the already closed-run printer
powered. Confirm the candidate is unavailable. Submit the S6 request **once**;
its template stops after job acceptance so it can remain queued. Record its job
ID from the IPP log and inspect pending state. Reconnect USB and confirm 9 mm;
wait for that same job to finish, without resubmitting it:

```sh
"$GATE/tools/run-stage.sh" status
"$GATE/tools/run-stage.sh" stage6-submit-offline stage6.png
"$GATE/tools/run-stage.sh" jobs
# After USB reconnect, substitute the accepted ID from stage6-submit-offline.ipp.log:
"$GATE/tools/run-stage.sh" wait-completed "" "$STAGE6_JOB_ID"
"$GATE/tools/run-stage.sh" status
```

The offline submission/wait slices are separate. Extract the entire interval
from before submission through completion (including intermediate status queries)
to avoid losing output emitted immediately after USB reconnect:

```sh
python3 "$GATE/tools/capture-stage6.py"
python3 "$GATE/tools/analyse.py" "$GATE/physical-evidence/stage6.prn"
```

Load 18 mm only after stage 6 is confirmed closed,
confirm media/status, and print S7 once:

```sh
"$GATE/tools/run-stage.sh" status
"$GATE/tools/run-stage.sh" stage7 stage7.png
"$GATE/tools/run-stage.sh" jobs
```

## Evidence and restoration

Retain `physical-evidence/usb-out.bin`, `usb-io.jsonl` (incoming status packets
and results), per-stage offsets/captures/analysis, IPP responses/job IDs/states,
candidate logs, image/config records, timestamps, and operator observations of
leader waste, separation, held ends, lengths, orientation, and intact outlines.
Hash the final bundle. Status-only captures must not contain page output.
Socket captures establish command/fixture correctness, not physical acceptance.
P710BT 6/24 mm remain software/documentation coverage, independent of this gate.

After stage 7 confirms a closed run and receiving-ready, save candidate logs,
stop/remove only `ptouch-p710bt-acceptance`, and restart each previously running
service that was explicitly yielded. Keep original image and configuration
unchanged, and verify restored queue/service state. Do not send a restoration
helper label. If a failure leaves a held or uncertain run, stop and report it;
obtain the recovery decision before returning USB to another process. Do not
silently change page flags, finishing policies, or repeat a trial.

After confirming closed/ready, run:

```sh
sudo docker logs ptouch-p710bt-acceptance > "$GATE/physical-evidence/candidate.log" 2>&1
sudo docker stop ptouch-p710bt-acceptance
sudo docker rm ptouch-p710bt-acceptance
# Only if original-running.txt is true and yielded-services.txt records ptouch:
sudo docker start ptouch
sudo docker inspect ptouch > "$GATE/physical-evidence/restored-service.json"
sudo docker exec ptouch ptouch-app status -d ptouch > "$GATE/physical-evidence/restored-status.txt"
```

Restore any other yielded owner using its recorded command. Compare original and
restored image/configuration/running state; retain the final evidence hashes
before copying the bundle back to openclaw. No restart is required for an
original service that remained running throughout.

Record successful integrated scope only after the operator observations and
captures pass every criterion. Keep this change local until its specifications
are synced and archived; production rollout remains a separate action.
