# BMS V1 Software Validation Matrix

状态：Engineering Closure M3；requirement/function -> repository evidence 映射。

证据分级：`production-C simulator` 表示 ARMCC5 编译实际 production C 并在 Keil Simulator 执行；`static verifier` 表示源码/工程/日志约束；`target build` 表示 ARMCC5 对 STM32 target 的编译链接。三者都不是 REAL_HW evidence。

| ID | Requirement / Function | Implementation evidence | Test / verifier / scenario evidence | Current software result | Hardware boundary |
|---|---|---|---|---|---|
| SW-001 | policy 单一来源与 fail-closed validation | `firmware/Config/bms_policy.[ch]` | `verify_phase9.py`; trust-chain tooling separate | PASS — SIM policy | production parameters需 approved artifacts |
| SW-002 | clock/BSP configuration | `bms_config.h`, `bsp_clock.c`, Keil target | Phase 2 image；ARMCC5 build | PASS | clock waveform/PVT deferred |
| SW-003 | software I2C state/ACK/timeout/recovery | `soft_i2c.[ch]` | `test_phase2_soft_i2c.c`, `verify_phase2.py`; lower regressions | PASS | electrical timing deferred |
| SW-004 | BQ CRC transport与write-finalization ambiguity | `bq76940.c`, `crc8_bq76940.c` | `test_phase3_transport.c`, `test_phase3_decode.c`; Phase 3/7 | PASS | physical ACK/STOP/W1C deferred |
| SW-005 | AFE startup safe-off/full config/calibration | `bms_afe_startup.[ch]`, `main.c` | `test_phase8_afe_startup.c`; split `phase8_afe` | PASS | real wake/register/electrical behavior deferred |
| SW-006 | 13S measurement mapping/conversion | `bq76940_measurement.[ch]` | `test_phase4_mapping.c`, `test_phase4_measurement.c`; split Phase 4 | PASS | accuracy/channel wiring deferred |
| SW-007 | NTC monotonic table/interpolation | `bms_ntc.[ch]`, SIM table in `bms_policy.c` | `test_phase8_data.c`; policy validator | PASS — SIM curve | real NTC curve/accuracy deferred |
| SW-008 | coherent measurement publication | `bms_data.[ch]`, `bms_sample.[ch]` | Phase 8 data/sample images；previous-good/partial-fail tests | PASS | preemptive target timing deferred |
| SW-009 | sample generation / AFE generation | `sample_sequence`, `afe_generation`, XREADY guards | Phase 8 provenance guard；Phase 9 verifier | PASS | real XREADY source deferred |
| SW-010 | validity vs freshness / DATA_STALE | metadata/stale latch in `bms_data.c`; State logic | SIM-17；Phase 8 stale/wrap tests；stress invariant | PASS | real timing/load deferred |
| SW-011 | State operational classification | `bms_state.[ch]` | SIM-01..04, SIM-32；`test_phase9.c` | PASS | real current/measurement inputs deferred |
| SW-012 | SW OV/UV/OC/temperature protection | State condition/debounce/hysteresis/actions | SIM-05,07,09,10,13..16；Phase 9 logic category | PASS — SIM thresholds | real policy/physical effect deferred |
| SW-013 | stale State compare-and-publish | `BMS_State_PublishIfCurrent` | Phase 9 stale-publish logic + verifier fragments | PASS | real scheduler contention deferred |
| SW-014 | HW/AFE fault ownership and directional actions | `bms_protect.[ch]`, `bms_safety.h` | Phase 7 production-C image；SIM-06/08/11/12/31；static writer checks | PASS | actual HW trip/events deferred |
| SW-015 | ALERT lost-edge/stuck-level service | EXTI ISR + `Task_Protect` bounded retry | Phase 7 review simulator: startup-high/long-high/comm failures | PASS | PB1 waveform deferred |
| SW-016 | CC_READY queue/mailbox/W1C ordering | `BMS_Protect_PushCcSample`, latest CC | Phase 7 H-02 tests；Phase 8 current-epoch tests | PASS | physical CC cadence/W1C deferred |
| SW-017 | FET directional arbitration + sole writer | `bms_fet_manager.c` | Phase 9 FET tests；SIM-22；writer/revision verifier | PASS — register logic | MOS conduction deferred |
| SW-018 | FET request/revision/readback race guards | input revisions，pre/post revalidation，quarantine | targeted Protect-revision race；readback corruption tests | PASS | bus/MOS timing deferred |
| SW-019 | 10-phase runtime XREADY recovery | `bms_recovery.[ch]`, Protect W1C, Sample provenance | SIM-20/21；Phase 9 recovery category；phase-list verifier | PASS | real XREADY/W1C/settle deferred |
| SW-020 | calibration provenance | generation/revision/post-clear evidence | targeted handoff-generation race；Phase 8 provenance guard | PASS | actual gain/offset accuracy deferred |
| SW-021 | HW fault recovery request/ack | `bms_hw_recovery.c`, Protect service | Phase 9 HW handshake tests；targeted new-event race | PASS | physical source recovery deferred |
| SW-022 | per-task heartbeat / health | `bms_health.[ch]`, bounded waits | SIM-23/24；Phase 9 health category | PASS | real stall/scheduler timing deferred |
| SW-023 | IWDG sole feeder/arm logic | `Task_State`, `bsp_iwdg.c` | static sole-feeder verifier；health simulator | PASS — logic/target binding | actual LSI/reset deferred |
| SW-024 | SOC integer accumulation/correction | `bms_soc.[ch]` | SIM-26..28；continuation SOC category；50k stress | PASS | Rsense/current accuracy deferred |
| SW-025 | balance selection/single writer/readback | `bms_balance.[ch]` | SIM-25；continuation balance；stress；writer verifier | PASS — register logic | balance current/thermal deferred |
| SW-026 | CAN explicit protocol encoding | `bms_can.[ch]` | continuation CAN category；SIM-30；frame/service cases | PASS | physical bus deferred |
| SW-027 | bxCAN target binding | `bsp_can.[ch]`, Keil SPL inclusion | `verify_phase9.py` timing/filter/ISR check；ARMCC5 target build | PASS — compiled/static | transceiver/bus deferred |
| SW-028 | CAN safety authority boundary | source-specific request only | verifier rejects direct BQ/IWDG/FET effects；invalid command tests | PASS | service process/hardware operator deferred |
| SW-029 | A/B explicit codec + CRC32/newest-valid | `bms_persistence.[ch]` | SIM-29；continuation codec | PASS | silicon Flash deferred |
| SW-030 | physical target adapter / commit-last order | `bsp_flash.c`, target persistence service | SIM-31/32 power-cut/corruption；static page restriction | PASS — software transaction | real brownout/endurance deferred |
| SW-031 | randomized long-duration invariants | production State/SOC/Balance/Persistence code | `test_stress.c`: 50,000 iterations, 600,000,000 ms, 512 transactions, 381 cuts, 0 failures | PASS | not wall-clock hardware run |
| SW-032 | targeted concurrency races | revision/generation/request identity | 3/3 races in `test_phase9.c` | PASS | real preemption/ISR contention deferred |
| SW-033 | lower-phase regression | Phase 4/6/7/8 production-C images | `run_phase8_split_simulators.ps1`; six images failures=0 | PASS | simulator only |
| SW-034 | Phase 8 artifact trust chain | strict canonical JSON/v2 schemas/manifest validator | `tools/phase8/test_validate_blocker_artifact.py`: 49 tests | PASS — tooling | missing approved production artifacts仍 blocker |
| SW-035 | UART target binding | `bsp_uart.[ch]` | ARMCC5 build + Phase 9 static binding check | PASS — compiled/static | waveform deferred |
| SW-036 | read-only UART observability | `bms_debug.[ch]`, CANTx task service | Phase 9 verifier checks key snapshot getters/no UART read；ARMCC5 0/0 | PASS — software | live line/waveform deferred |
| SW-037 | FreeRTOS objects/tasks/hooks | `app_rtos.[ch]`, hooks, `FreeRTOSConfig.h` | Phase 6 image；production map/callgraph | PASS — static/simulator | target heap/stack watermark deferred |
| SW-038 | Flash/IROM/RAM boundaries | `bms_memory_map.h`, Keil target | compile assertions；map `Max 0xF400`; verifier RW+ZI <20 KiB | PASS — link layout | actual part identity deferred |
| SW-039 | ARMCC5 production Clean/Rebuild | Keil `BMS_V1` target | `firmware/Project/Keil/Build/BMS_V1_Phase8_build.log` | PASS — 0 errors / 0 warnings at M3 build | target execution/hardware deferred |
| SW-040 | architecture negative constraints | writer scans/no generic clear-all/authority checks | `verify_phase9.py` | PASS | independent review not claimed |

## Authoritative execution entries

从仓库根目录执行：

```powershell
& firmware\Tests\build_phase9.ps1
& firmware\Tests\build_phase8.ps1
& firmware\Tests\run_phase8_split_simulators.ps1
python -m unittest tools.phase8.test_validate_blocker_artifact
python firmware\Tests\verify_phase9.py
```

说明：`build_phase8.ps1` 包含 frozen real-hardware artifact gate。当前没有 approved NTC/AFE artifacts，因此其真实硬件 Hard Gate 预期仍是 BLOCKED；这与 production Clean/Rebuild 和 lower-phase tests PASS 并不矛盾，也不能为了 M3 修改 verifier 让它接受 simulation policy。

## Evidence primary artifacts

- `firmware/Tests/Build/Phase9/phase9_simulator.log`
- `firmware/Tests/Build/Phase8/phase8_split_simulator.log`
- `firmware/Tests/Build/Phase8/phase8_build.log`
- `firmware/Project/Keil/Build/BMS_V1_Phase8_build.log`
- `firmware/Tests/Build/Phase8/BMS_V1_Phase8_production.map`
- `deliverables/simulation/BMS_V1_Simulation_RC_Report.md`
- `deliverables/release/BMS_V1_Software_Release_Baseline.md`
