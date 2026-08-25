# BMS V1 Configuration Guide

目标：说明当前 `SIM_POLICY_V1` 在哪里定义，以及未来如何替换为真实硬件/产品配置。M3 不创建“量产参数”，也不弱化 Phase 8 approved-artifact gate。

## 1. 配置层级

| Layer | Source | 用途 | 修改约束 |
|---|---|---|---|
| compile-time hardware identity/clock/pins | `firmware/Config/bms_config.h` | target part、13S、clock、BSP pins、software-I2C/sample基础常量 | 改 MCU/pin/clock会影响 BSP/Keil/interrupt，需独立硬件变更 review |
| Flash layout | `bms_memory_map.h` + Keil IROM | application/保留页 boundary | 三者必须一致；overlap触发 STOP，不可仅改一处 |
| runtime immutable policy | `bms_policy.[ch]` | simulation hardware值、保护/状态/health/SOC/balance/CAN/Flash policy | 通过 `BMS_Policy_Validate`; future应由 approved artifacts生成/审查 |
| baseline rationale | `docs/hardware/BMS_V1_模拟硬件参数与产品策略基线.md` | `SIM-HW-POLICY-V1` 人类可读来源 | simulation输入，不是产品认证 |
| toolchain/RTOS | `FreeRTOSConfig.h`, `app_rtos.h`, `.uvprojx` | heap、task stack/priority、queue depth、compiler/memory target | 资源/实时性变更需 Clean/Rebuild、map/callgraph/REAL watermark |

## 2. 配置项矩阵

| Group / Item | Where configured | Unit | Current simulation value | Hardware validation requirement | Risk if wrong |
|---|---|---:|---|---|---|
| Battery topology / chemistry | `bms_config.h`, baseline doc | text | 13S NMC | schematic/cell configuration | cell mapping、pack voltage、protection全部错误 |
| cell count | `BMS_CELL_COUNT`, policy `cell_count` | cells | 13 | VC wiring逐通道验证 | bitmap/mapping/out-of-bounds/错误阈值 |
| nominal capacity | `capacity_mah`, SOC policy | mAh | 20,000 | actual cell capacity/aging policy | SOC slope/full capacity错误 |
| Rsense | `rsense_uohm` | µΩ | 4,000 | BOM/Kelvin + multi-point calibration | current、OC、SOC全部按比例错误 |
| current polarity | `current_polarity` | +1/-1 | +1；positive=charge | controlled charge/discharge | CHG/DSG state/protection方向反转 |
| sample period | `BMS_SAMPLE_PERIOD_MS` | ms | 250 | target scheduling/I2C budget | stale、CPU/I2C load、CC identity影响 |
| temperature divider | `BMS_TEMPERATURE_SAMPLE_DIVIDER` | sample cycles | 8 (=2000 ms) | thermal dynamics/freshness | temperature过慢或过载 I2C |
| cell valid range | `BMS_CELL_VALID_*` | mV | 2000..5000 | AFE/input range | invalid/out-of-range错误分类 |
| startup cell range | `BMS_STARTUP_CELL_*` | mV | 2500..4300 | product startup policy | unsafe pack被接受或合法pack被拒绝 |
| AFE wake/settle | `bms_config.h`, `afe_startup` code | ms | 10 / 800 | BQ/board waveform/data-valid | stale configuration/calibration evidence |
| HW OV target/delay | `policy.afe_startup` | mV / s | 4250 / 2 | actual calibration + trip test | cell overcharge或nuisance trip |
| HW UV target/delay | same | mV / s | 2800 / 4 | actual trip test | overdischarge或nuisance trip |
| OCD | PROTECT2 fields in `afe_startup` | mV/A, ms | 42 mV≈10.5 A, 320 ms, RSNS=0 | real Rsense/current pulse | conductor/MOS protection错误 |
| SCD | PROTECT1 fields | mV/A, µs | 89 mV≈22.25 A, 200 µs, RSNS=0 | safe pulsed fixture | catastrophic current protection错误 |
| NTC model/table | `ntc_model_id`, `ntc_points` | Ω, 0.1°C | SIM_NTC_10K_B3950, 15 points | actual part/domain/accuracy artifact | temperature protection方向/点位错误 |
| SW OV | `policy.sw_ov` | mV, ms | 4200 trigger, 4100 recovery, 500 debounce, 2000 qualify | approved product policy + hardware tests | overcharge/nuisance trip |
| SW UV | `policy.sw_uv` | mV, ms | 3000/3200/500/2000 | approved policy | overdischarge/nuisance trip |
| charge OC | `policy.sw_oc_charge` | mA, ms | +5000/+4500/500/2000 | current calibration/product rating | charger/MOS风险或误动作 |
| discharge OC | `policy.sw_oc_discharge` | mA, ms | -10000/-9000/500/2000 | calibration/rating | load/MOS风险或误动作 |
| charge temperature | `policy.charge_temperature` | 0.1°C, ms | 0/5°C low；50/45°C high；1000/2000 | cell maker/NTC/thermal validation | unsafe charge温度窗口 |
| discharge temperature | `policy.discharge_temperature` | 0.1°C, ms | -20/-15°C；60/55°C；1000/2000 | product/thermal validation | unsafe discharge窗口 |
| freshness | `policy.freshness`, `BMS_*FRESH*` | ms/frames | V/I 1000, T 5000, recovery 2 frames | worst-case task/I2C timing | stale数据继续控制或频繁trip |
| AFE_COMM | `policy.afe_comm` | count/ms | 3 failures；no success 1000；latch 5000；3 successes recover | electrical error/load tests | comm fault过迟/过敏 |
| service reset qualify | `service_reset_qualify_ms` | ms | 2000 | operator/service process | stale request被接受或无法复位 |
| state startup timeout | `policy.state` | ms | 5000 | hardware startup worst case | premature FAULT或过晚fail |
| state current thresholds | `policy.state` | mA/ms | standby abs<100/1000；charge 300/500 exit150/1000；discharge -300/500 exit-150/1000 | current accuracy/use case | state抖动/分类错误；不直接替代FET权限 |
| health roster/window | static roster in `bms_policy.c` | ms | 300,750,300,2500,2500,2500,500 | target load/worst-case blocking | false reset或漏检task stall |
| IWDG | `policy.health`, `bsp_iwdg.c` | ms | nominal 4000；startup grace 5000 | actual LSI/PVT/reset test | reset过早/过晚；liveness coverage hole |
| SOC fallback/efficiency | `policy.soc` | permille/ms | initial500；charge995；discharge1000；period1000 | cell/coulomb calibration | SOC drift |
| SOC full correction | `policy.soc` | mV/mA/ms | min cell4180；0..500mA；60s | pack/charger validation | false 100% correction |
| SOC empty correction | `policy.soc` | mV/mA/ms | min cell3050；abs discharge<=1000mA；30s | pack/load validation | false 0% correction |
| balance eligibility | `policy.balance` | mV/0.1°C/mA | 4100 start; 20/10 delta; 4050 low stop; 0..45°C; abs<=3000 | cell/resistor/thermal | overheat、overdischarge、错误cell |
| balance topology | `policy.balance` | count/bool/ms | max2；adjacent false；rotation5000 | PCB/AFE/resistor thermal | 相邻热耦合/寄存器映射错误 |
| CAN bitrate/IDs | `policy.can`, `bms_config.h`, `bsp_can.c` | bit/s, ID | 500k；std；TX180..185；RX280 | transceiver/bus/network agreement | bus-off、filter错、interoperability |
| CAN command timeout | `policy.can` | ms | 1000 | service/network latency | stale command accepted |
| Flash A/B addresses | `policy.flash`, `bms_memory_map.h`, Keil IROM | address | F800/FC00, 1 KiB pages | actual MCU/boot/link map | erase application/overlap（STOP） |
| save policy | `policy.flash` | ms/permille | min 60000；SOC delta10 | endurance/power-loss tests | excessive wear或数据过旧 |
| RTOS heap | `FreeRTOSConfig.h` | bytes | 12 KiB | target minimum-ever-free heap | malloc failure/浪费静态RAM |
| task stacks | `app_rtos.h` | 32-bit words | 160/192/384/192/256/240/160 | `uxTaskGetStackHighWaterMark` on target | overflow/reset；static callgraph alone不足 |
| queue depths | `app_rtos.h` | elements | CAN TX24, RX12, CC8 | burst/load tests | drops、CC gap、memory excess |
| task priorities | `app_rtos.h` | FreeRTOS priority | 5/4/3/3/2/2/2 | latency/schedulability test | Protect starvation或low-task liveness |
| UART telemetry | `bms_debug.c` | ms | 1000 | live bitrate/latency/load | observability loss；不得成为安全依赖 |

