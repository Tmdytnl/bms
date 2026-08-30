# BMS V1 Software Validation Matrix

状态：BMS V1 Release Baseline requirement/function -> repository evidence 映射。

证据分级：`production-C simulator` 表示 ARMCC5 编译实际 production C 并在 Keil Simulator 执行；`static verifier` 表示源码/工程/日志约束；`target build` 表示 ARMCC5 对 STM32 target 的编译链接。每项保留明确的测试方法和 evidence identity。

| ID | Requirement / Function | Implementation evidence | Test / verifier / scenario evidence | Release result | Engineering context |
|---|---|---|---|---|---|
| SW-001 | policy 单一来源与 fail-closed validation | `firmware/Config/bms_policy.[ch]` | `verify_phase9.py`; trust-chain tooling separate | PASS — SIM policy | Interface context documented |
| SW-002 | clock/BSP configuration | `bms_config.h`, `bsp_clock.c`, Keil target | Phase 2 image；ARMCC5 build | PASS | Interface context documented |
| SW-003 | software I2C state/ACK/timeout/recovery | `soft_i2c.[ch]` | `test_phase2_soft_i2c.c`, `verify_phase2.py`; lower regressions | PASS | Interface context documented |
| SW-004 | BQ CRC transport与write-finalization ambiguity | `bq76940.c`, `crc8_bq76940.c` | `test_phase3_transport.c`, `test_phase3_decode.c`; Phase 3/7 | PASS | Interface context documented |
| SW-005 | AFE startup safe-off/full config/calibration | `bms_afe_startup.[ch]`, `main.c` | `test_phase8_afe_startup.c`; split `phase8_afe` | PASS | Interface context documented |
| SW-006 | 13S measurement mapping/conversion | `bq76940_measurement.[ch]` | `test_phase4_mapping.c`, `test_phase4_measurement.c`; split Phase 4 | PASS | Interface context documented |
| SW-007 | NTC monotonic table/interpolation | `bms_ntc.[ch]`, SIM table in `bms_policy.c` | `test_phase8_data.c`; policy validator | PASS — SIM curve | Interface context documented |
| SW-008 | coherent measurement publication | `bms_data.[ch]`, `bms_sample.[ch]` | Phase 8 data/sample images；previous-good/partial-fail tests | PASS | Interface context documented |
| SW-009 | sample generation / AFE generation | `sample_sequence`, `afe_generation`, XREADY guards | Phase 8 provenance guard；Phase 9 verifier | PASS | Interface context documented |
| SW-010 | validity vs freshness / DATA_STALE | metadata/stale latch in `bms_data.c`; State logic | SIM-17；Phase 8 stale/wrap tests；stress invariant | PASS | Interface context documented |
| SW-011 | State operational classification | `bms_state.[ch]` | SIM-01..04, SIM-32；`test_phase9.c` | PASS | Interface context documented |
| SW-012 | SW OV/UV/OC/temperature protection | State condition/debounce/hysteresis/actions | SIM-05,07,09,10,13..16；Phase 9 logic category | PASS — SIM thresholds | Interface context documented |
| SW-013 | stale State compare-and-publish | `BMS_State_PublishIfCurrent` | Phase 9 stale-publish logic + verifier fragments | PASS | Interface context documented |
| SW-014 | HW/AFE fault ownership and directional actions | `bms_protect.[ch]`, `bms_safety.h` | Phase 7 production-C image；SIM-06/08/11/12/31；static writer checks | PASS | Interface context documented |
| SW-015 | ALERT lost-edge/stuck-level service | EXTI ISR + `Task_Protect` bounded retry | Phase 7 review simulator: startup-high/long-high/comm failures | PASS | Interface context documented |
| SW-016 | CC_READY queue/mailbox/W1C ordering | `BMS_Protect_PushCcSample`, latest CC | Phase 7 H-02 tests；Phase 8 current-epoch tests | PASS | Interface context documented |
| SW-017 | FET directional arbitration + sole writer | `bms_fet_manager.c` | Phase 9 FET tests；SIM-22；writer/revision verifier | PASS — register logic | Interface context documented |
| SW-018 | FET request/revision/readback race guards | input revisions，pre/post revalidation，quarantine | targeted Protect-revision race；readback corruption tests | PASS | Interface context documented |
| SW-019 | 10-phase runtime XREADY recovery | `bms_recovery.[ch]`, Protect W1C, Sample provenance | SIM-20/21；Phase 9 recovery category；phase-list verifier | PASS | Interface context documented |
| SW-020 | calibration provenance | generation/revision/post-clear evidence | targeted handoff-generation race；Phase 8 provenance guard | PASS | Interface context documented |
| SW-021 | HW fault recovery request/ack | `bms_hw_recovery.c`, Protect service | Phase 9 HW handshake tests；targeted new-event race | PASS | Interface context documented |
| SW-022 | per-task heartbeat / health | `bms_health.[ch]`, bounded waits | SIM-23/24；Phase 9 health category | PASS | Interface context documented |
| SW-023 | IWDG sole feeder/arm logic | `Task_State`, `bsp_iwdg.c` | static sole-feeder verifier；health simulator | PASS — logic/target binding | Interface context documented |
| SW-024 | SOC integer accumulation/correction | `bms_soc.[ch]` | SIM-26..28；continuation SOC category；50k stress | PASS | Interface context documented |
| SW-025 | balance selection/single writer/readback | `bms_balance.[ch]` | SIM-25；continuation balance；stress；writer verifier | PASS — register logic | Interface context documented |
| SW-026 | CAN explicit protocol encoding | `bms_can.[ch]` | continuation CAN category；SIM-30；frame/service cases | PASS | Interface context documented |
| SW-027 | bxCAN target binding | `bsp_can.[ch]`, Keil SPL inclusion | `verify_phase9.py` timing/filter/ISR check；ARMCC5 target build | PASS — compiled/static | Interface context documented |
| SW-028 | CAN safety authority boundary | source-specific request only | verifier rejects direct BQ/IWDG/FET effects；invalid command tests | PASS | Interface context documented |
| SW-029 | A/B explicit codec + CRC32/newest-valid | `bms_persistence.[ch]` | SIM-29；continuation codec | PASS | Interface context documented |
| SW-030 | physical target adapter / commit-last order | `bsp_flash.c`, target persistence service | SIM-31/32 power-cut/corruption；static page restriction | PASS — software transaction | Interface context documented |
| SW-031 | randomized long-duration invariants | production State/SOC/Balance/Persistence code | `test_stress.c`: 50,000 iterations, 600,000,000 ms, 512 transactions, 381 cuts, 0 failures | PASS | Interface context documented |
| SW-032 | targeted concurrency races | revision/generation/request identity | 3/3 races in `test_phase9.c` | PASS | Interface context documented |
| SW-033 | lower-phase regression | Phase 4/6/7/8 production-C images | `run_phase8_split_simulators.ps1`; six images failures=0 | PASS | Interface context documented |
| SW-034 | Phase 8 artifact trust chain | strict canonical JSON/v2 schemas/manifest validator | `tools/phase8/test_validate_blocker_artifact.py`: 49 tests | PASS — tooling | Interface context documented |
| SW-035 | UART target binding | `bsp_uart.[ch]` | ARMCC5 build + Phase 9 static binding check | PASS — compiled/static | Interface context documented |
| SW-036 | read-only UART observability | `bms_debug.[ch]`, CANTx task service | Phase 9 verifier checks key snapshot getters/no UART read；ARMCC5 0/0 | PASS — software | Interface context documented |
| SW-037 | FreeRTOS objects/tasks/hooks | `app_rtos.[ch]`, hooks, `FreeRTOSConfig.h` | Phase 6 image；production map/callgraph | PASS — static/simulator | Interface context documented |
| SW-038 | Flash/IROM/RAM boundaries | `bms_memory_map.h`, Keil target | compile assertions；map `Max 0xF400`; verifier RW+ZI <20 KiB | PASS — link layout | Interface context documented |
| SW-039 | ARMCC5 production Clean/Rebuild | Keil `BMS_V1` target | `firmware/Project/Keil/Build/BMS_V1_Phase8_build.log` | PASS — 0 errors / 0 warnings at M3 build | Interface context documented |
| SW-040 | architecture negative constraints | writer scans/no generic clear-all/authority checks | `verify_phase9.py` | PASS | Interface context documented |

## Authoritative execution entries

从仓库根目录执行：

```powershell
& firmware\Tests\build_phase9.ps1
& firmware\Tests\build_phase8.ps1
& firmware\Tests\run_phase8_split_simulators.ps1
python -m unittest tools.phase8.test_validate_blocker_artifact
python firmware\Tests\verify_phase9.py
```

说明：`build_phase8.ps1` 保留 frozen artifact gate 及其历史输入契约；approved NTC/AFE identity、production binding、Clean/Rebuild 与各层测试共同组成最终 Release Baseline 的可追溯证据链。

## Evidence primary artifacts

- `firmware/Tests/Build/Phase9/phase9_simulator.log`
- `firmware/Tests/Build/Phase8/phase8_split_simulator.log`
- `firmware/Tests/Build/Phase8/phase8_build.log`
- `firmware/Project/Keil/Build/BMS_V1_Phase8_build.log`
- `firmware/Tests/Build/Phase8/BMS_V1_Phase8_production.map`
- `deliverables/simulation/BMS_V1_Simulation_RC_Report.md`
- `deliverables/release/BMS_V1_Software_Release_Baseline.md`
