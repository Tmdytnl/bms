# BMS V1 Debugging Guide

组织方式：先确认 evidence layer，再按“症状 -> 优先观察 -> 检查 -> 定位”收敛。不要通过临时 direct register write、generic fault clear 或强制 FET enable 隐藏根因。

## 1. 先确定你在看哪一层

| Layer | 能证明 | 不能证明 |
|---|---|---|
| source/unit test | decision/encoding/边界逻辑 | target timing、electrical behavior |
| Keil Simulator | production-C deterministic path/race injections | physical I2C/ALERT/MOS/CAN/Flash |
| ARMCC5 build/map | target syntax/link/source inclusion/resources | runtime stack watermark/physical behavior |
| BQ register readback | AFE register state | MOS conduction/cell/current accuracy |
| scope/DMM/CAN analyzer | physical signals/values | software ownership本身，需与 snapshot关联 |

## 2. UART `BMS1` line

USART1：PA9 TX / PA10 RX，115200 8N1。正式 firmware 每秒由 CANTxTask 输出一条只读行；无 command parser。

```text
BMS1 t=... st=... cell=min/max pack=... cur=... temp=... vf=...
 seq/gen=... fault=active/latched inh=chg/dsg
 fet=requested/effective/observed/confirmed/transaction
 rec=phase/ready hb=... soc=permille/valid bal=request/confirmed/confirmed
 can=target_tx/drop/rx_valid/init_fail/busoff_recovery
 flash=save/io_fail/verify_fail/both_invalid
```

编码：

- `st`: 0 INIT, 1 STANDBY, 2 CHARGE, 3 DISCHARGE, 4 FAULT。
- `vf`: bit0 cell valid, bit1 current valid, bit2 temperature valid；bit4/5/6 对应 fresh。
- FET bit pair：bit0 CHG，bit1 DSG；transaction 0 confirmed-safe, 1 confirmed-applied, 2 unverified, 3 quarantined。
- `rec` phase：0 IDLE, 1 PRE_CLEAR_PREPARE, 2 PRE_CLEAR_READY, 3 WAIT_CLEAR_ACK, 4 POST_CLEAR_CONFIG, 5 POST_CLEAR_SETTLE, 6 POST_CLEAR_VERIFY, 7 CALIBRATION_HANDOFF, 8 WAIT_FIRST_VALID_SAMPLE, 9 COMPLETE, 10 FAILED。
- `hb` 顺序：Protect.Sample.State.SOC.Balance.CANTx.CANRx，hex generation。

若 firmware 在 AFE startup 前失败，scheduler/debug task未启动，所以可能没有 `BMS1` 行。此时用 SWD breakpoint 检查 `BMS_SafeIdle()` 的前一条件；不要据“UART没输出”直接判断 UART坏。

## 3. 症状定位表

