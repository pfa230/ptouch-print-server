# P710BT validation

## Prior direct-USB geometry evidence

[verified] On 2026-10-13, all 22 probe source, rectangle fixture/capture, and status-log files were preserved from the development VM's `/tmp` into the external bundle `openclaw:/home/pfa/.local/share/ptouch-validation/p710bt-20261013/`. `SHA256SUMS` and `manifest.json` record every retained file. No source files were missing at apply time. These are local VM paths, not paths on `vault`.

The three widest captures match the design's SHA-256 values:

| Tape | Capture | SHA-256 |
| --- | --- | --- |
| 9 mm | `p710bt-9mm/03-52.prn` | `711d2337f43c23fcd3eea84dcf5527cc03ef11451c02336f2e1b820b0353e323` |
| 12 mm | `p710bt-rectangles/03-76.prn` | `7eb5be170e12c0f7c574c725b998655efb6f5e6ba6791aedc4d98e1e6bf13718` |
| 18 mm | `p710bt-18mm/03-120.prn` | `bc07c514b4cbdf0978e42bb75e12d80e54a96a4dfe9ae6d2a63964718032b591` |

The operator reported all three separate continuous rectangles printed fine on each loaded cassette: 50/51/52 dots on 9 mm, 74/75/76 dots on 12 mm, and 112/116/120 dots on 18 mm. The first dashed 76-dot fixture is separate from the later successful continuous-rectangle trial. These observations establish the tested geometry; they do not certify integrated finishing, continuation, or recovery. Physical 6/24 mm coverage remains untested.

## Integrated implementation

[verified] Full configure/build/CTest in the pinned `ptouch-review-cups:20261013` development image passed all 12 tests. Its dependencies are PAPPL 1.4.11 and patched CUPS 2.4.2-3+deb12u9. Core-only configure/build/CTest separately passed all six tests. The local deployment build `ptouch-p710bt:local` passed its embedded 12-test suite, and both `--model PT-2730` and `--model PT-P710BT` runtime IPP/socket image smokes passed. CI invokes both smoke selections before publication.

[verified] Production-callback checks cover P710BT's exact fresh/held-run setup bytes, destination heights, cross-batch page indication, literal PackBits lines, four policies, unknown-count mixed heights, cancellation, fragmented readiness, and conservative failure cleanup. The P710BT status fixture is seeded from the retained actual 12 mm snapshot and reproduces its printing/completed/receiving-ready replies. Both-model PNG checks preserve exact outlines across all five software widths and reject invalid geometry and input without output.

[verified] Baseline `4de4587e2f1f3c17ea67d1a28c96d147b50d3283` and candidate PT-2730 production-callback suites emitted byte-identical accepted transcripts, including PNG/raster policy cases, status requests, transfer-failure cases, and the 65/999-copy counting cases: 4,180,474 bytes, SHA-256 `cc9986ca8e2beaea34ad7102c42867e887ebc6d3533fd1b7d6d797ab2046c4b4`. Test-local instrumentation was confined to external source snapshots. PT-2730's emitted print policy was preserved; no additional legacy physical labels are required by this change.

Successful logs, the two legacy transcripts, prepared fixtures, and candidate image are retained in the external validation bundle. At this preparation stage, integrated hardware acceptance was still pending and no new physical output had been sent; the completed acceptance is recorded below.

## KISS review

[verified] The independent implementation review found no speculative overengineering or runtime defect; its real-status fixture gap was corrected and checked. The implementation keeps two explicit model choices, pure command builders, canonical geometry, and the existing constant-size batch lifecycle. It introduces no printer profile framework, second planner, page buffer, dependency, or automatic protocol fallback. Hardware acceptance remains a separate required gate.

## Prepared hardware gate

[verified] The [exact gate](../../../../tests/P710BT-ACCEPTANCE.md) was prepared before explicit authorization. Its candidate image, tools, requests, raw pixel expectations, socket captures, and successful software logs are retained at `openclaw:/home/pfa/.local/share/ptouch-validation/p710bt-20261013/integrated-gate/`; all 96 `SHA256SUMS` entries were checked before and after preservation. The candidate image is `ptouch-p710bt:local`, image ID `sha256:501a839dae681174feb7759e9b6ce2769d93d0e6b21104f23602e7642a9df89b`.

