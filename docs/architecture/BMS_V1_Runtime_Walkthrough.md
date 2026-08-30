# BMS V1 Runtime Walkthrough

本文沿一次实际软件执行链说明模块如何协作。函数名均来自当前源码；硬件接口观察维度由集成矩阵统一索引。

## 1. Power-on 到 normal control

### 1.1 Reset 与最早期 fail-safe

`Reset_Handler` 完成 data/BSS 初始化并调用 `SystemInit()`，随后进入 `main()`。`BMS_Data_Init()` 先把 measurement、state、fault、SOC metadata 放到显式 invalid/unknown 初值；`BMS_Sample_Init()` 也保持 device/calibration 未绑定。

`BMS_Policy_Get()` 返回 immutable `SIM_POLICY_V1`。`BMS_Policy_Validate()` 检查 profile、13S、20 Ah、4 mΩ、NTC 单调表、AFE protect codes、threshold ordering、health roster、CAN/Flash layout。验证失败不会尝试“用默认值继续”，而是 `BMS_SafeIdle()`。

### 1.2 MCU/BSP 与 AFE startup

`BSP_Clock_Verify()` 核对 72 MHz clock tree；随后初始化 GPIO、TIM3 与 USART1。software I2C 以 PB8/PB9 open-drain、5 µs half-cycle 初始化；如果 SDA 初始 stuck-low，仅允许一次明确的 9-clock recovery path。

`BQ76940_Init()` 建立 transport handle 后，`BMS_AfeStartup_Step()` 在 pre-scheduler 单线程中推进：

1. PA8 wake edge 与 10 ms settle；
2. probe BQ；
3. 写/读回 SYS_CTRL2 safe-off 与 CELLBAL1..3 all-zero；
4. 配置 SYS_CTRL1/CC_CFG；读取 actual ADC gain/offset；
5. 根据 calibration 编码 OV/UV，配置 PROTECT1/2/3；
6. 等待 800 ms initial data；读 final SYS_STAT；
7. 若 final read 看到 XREADY，执行一次 startup-owned W1C，并从头重新配置/settle；
8. final status 无 blocking bit、FET/balance safe outputs readback confirmed 后 COMPLETE。

任何 ambiguous write finalization、readback mismatch、unsafe status 或 calibration error 都使 startup FAILED，scheduler 不启动。

### 1.3 RTOS 对象与七任务

startup calibration 与 simulation NTC table 安装到 Sample 后，依次初始化 Protect、Health、State、Recovery、FET、SOC、Persistence、Balance、CAN、Debug。Persistence startup 只读取两个 slots，若有 newest-valid payload 就恢复 SOC；没有 valid record 使用 SOC engine 的 OCV/fallback 路径。

`APL_Rtos_CreateObjects()` 原子式创建 2 mutex、1 binary semaphore、3 queues 和 event group；任何创建失败都会删除已创建对象。raw handles 只在 `apl_rtos_internal.h` 的 APL-private registry 暴露。`APL_Rtos_CreateTasks()` 按 Protect、Sample、State、SOC、Balance、CANTx、CANRx 顺序创建，并把 AFE device 作为 ProtectTask argument 注入。ProtectTask 启动后才打开 EXTI，避免 FreeRTOS ISR-priority validator 尚未初始化时出现真实 edge。

### 1.4 第一次 measurement 与 readiness

SampleTask 每 250 ms 开始一次 staging：读 13 cell window、BAT，取 Protect latest CC mailbox；每第 8 cycle 读 TS1 并通过 NTC table 转换。它在 I2C 前后检查 XREADY/calibration/config revision，只在 mandatory core 完整、generation 未变时调用 `BMS_Data_PublishMeasurement()`。

publication 使 `sample_sequence` 前进并携带 `afe_generation=0`（或当前 generation）。Recovery 初始 `COMPLETE/technical_ready=true` 的 startup handoff 与 fresh measurement 让 State engine 从 INIT 进入 STANDBY qualification。State 以 100 ms 作为最大有界等待，并可由安全事件 urgent notification 提前唤醒；它发布与该 sample identity 绑定的 snapshot，FET Manager 再独立复核 Protect/State/Recovery revisions、执行 SYS_CTRL2 transaction/readback。此时才能报告 register-level effective FET state。