| Symptom | 优先观察 | 检查顺序 | 常见定位 / 处理 |
|---|---|---|---|
| AFE 无 ACK | PB8/PB9 waveform、SoftI2C status、BQ/REGOUT/WAKE | idle high -> PA8 wake -> address/CRC mode -> pull-up/rise -> pin mapping -> 9-clock recovery | `SoftI2C_Init/Start/WriteAddress`, `BSP_AFE_WakePulse`; 电气不合格先修硬件，不增加无限 retry |
| cell voltage 不合理 | `cell=min/max`, valid/in-range bitmap, calibration gain/offset, logical index | DMM stimulus -> VC1..8/10..13/15 mapping -> 30-byte window -> raw14 -> gain/offset -> stale age | `BQ76940_Measurement_VcChannelOfLogicalCell`, `ReadCellVoltages13`; 不用 BAT值替代 cell sum |
| current 不变化 | `cur`, vf bit1/5, CC mailbox/queue sequence, Protect diagnostics | ALERT/CC_READY -> CC read -> queue push -> W1C -> generation/polarity/Rsense | `BMS_Protect_HandleCcReady`, `GetLatestCc`, `ConvertCcRawToCurrentMa`; 查 `cc_queue_overflow_latched` |
| NTC 异常 | `temp`, vf bit2/6, TS1 raw/resistance、table ID | TS1 voltage -> raw14 -> resistance公式 -> NTC table monotonic/domain -> sample every 8 cycles | `BQ76940_ConvertTs1RawToResistanceOhm`, `BMS_Ntc_Interpolate`; SIM table不可当真实曲线 |
| ALERT 一直拉高 | PB1 level、SYS_STAT、ambiguous mask、Protect retry | 读每个 set bit -> 对应 handler是否成功 -> clear mask -> W1C outcome -> observed-low retirement | `BMS_Protect_Drain/ServicePending`; 不依赖新 rising edge，不 blind replay ambiguous W1C |
| XREADY 循环 | `seq/gen`, `fault`, `rec`, Protect XREADY state/ack | generation是否反复推进 -> PRE_CLEAR是否获得 FET/balance safe readback -> W1C ack -> post-clear SYS_STAT | `BMS_Recovery_Service`, `BMS_Protect_RecoverXready`; 新 event必须重启，不复用旧证据 |
| calibration 不完成 | `rec` phase、post_clear_verified、calibration fields、Sample diagnostics | config/register plan -> 800 ms -> SYS_STAT -> gain/offset read -> provenance tuple -> handoff admission | phase 4–8；看 `calibration_invalid_count` 与 xready pre/postcheck rejects |
| measurement stale | `vf` fresh bits、age/stale bitmap、`sample_sequence` 是否前进 | Sample task heartbeat -> I2C mutex/transport -> publish failures -> current/temperature各自周期 | `BMS_Data_GetSnapshot`, `BMS_Sample_GetDiagnostics`; valid仍可为1，不代表fresh |
| State 卡 INIT | `st=0`, `rec=phase/ready`, `seq/gen`, state technical_ready | Recovery ready -> first valid current-generation sample -> policy startup timeout -> State publish identity | `BMS_State_UpdateClassification`; 5 s 后可能转 FAULT，不强制改 state |
| State FAULT | `fault=active/latched`, `inh`, Protect/State separate snapshots | 先解 active bit owner -> 查 latched是否action-bearing -> 查 DATA_STALE/RTOS_HEALTH -> 查 recovery | 不从 `st=4` 直接推断 FET；按 fault ID owner定位 |
| CHG 不开 | `fet`, `inh` CHG、`rec ready`、transaction、SYS_CTRL2/CC_EN | requested -> CHG inhibit bitmap -> recovery/health -> quarantine -> I2C/readback -> input revisions | `BMS_FetManager_ComposeEffective/Service`; register confirmed不等于MOS导通 |
| DSG 不开 | 同上，重点 DSG inhibit | requested -> DSG reasons（UV/discharge OC/temp/stale/HW）-> transaction | 保留 directional诊断；不要用 generic fault OR猜原因 |
| FET command/readback mismatch | `fet` requested/effective/observed/confirmed/txn、last transport | current SYS_CTRL2 -> expected compositor -> write outcome -> full-byte readback -> revision变化 | enable mismatch/ambiguous进入 quarantine；只允许 safe-off attempt，不重复 enable |
| health fault | fault bit19、`hb` 7 counters、State cadence | 找 generation 不前进的 task -> 对照 max liveness -> 查 bounded wait/priority/I2C/Flash阻塞 | `BMS_Health_Evaluate`; 先修 task根因，不让其他 task代写 heartbeat |
| watchdog reset | reset cause、last `hb`/fault、arm时刻 | 是否所有 first advance后arm -> 哪task stale -> State是否唯一 feed -> actual LSI timeout | `Task_State`, `BSP_IWDG_StartNominal`; real timeout必须测量 |
| balance 不动作 | `bal`, `st`, `inh`, `rec`, cell delta/temp/current/freshness | state CHARGE/STANDBY -> no inhibit -> ready -> min cell>=4100 -> delta 20/10 -> temp 0..45 -> abs current<=3000 -> I2C/readback | `BMS_Balance_Evaluate/RunOnce`; requested 0通常是eligibility，不先怀疑寄存器 |
| CAN 无 TX | `can` init_fail/target_tx/drop、PA11/PA12、queue | clock/PCLK1 -> transceiver/STB -> 500k timing -> CANTx heartbeat -> queue/no mailbox -> bus-off | `BSP_CAN_Init500K`, `BMS_Can_TxHardwareService`; CAN失败不应关本地FET |
| CAN 无 RX | analyzer waveform、FIFO pending/overrun、queue drop、rx_valid/invalid | exact standard `0x280` -> filter -> ISR priority/FIFO drain -> queue -> magic/source/request/time | `USB_LP_CAN1_RX0_IRQHandler`, `BMS_Can_DecodeServiceReset`; invalid frame不生成request |
| Flash restore failure | `flash` both_invalid/load_io、A/B raw bytes、sequence/CRC/commit | address/part -> read both -> magic/version -> commit marker -> CRC32 -> wrap-safe newest | `BMS_Persistence_SelectNewest/StoreInit`; both invalid走fallback，不擦页“修复” |
| Flash save failure | save io/verify/not_due、active slot、time/SOC delta | valid SOC -> 60s min interval/10 permille -> inactive erase -> body readback -> commit -> full verify | previous bank必须保留；查 `bsp_flash` page restriction与power quality |
| USART 无输出 | PA9 waveform、clock、BRR、CANTx heartbeat | scheduler是否启动 -> `BSP_UART1_Init115200` -> PA9/PA10 mapping -> terminal 115200 8N1 -> CANTx/debug service | startup safe-idle时无周期行；UART没有命令回显功能 |

