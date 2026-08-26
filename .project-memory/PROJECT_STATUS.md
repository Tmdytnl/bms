# BMS V1 Project Status

## Current Status

| Item | Status |
|---|---|
| Project | COMPLETE |
| Engineering Closure | COMPLETE |
| Release Baseline | ESTABLISHED |
| Open Blockers | NONE |
| Final Project Polish | COMPLETE |

当前版本已形成完整工程基线。Git、当前源码、测试与构建证据仍是事实来源；动态 HEAD 应在任务开始时直接从 Git 读取。

## Authoritative Baseline

- Starting HEAD: `714c4d63f8833d50b380feefbb841d633b4a6261`
- Finalization branch: `dsh/project-finalization`
- Release contract: `BMS-FINAL-POLISH-CONTRACT-V1`
- Release document: `deliverables/release/BMS_V1_Release_Baseline.md`

## Final Evidence

- Phase 9 core scenarios: 24 / 24 PASS
- Continuation scenarios: 8 / 8 PASS
- Total deterministic scenarios: 32 / 32 PASS
- Targeted races: 3 / 3 PASS
- Randomized stress: 50,000 iterations / 0 failures
- Persistence: 512 transactions / 381 injected power cuts
- Trust-chain tests: 49 / 49 PASS
- ARMCC5 Clean/Rebuild: 0 errors / 0 warnings
- Resource baseline: Code 53,288 B / RO 1,092 B / RW 364 B / ZI 16,508 B

## Frozen Architecture

- State classification is not FET permission.
- ProtectTask owns HW/AFE faults and runtime XREADY W1C.
- StateTask owns SW protection, DATA_STALE and RTOS_HEALTH.
- FET Manager is the scheduler-era SYS_CTRL2 CHG/DSG sole writer.
- StateTask is the sole IWDG feeder.
- BalanceTask is the scheduler-era CELLBAL sole writer.
- `BMS_Data` is diagnostic aggregation, not FET safety authority.
- Recovery remains phaseful and BOTH-inhibited until technical completion.
- `sample_sequence`、`afe_generation`、generation/revision、compare-and-publish 与 hardware recovery evidence contracts remain frozen.

## Navigation

- Project entry: `README.md`
- Release baseline: `deliverables/release/BMS_V1_Release_Baseline.md`
- Architecture and learning material: `docs/architecture/` and `docs/learning/`
- Regression entrypoints: `firmware/Tests/build_phase8.ps1` and `firmware/Tests/build_phase9.ps1`
