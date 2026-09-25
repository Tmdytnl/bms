# BMS V1 Tests

This directory contains historical phase checkpoints plus the final BMS V1
regression. Run commands from the repository root on
Windows with Python 3, ARMCC5 5.06u7 and Keil uVision 5.38 at the configured
paths.

## Final regression

```powershell
# Phase 9: 24 core scenarios + 3 races + 8 continuation scenarios + stress
& tests\build_phase9.ps1

# Check that the APP package and layer boundaries remain self-contained.
python tests\verify_architecture.py

# Artifact/manifest trust-chain tests.
python tools\phase8\test_validate_blocker_artifact.py

# Recheck current safety contracts, simulator markers, map and build evidence.
python tests\verify_phase9.py
```

Expected current totals are 32 deterministic scenarios, 3 targeted races,
50,000 stress iterations representing 600,000,000 ms, and 49 trust-chain unit
tests. The split lower regression must report all Phase 4/6/7/8 failure counts
as zero. The production log must report 0 errors / 0 warnings.

## Entrypoint semantics

- `build_phase9.ps1` executes the current 32-scenario/race/stress regression.
- `build_phase8.ps1` preserves the historical Phase 8 hard gate. Its six
  production-C images still build and pass in the simulator, while the old
  static gate expects the earlier layout and deliberately reports the earlier
  integration state. It is not a current acceptance command.
- `run_phase8_split_simulators.ps1` is a historical simulator workaround.
- `verify_phase1.py` through `verify_phase7.py` and the Phase 7 review runner are
  archival checkpoint tools. `build_phase7_review.ps1` still names removed
  `firmware/` paths and is not executable after the APP migration. Later source
  revisions can also make the frozen verifier manifests report drift.

Maps and logs embed checkout paths and timestamps. Semantic reproducibility is
required; byte-identical output across directories is not promised.

The Simulator executes production C with deterministic transport, RTOS and BSP
fakes, making fault injection, transaction interruption, generation-wrap and
race ordering repeatable. It is one method in the final evidence chain together
with static verifiers, ARMCC5 test images and the production Clean/Rebuild.