## 3. 从 SIM_POLICY_V1 迁移到真实配置

### Step 1 — 冻结硬件身份

收集 board/schematic/BOM revision、确切 AFE part、cell topology、Rsense/NTC、FET/transceiver/Flash identity。不能只用口头数值。

### Step 2 — 完成 approved artifacts

使用 `deliverables/phase8/input_templates/` 的 v2 schemas、detached approval 与 gate manifest。NTC 与 AFE artifacts 必须共享 hardware identity；strict canonical JSON 与 projection hash由 `tools/phase8/validate_blocker_artifact.py` 检查。不要把 `SIM_POLICY_V1` 标成 approved production artifact。

### Step 3 — 生成/审查 policy

将 artifact values 显式映射到 `BMS_Policy_t`，保留 source-native units：mV、mA、µΩ、0.1°C、ms/s/µs。任何 rounding/threshold-table selection都记录 requested与selected，不隐藏离散化。

### Step 4 — 运行 structural validation

扩展/保持 `BMS_Policy_Validate()` 的 fail-closed checks：threshold/recovery ordering、cell count、NTC monotonicity、protect bytes、health/IWDG关系、balance limits、CAN/Flash layout。配置 invalid 时不得自动回到可能启 FET 的默认值。

### Step 5 — 全软件回归与 target build

运行 Phase 8 lower regressions、Phase 9 scenarios/races/stress、trust-chain、ARMCC5 Clean/Rebuild；记录新的 Code/RO/RW/ZI/map/callgraph。生产参数变化不应改变 frozen ownership，但会改变 expected scenario values，tests必须显式更新并审查。

### Step 6 — 分阶段 REAL_HW validation

按 bring-up guide 从 I2C、AFE、measurement、NTC/current、ALERT/protection、FET、IWDG、CAN、Flash推进。每一硬件矩阵行独立收证据；不能用一次“整机能跑”替代每项。

### Step 7 — release identity

新的 hardware-profile release 必须记录 artifact hash、firmware commit、board revision与hardware validation status，并与当前 simulation release并列，不覆盖历史证据。

## 4. 不允许的配置方式

- 通过 UART/CAN 动态修改 safety policy、直接 clear latch 或 enable FET；
- 在多个 `.c` 文件复制 threshold magic values；
- 改 Flash slot地址却不改/验证 Keil IROM和memory-map assertions；
- 用真实硬件测得的一个点反推整条 NTC/current/protection曲线而无误差/domain；
- 为让测试通过而弱化 `verify_phase8.py` 的 approved-artifact要求；
- 将 SIM 值改名为 production 而不改变 evidence identity。