### 1.5 一个典型 runtime cycle

```text
ProtectTask:
  ALERT/100 ms wake -> SYS_STAT drain -> CC queue/mailbox + Protect snapshot

SampleTask (250 ms):
  BQ reads -> staged frame -> generation guard -> atomic measurement publish

StateTask (100 ms / urgent):
  heartbeat health -> Recovery one step -> State compare-and-publish
  -> HW recovery qualification -> FET Manager transaction -> diagnostic aggregate
  -> feed/no-feed IWDG

SOCTask (1000 ms):
  drain CC queue -> integer integrate/correct -> SOC publish -> save-if-due

BalanceTask (1000 ms):
  read current identities/safety -> select -> revalidate -> CELLBAL write/readback

CANTxTask (10 ms service):
  every 100 ms build 0x180..0x185 -> hardware TX; every 1 s build UART status,
  drain at most 8 bytes per 10 ms service

CANRxTask (bounded 100 ms):
  validate exact 0x280 service frame -> source-specific Protect request
```

上述 task 不形成“按顺序执行的超级循环”。它们通过 immutable policy、owner snapshots、queues 与 identity/revision 连接。图中的顺序是数据依赖，不是 scheduler 的固定时间表。

## 2. Normal path 的几个细节

### Measurement 与 current identity

CC_READY 由 Protect 处理。只有 `BQ76940_ReadCcRaw()` 成功且 newest sample 已进入 `xCcSampleQueue` 时，Protect 才把 CC_READY 加入 W1C mask；同时发布 latest mailbox 给 Sample。SOC 消费 queue，Sample 不抢 queue；这避免两个 consumer 对“哪一个 current sample”产生不同理解。

### State 与 FET

State 依据 current 把运行分类为 STANDBY/CHARGE/DISCHARGE，且生成 intent；SW fault 产生 directional inhibit。FET Manager 合并三个 authoritative snapshots。即使 state 为 STANDBY，如果 DATA_STALE 或 XREADY recovery active，effective CHG/DSG 仍为 OFF。

### SOC、Balance、CAN、Flash

- SOC 只通过 CC timestamp/generation 积分，queue gap 会被记录，不假装 sample 连续。
- Balance 只在 CHARGE/STANDBY、无任何方向 inhibit、Recovery ready、数据 fresh/in-range 时选 cell。
- CAN frames 显式编码 C values；`0x280` 只转换为 service request，不能直接改安全状态。
- Flash 每 60 s minimum interval 且 SOC 变化达到 10 permille 才可能保存；保存时 inactive bank 先完成 body+CRC readback，commit marker 最后写。

## 3. Scenario A — cell over-voltage

### 软件 OV 路径

1. Sample 读到某 cell `>=4200 mV`，frame valid/in-range/current-generation，发布新 sequence。
2. StateTask 读取该 snapshot。`BMS_State_UpdateCondition()` 对 SW_OV 开始 500 ms debounce；期间新 measurement 的 max cell 持续满足 trigger。
3. debounce 到期，State snapshot 设置 `BMS_FAULT_ID_SW_OV` active，并只在 `inhibit_chg_reasons` 加该 fault bit。
4. compare-and-publish 确保 decision 仍对应当前 sample；若 Sample 在提交前发布新帧，旧 decision 被拒绝。
5. FET Manager 看到 CHG inhibit，effective CHG=OFF；DSG 只受其他 sources 影响。它 read/compose/write/readback SYS_CTRL2 并发布 transaction state。
6. cell `<=4100 mV` 且持续 recovery qualify 2000 ms 后，State owner 清自己的 SW_OV active/action。

### 硬件 OV event 路径

1. BQ 使 ALERT active，EXTI1 ISR 给 semaphore。
2. ProtectTask 读 SYS_STAT 看到 OV，推进 HW_OV source generation、置 active，并把 W1C 作为 event acknowledgement；它不因随后 bit low 自动清 active。
3. Protect snapshot 对 CHG directional inhibit；State/FET 立即消费该 authority。
4. 当 fresh measurements 持续满足 HW recovery policy，State 的 HW recovery engine 提交携带 source generation/sample identity/expiry 的 request。
5. Protect 复核 request，重新读取 SYS_STAT、确认没有 target/blocking status，再只清 HW_OV private active source并 ack。

