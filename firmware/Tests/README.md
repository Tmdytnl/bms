# Firmware Tests

This directory contains historical phase checkpoints plus the current M3
simulation-development regression. Run commands from the repository root on
Windows with Python 3, ARMCC5 5.06u7 and Keil uVision 5.38 at the configured
paths.

## Current M3 regression

```powershell
# Phase 9: 24 core scenarios + 3 races + 8 continuation scenarios + stress
& firmware\Tests\build_phase9.ps1

# Rebuild lower-phase images and the production target.
# Exit code 3 / NOT_EXECUTED is intentional because the monolithic simulator
# stage is skipped; compilation and production Clean/Rebuild must still pass.
& firmware\Tests\build_phase8.ps1 -SkipSimulator

# Execute the six lower-phase images separately. This avoids the known local
# uVision multi-LOAD simulator hang without changing test assertions.
& firmware\Tests\run_phase8_split_simulators.ps1

# Artifact/manifest trust-chain tests.
python tools\phase8\test_validate_blocker_artifact.py

# Recheck current architecture, simulator markers, map and build evidence.
python firmware\Tests\verify_phase9.py
```

Expected current totals are 32 deterministic scenarios, 3 targeted races,
50,000 stress iterations representing 600,000,000 ms, and 49 trust-chain unit
tests. The split lower regression must report all Phase 4/6/7/8 failure counts
as zero. The production log must report 0 errors / 0 warnings.

## Gate semantics

- `build_phase9.ps1` is the current software simulation-development gate.
- `build_phase8.ps1` preserves the frozen Phase 8 qualification contract. Its
  `Skip*` switches are diagnostic and deliberately cannot claim a hard-gate
  PASS. Missing approved product artifacts or REAL_HW evidence remain blockers
  for that frozen qualification gate.
- `run_phase8_split_simulators.ps1` is an execution workaround for the local
  multi-image simulator hang; it does not weaken or replace assertions.
- `verify_phase1.py` through `verify_phase7.py` and the Phase 7 review runner are
  historical checkpoint tools. Later source revisions can intentionally make
  their frozen manifests report drift.

Maps and logs embed checkout paths and timestamps. Semantic reproducibility is
required; byte-identical output across directories is not promised.

The Simulator executes production C with deterministic transport, RTOS and BSP
fakes where physical access is required. It does not validate target timing,
the BQ7694003 electrical behavior, FET power paths, real CAN/Flash/UART, watchdog
reset timing, calibration accuracy, thermal behavior or EMC. `REAL_HW validation
has not been performed by this Milestone.`
