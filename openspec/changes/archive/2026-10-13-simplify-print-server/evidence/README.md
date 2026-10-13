# Retained validation evidence

`ptouch-integrated-12mm.tar.gz` contains the authorized hardware session's
IPP requests, generated label fixtures, output offsets, transparent USB capture,
status packets, candidate logs, and original-service restoration records.
Compiled helper binaries and the container image are excluded. `usb-capture.c`
is the transparent bulk-transfer recorder used for that session.

The four `gate-24mm-*.prn` files are the earlier bounded direct-protocol
captures. Operator observations for both sessions are recorded in ../validation.md;
command captures alone are not proof of physical cuts.

`claude-implementation-review.txt` is the independent pre-fix review.
`named-raster-*` and `driver-png-*` retain failing reproductions and passing
regressions. `cups-review-fixes.txt` records a normal isolated driverless queue,
including fitting rounded Custom sizes, oversized rejection, PDF copies, and
exact standard media-col dimensions. `final-deploy.txt` and `final-smoke.txt` record the successful final runtime
build, all eleven CTest suites, and real IPP printing into an isolated sink.

## SHA-256

```
6976a9ad2173cd2384553dbbc796e9cd2d691e804b24d07a6c2614a2dcc1bb26  claude-implementation-review.txt
c2a12c6afb48d6845e50159b94549a68930d2b2cbc2f2a588fc1d7bdf6cf29f2  cups-review-fixes.txt
0ba921089301497c71781f0c93d1b883ee1e77810869f33f0ccd4da8f68db8fe  driver-png-before-driver.txt
a255b89275d5ffbfdd6fe02fe3eda0bf01cffbfc9d9bd38d43a20a90cd8fd187  driver-png-before.txt
d878c2f1b4d7b133d4ccb1cb2f5fc00662205b14b6f00e90c9ec54e816b54446  driver-png-final.txt
71443e9397730f2449eb690db02661cf2706fcde4052ef076141c725277dbd0d  driver-png-report.md
6182ee24a6399f3c346ef639f17fd19feec0612b2081a70d95c1d8f5c1ad56fd  final-deploy.txt
2fb62cf7c6539397952c24c3a3835d851e041c14d4334a280ea46344d4c31e31  final-openspec.txt
8e0907ac2e06ce6d980929a7bd976b292b614324f061e841c9a889ef089e802e  final-smoke.txt
f1dda2d3b836a41244c07bf179d8d4d6e24d3fc5af3125358b438756a79c985b  gate-24mm-chain-2.prn
e0a02dbc762f1f5ed4e78d945d0d73f31df4361d3aea275c0011a8a3f3a43be5  gate-24mm-each-2.prn
d20e3679e6f50a5df46a444908f9aef688f9f53dda1792485b91b9bca8e2ac70  gate-24mm-end-2.prn
2fc856162c8de5df5e7d29b64f0c78a1ddacf751bb9fc972a875ef8bdf39ed3a  gate-24mm-none-2.prn
2ee229a298a9899b409d4dd4a4d13b6016012646d2861fb65dc51458a7e55919  named-raster-after.txt
9d87b6f454ab216a2901e25823e1837fb278b4e237c812377e79d3735667e270  named-raster-bad-context.txt
53dfac86f7aeb46fc64a3ee3b44beb4deb2fc31daa8ddbf9029260341c75e9c9  named-raster-before.txt
2fa53e31ed148030bd15849808f0fb37f2622bda08baa69b25792807f44a1260  named-raster-clean.txt
fc6f3896959cd7c55b9a5f8996e7cd922238acbec52f51c5c389e647af66d5fb  named-raster-duplicate.txt
63f8ab34e450f6369310561dc2b13344cc47b472e9272b9aa1e1616c91f385af  named-raster-finishings-followup.txt
c40cc79aeb33f1ac458e1ec86ceed30adb33f991327d4b96e3b61b49ffef8488  ptouch-integrated-12mm.tar.gz
584aa8ecb351c08f15ed67169c186fb69db6eece4713c5c2d1ca914f6ccc8c31  usb-capture.c
```