The isolated socket gate reproduced 11 complete simulated labels with exact pixels, 180/216-row heights, fresh and held-run page indications, a 60-row incomplete fragment without finalization, and a blocked follower with zero output. A separate affected-width run confirmed the zero-output wrong-media trial on simulated 9 mm media and successful 52/120-dot fixtures afterward. The initial 9 mm preparation request inherited incompatible margins; its templates now specify the existing canonical margins, preserving strict validation. Simulated cassette changes and process restarts do not establish real offline or power-cycle recovery. Bundled ipptool and capture-shim runtime dependencies were checked with no USB access.

The concrete physical bound is nine complete labels on 12 mm, one on 9 mm, and one on 18 mm, plus the incomplete/no-output trials. Complete content totals 28.448 cm, excluding leaders, feed allowances, and fragment waste. No SSH operation, USB output, physical label, or production rollout occurred during preparation. At this preparation stage, tasks 5.1–5.4 and final reconciliation were pending; their completed results follow.

## Initial physical-gate execution record

The following paragraphs preserve intermediate observations; the authoritative completed outcome follows in “Completed integrated acceptance.”

The user authorized the concrete gate with “ready” on 2026-10-13. The active bundle is `vault:/tmp/p710bt-acceptance.7nL5Ss/`, candidate container `ptouch-p710bt-acceptance` on local port 18000. All 96 transferred file hashes passed. The original `ptouch` service remains running and unchanged on PT-2730; its image ID is `sha256:acd1f2d0e5b9ebecdd63bd838cc85e70aa0fadfa0c78b2feb6c68706f7419686`. No competing USB owner was found for the sole P710BT, `04f9:20af`, bus 003/device 024. The user was instructed to keep USB power connected and other printing clients closed. No auto-off setting was changed.

[verified] Preflight confirmed clean idle status, 12 mm media with canonical margins, and an empty candidate queue. Only status requests were emitted before printing. USB power/presence and clean receiving-ready status persisted through a measured 10-second idle interval; the full interval between stage-1 completion and stage-2 submission was longer due to capture inspection and operator observation.

[verified] Stage 1 completed IPP job 1 with two 180-row pages, indications 0/1, two FF commands, chain on, and no EJECT. Its 7,368-byte capture matches the prepared socket capture after removing status requests; both receiving-ready notifications were consumed without errors. Operator observation: “leader and properly printed rectangle label.” This matches the free leader/first label expected with the last label held; its physical release is checked by stage 2.

[verified] Stage 2 completed IPP job 2 with two 180-row pages, indications 1/1, no invalidate/init, one FF and one late chain-off/EJECT. Its 7,270-byte capture matches the prepared socket capture after removing status requests; all incoming statuses are clean, with four receiving-ready notifications across stages 1/2. Current physical evidence was copied locally to `/tmp/p710bt-live-evidence`. Physical observations of prior-label release, no new discarded leader, and final separation remain pending. Four complete labels have been submitted in total; stages 3–7 have not run. The candidate remains running for the authorized gate.

The user then explicitly requested “lets repeat step 1”, authorizing one extra two-label chain-only batch. The overall label bound increases from 11 to 13; this is a manual operator-requested repeat, not an automatic retry. Original stage-2 physical observations remain pending.

[verified] The repeat completed as IPP job 3 after idle/12 mm preflight, with two 180-row pages, indications 0/1, fresh invalidate/init, two FF commands, and no EJECT. Its normalized 7,368-byte capture matches the original stage-1 socket reference; all incoming statuses remain clean, with six receiving-ready notifications across the three batches. Original stage-1 evidence was preserved in `physical-evidence/stage1-original/`; the new artifacts are `stage1-repeat1.*`, and the original `stage1.*` files were restored. Updated physical evidence was copied to `/tmp/p710bt-live-evidence/`.

Six complete labels have now been submitted. The operator answered “yes” to confirm a leader and one free rectangle label, with the second label still held, for the repeated stage 1. This supplementary chain-only trial passes its capture, status, and physical criteria. The repeated batch leaves a healthy held run; stages 3–7 remain unexecuted, and the original stage-3 fresh/closed starting condition requires an explicit closing decision before continuation. Original stage-2 physical observations remain pending. No further output was sent.

## Completed integrated acceptance