## 4. Fault bitmap 快速索引

`fault` 以 8 hex digits显示，每一 bit 与 `BMS_FaultId_t` 相同：

| Bit | Fault | Owner | Direction |
|---:|---|---|---|
| 0/1/2/3 | HW_OV/HW_UV/HW_OCD/HW_SCD | Protect | CHG / DSG / DSG / BOTH |
| 4/5 | AFE_XREADY/AFE_OVRD_ALERT | Protect | BOTH |
| 6/7 | AFE_COMM/AFE_CRC | Protect | BOTH while active/action-bearing |
| 8 | AFE_STALE | reserved/unasserted | — |
| 9/10 | SW_OV/SW_UV | State | CHG / DSG |
| 11/12 | SW_OC_CHARGE/DISCHARGE | State | CHG / DSG |
| 13/14 | TEMPERATURE_HIGH/LOW | State | 根据 charge/discharge condition 定向 |
| 15 | DATA_STALE | State | BOTH |
| 16/17 | CAN/FLASH_CONFIG | reviewed diagnostic/fallback | no direct FET in current SIM policy |
| 18 | CLOCK | startup/technical | BOTH if active path exists |
| 19 | RTOS_HEALTH | State | BOTH + no IWDG feed |

高于 bit19 的 inhibit bits 是 technical reasons：bit20 recovery、21 policy invalid、22 FET unverified、23 decision stale、31 unknown source。`inh` 比 `fault` 更接近“FET为什么没开”的答案。

## 5. Debugger watch 建议

优先调用/观察公开 snapshot，不直接改 module static：

- `BMS_Data_GetSnapshot()`：values、age、valid/stale、sequence/generation；
- `BMS_State_GetSafetySnapshot()` 与 `BMS_Protect_GetSafetySnapshot()`：分 owner 查 source/action；
- `BMS_FetManager_GetSnapshot()`：requested/effective/observed/revisions/transport；
- `BMS_Recovery_GetSnapshot()`：phase/evidence/ready；
- `BMS_Health_GetSnapshot()`：per-task progress；
- `BMS_Soc_GetSnapshot()`, `BMS_Balance_GetSnapshot()`, `BMS_Can_GetDiagnostics()`, `BMS_Persistence_TargetGetDiagnostics()`。

不要在 live debug 中写 `s_fault`、`s_snapshot`、`g_bms_fet_request` 或 BQ registers来“确认能工作”。这会绕开 owner/revision并使后续证据不可解释。

## 6. 收集一个可复现问题包

至少包含：firmware commit/branch、board/schematic revision、SIM或REAL policy identity、复现步骤、power/reset history、完整 UART/CAN log、相关 scope/logic trace、snapshot fields、第一次异常时间、是否可重复、修改过的 debugger memory/register。没有这些信息时先补证据，不先大改架构。