production-C 测试覆盖上述 request/ack/revision 行为；OV threshold、delay、ALERT、W1C 与 MOS action 的接口观察维度由集成矩阵统一索引。

## 4. Scenario B — measurement becomes stale

1. 假设 SampleTask 不再成功发布，但历史 measurement 数值仍在合法范围。
2. `BMS_Data_GetSnapshot(now_ms)` 从 group timestamp 派生 age；跨过 1000 ms cell/current 或 5000 ms temperature freshness limit 后置 stale latch。validity 不被抹掉。
3. State 的 `BMS_State_AllSafetyMeasurementsFresh()` 返回 false；`DATA_STALE` active，CHG 与 DSG inhibit 同时设置。
4. FET Manager 不关心旧数值“看起来正常”，只消费 State directional inhibit，申请 both-off 并验证 SYS_CTRL2 readback。
5. Sample 恢复后，每个新 frame 带新 sequence。State 要求连续 `recovery_fresh_frames=2` 个 fresh accepted frames 才清 DATA_STALE。

这里 validity 与 freshness 的分离很重要：invalid 表示未获得合法数据；stale 表示数据曾合法但时间上已不可信。两者都不能被“数值仍在阈值内”掩盖。

## 5. Scenario C — runtime XREADY

1. ProtectTask 读 SYS_STAT 首次看到 XREADY：推进 `xready_generation`、置 XREADY active/latched、使 latest CC mailbox invalid；Protect snapshot BOTH inhibit。
2. Recovery Coordinator 在下一次 StateTask service 发现 generation 改变，调用 `BMS_Recovery_ResetEvidence()`，phase=`PRE_CLEAR_PREPARE`，校准/first-sample evidence 清空。
3. FET Manager 与 BalanceTask仍是各自 register writer。Recovery 等待 current-generation FET readback off、CC_EN present、CELLBAL all-off confirmed。
4. phase 到 `PRE_CLEAR_READY` 后，Coordinator 写 authorization `{generation,recovery_revision}` 并唤醒 Protect。
5. Protect 是 runtime XREADY W1C sole owner；成功 finalization 发布 ack 并清 active。若 STOP finalization ambiguous，进入 quarantine/failure，不 blind replay。
6. Recovery 重新写读完整配置，等待 800 ms，再读 SYS_STAT 确认 XREADY/blocking bits absent。
7. Recovery 读取 calibration，构造 `{xready_generation,recovery_revision,post_clear_verified,calibration}`；Sample 只在 current generation、XREADY inactive、revision/phase 允许时接收。
8. Sample 发布第一帧 current-generation measurement；Recovery 看到 `sample_sequence` 不等于 handoff baseline 且 generation 匹配，记录 first-valid evidence。
9. Protect 按 SIM policy source-specific release XREADY action latch；Recovery phase=`COMPLETE`、`technical_ready=true`、recovery inhibits 清零。

任何一步出现新 XREADY generation，旧流程立即失效并从 PRE_CLEAR 重新开始；任何 terminal failure 保持 BOTH inhibit。

## 6. 可观测性对应关系

M3 UART `BMS1` 行可用于把上述行为和 runtime 状态对齐：

| Field | 含义 |
|---|---|
| `st` | `BMS_State_t` numeric：0 INIT, 1 STANDBY, 2 CHARGE, 3 DISCHARGE, 4 FAULT |
| `cell=min/max`, `pack`, `cur`, `temp` | mV/mV、mV、mA、0.1°C |
| `vf` | bit0/1/2 cell/current/temp valid；bit4/5/6 respective fresh |
| `seq/gen` | accepted measurement identity |
| `fault=active/latched` | diagnostic OR of State+Protect bitmaps |
| `inh=chg/dsg` | FET Manager consumed reason bitmaps |
| `fet=req/eff/obs/confirmed/txn` | bit0 CHG、bit1 DSG；transaction enum 0 safe,1 applied,2 unverified,3 quarantined |
| `rec=phase/ready` | recovery enum numeric 与 technical readiness |
| `hb` | Protect.Sample.State.SOC.Balance.CANTx.CANRx generations（hex） |
| `soc`, `bal`, `can`, `flash` | respective owner snapshots/diagnostics |

UART 没有 command parser，观察行为不能改变系统状态。