[verified] The user subsequently requested “now step 2”, authorizing another two-label repeat and a **15-label total bound**. Repeated stage 2 completed as job 4 with no invalidate/init and indications 1/1, releasing the held S1 and cutting both S2 labels free; the operator reported “S1 and 2 S2, all cut off.” The normalized capture is 7,270 bytes and matches the socket reference. Original/repeated evidence was preserved separately. This repeat restored the closed-run starting condition for stage 3.

The USB node change from 024 to 026 was investigated before repeating stage 2. Kernel timestamps show the last enumeration at monotonic 1,063,170.866508, **before** repeated stage 1's first output at 1,063,172.620679860. There was no disconnect during or after that repeated held batch. Its continuation was therefore valid; no flags were changed and no output was retried automatically. `stage2-repeat-preflight-continuity.json` retains that evidence.

| Trial | Job / outcome | Capture and operator evidence |
| --- | --- | --- |
| 3: none | First process, job 5 completed two 180-row pages; 0/1, two FF, no EJECT | 7,368 normalized bytes; exact pixels. Operator confirmed leader cut off, joined S3 labels, end held. |
| 4: final-only PWG | First process, job 6 completed 180/216-row pages with declared total zero; 1/1, FF then late chain-off/EJECT | 7,990 normalized bytes; exact pixels. Operator confirmed released S3 strip and a separate joined S41/S42 strip, second label longer, final end cut. |
| 5: incomplete | First process, job 7 aborted after 60/180 raster rows | 1,338 normalized bytes, no FF/EJECT. No complete label finalized. |
| 5: blocked follower | First process, job 8 aborted with explicit recovery-required message | Zero output bytes; no automatic reset, feed, cut, or replay. |
| 5: recovery | Operator confirmed the instructed button power-cycle; only the candidate restarted. New process, job 1 completed one 76-dot S5 label | 3,743 normalized bytes, fresh init/indication 0 and EJECT. Operator reported “leader and S5,” with no extra replay reported. |
| 5a: wrong media | With confirmed 9 mm tape, job 2 explicitly requesting 12 mm aborted | Only two status requests; no initialization, setup, raster, feed, or cut. The subsequent valid job remained usable. |
| 6: offline queue | Operator disconnected only USB with power retained. Job 3 accepted once, waited for USB, and completed after reconnect | Zero output while disconnected; whole reconnect interval is 3,743 normalized bytes with exact 52-dot pixels and EJECT. Operator reported “leader and S6” and acknowledged the requested intact-outline check before loading 18 mm. |
| 7: 18 mm | New process, job 4 completed one 120-dot S7 label | 3,743 normalized bytes, exact pixels, fresh indication 0 and EJECT. Operator explicitly confirmed all four rectangle sides complete and label cut free. |

All positive/negative captures match their prepared socket references after removing only status requests. The incoming packet log contains 15 receiving-ready notifications for the 15 complete labels and no printer error bits. There were **13 complete labels on 12 mm, one on 9 mm, and one on 18 mm**. Complete printed content totals 38.608 cm; actual consumed tape/leader/feed waste was not measured. P710BT 6/24 mm remain software-only, and Bluetooth/360 dpi remain outside the verified scope.

[verified] Final status was clean/idle on 18 mm with all live-process jobs terminal. The candidate was stopped and removed with no additional print output. The original service is running with the same image, Config, and HostConfig as recorded before the gate. Its StartedAt changed independently from 07:00:09 to 19:01:53 UTC during the session; the gate issued no stop/restart for that service, so uninterrupted original-service uptime is not claimed. The verified image/configuration preservation and final running state are retained in `restoration-verified.json`.

The final physical evidence was copied from `vault:/tmp/p710bt-acceptance.7nL5Ss/physical-evidence/` to `/tmp/p710bt-live-evidence/`; all 186 remote hashes matched on collection. Operator observations and updated hashes were added locally. Durable evidence belongs under `openclaw:/home/pfa/.local/share/ptouch-validation/p710bt-20261013/integrated-gate/physical-evidence/`. No production image rollout or push occurred.

## Final reconciliation

[verified] All three deltas were reviewed against the implementation, regressions, and completed hardware evidence. Main specifications were synced while preserving their Purpose sections and unaffected requirements. The KISS pass retained the existing driver lifecycle, canonical geometry, and two explicit model selections without new frameworks or dependencies. Integration and production rollout were subsequently authorized explicitly by the user.
