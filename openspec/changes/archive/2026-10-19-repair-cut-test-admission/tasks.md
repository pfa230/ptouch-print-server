# Tasks

## 1. Repair canceled-stream admission

- [x] 1.1 Fix the canceled-stream fixture/helper's upload coordination so PAPPL admits the job while the test retains control of its unfinished input, then preserve the existing cancellation-before-ownership and completion-after-cut assertions. Verify driver_finishings_p710bt and all CTests pass in the full Dockerfile.deploy build, and run the core and strict OpenSpec checks. Do not mark the container test complete based on core-only results; the loop's machine-local container gate must execute it.
