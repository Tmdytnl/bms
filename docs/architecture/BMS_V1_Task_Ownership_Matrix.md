# BMS V1 Task / Shared-State Ownership Matrix

本文回答“谁能写、在哪写、失败时怎样”。`Owner/Writer` 是约束，不是建议。

## 1. Task ownership

| Task | Trigger/Period | Owns | Reads | Writes | Hardware access | Heartbeat | Safety role |
|---|---|---|---|---|---|---|---|
| `Task_Protect` | binary semaphore；PB1 high/retry；100 ms bounded health wait | HW/AFE source lifecycle、Protect snapshot、source generations、XREADY state/W1C、CC mailbox/producer | SYS_STAT、CC、HW recovery/service reset requests | `xCcSampleQueue`、latest CC、Protect snapshot/ack；urgent State notification | bounded `xI2CMutex`; BQ SYS_STAT/CC；runtime XREADY W1C sole owner | `BMS_HEALTH_TASK_PROTECT` | HW/AFE fault authority；unknown source fail-both |
| `Task_Sample` | `vTaskDelayUntil`, 250 ms | measurement acquisition/publication、sample diagnostics、calibration binding | BQ cell/BAT/TS1、Protect latest CC/XREADY、immutable NTC table | measurement-owned `BMS_Data` fields、`sample_sequence`、`afe_generation` | bounded `xI2CMutex`; BQ measurements | `SAMPLE` | 只发布完整 current-generation core；拒绝 stale provenance |
| `Task_State` | `ulTaskNotifyTake`, timeout 100 ms | State engine、State safety snapshot、health monitor、HW recovery engine；Recovery/FET execution context | Data/Protect/Recovery/health snapshots | State snapshot、HW recovery request、BMS_Data diagnostic aggregate；IWDG start/feed | Recovery/FET services 内 bounded I2C；IWDG | `STATE` | SW protection、DATA_STALE、RTOS_HEALTH；sole IWDG feeder；sole FET Manager invoker |
| `Task_SOC` | periodic 1000 ms | SOC engine/snapshot；CC queue consumer；runtime persistence-save call ownership | `xCcSampleQueue`、measurement、policy、startup restore state | SOC diagnostic、A/B persistence request | Flash service only；不持 data/I2C mutex | `SOC` | 无直接 FET/fault authority；queue gap/generation change invalidation |
| `Task_Balance` | periodic 1000 ms | balance engine/snapshot、scheduler-era CELLBAL writes | Data、State、Protect、Recovery | CELLBAL1..3 request/readback snapshot | bounded `xI2CMutex`；sole scheduler-era CELLBAL writer | `BALANCE` | safety input/revision 改变即 all-off path |
| `Task_CANTx` | 10 ms loop；100 ms CAN build；1000 ms UART snapshot / 8 B drain | CAN TX hardware path、UART telemetry caller | Data/State/Protect/Recovery/FET/SOC/Balance/CAN/Flash/health diagnostics | `xCanTxQueue` drain、bxCAN mailboxes、USART1 TX | bxCAN TX；USART1 TX | `CAN_TX` | diagnostic/control-plane support；没有 FET/CELLBAL/fault/IWDG authority |
| `Task_CANRx` | RX queue；100 ms bounded wait | protocol RX validation | frames、Data identity、State revision、policy | source-specific Protect service request | none；ISR 已完成 FIFO copy | `CAN_RX` | 不能直接 clear bitmap 或启 FET |

## 2. ISR ownership

| ISR | Allowed | Forbidden | Handoff |
|---|---|---|---|
| `EXTI1_IRQHandler` | check/clear STM32 EXTI1 pending；`xSemaphoreGiveFromISR`；yield | I2C、BQ read/write、fault lifecycle、FET、UART/printf | `xAfeAlertSem` -> ProtectTask |
| `USB_LP_CAN1_RX0_IRQHandler` | clear FIFO overrun；drain FIFO；copy frame；timestamp；`xQueueSendFromISR`；counter | service command parsing、Protect request、FET、Flash、UART | `xCanRxQueue` -> CANRxTask |
| FreeRTOS SVC/PendSV/SysTick | kernel port | application safety logic | scheduler |

## 3. Shared state ownership

| Shared State | Authoritative Writer | Readers | Protection | Generation/Revision | Failure semantics |
|---|---|---|---|---|---|
| measurement snapshot | `Task_Sample` via `BMS_Data_PublishMeasurement` | State、SOC、Balance、CAN、Debug | `xDataMutex`；stage then atomic commit | `sample_sequence` + `afe_generation` | mandatory core failure leaves previous snapshot unchanged |
| `sample_sequence` | `BMS_Data_PublishMeasurement` only | State/Recovery/Balance/CAN | same data mutex | natural `uint32_t` advance | equality is current identity；full 2^32 alias不声称可检测 |
| `afe_generation` in data | Sample copies current Protect XREADY generation | State/Recovery/Balance/SOC | frame validation + mutex | compared with XREADY/recovery evidence | mismatch rejects publish/consumer evidence |
| latest CC mailbox | Protect after accepted CC queue push | Sample only | scheduler suspension | mailbox `sequence` + `xready_generation` | XREADY active or generation mismatch returns invalid |
| CC sample queue | Protect sole producer；SOC sole consumer | SOC | FreeRTOS queue | tick + `xready_generation` | full queue drops one oldest, keeps newest, latches gap diagnostics；CC W1C only after enqueue success |
| State safety snapshot | StateTask via compare-and-publish | FET、Balance、CAN、Debug | scheduler suspension | evaluated sample identity + `publication_revision` | newer measurement rejects stale decision |
| Protect safety snapshot | ProtectTask | FET、State/HW recovery、Balance、CAN、Debug | scheduler suspension | `publication_revision` + per-source generation | transport ambiguity retains/unverifies source；unmapped active source inhibits both |
| diagnostic fault aggregate in `BMS_Data` | StateTask ORs State+Protect | CAN/Debug/user display | `xDataMutex` narrow API | data snapshot identity only | reporting may lag一周期；never FET authority |
| source fault lifecycle | Protect for HW/AFE；State for SW/stale/health | FET/diagnostics | each owner private state | source generation/revision | no cross-owner bitmap clear；no generic clear-all |
| `inhibit_chg_reasons` | each authoritative safety owner for its snapshot | FET、Balance、Debug | owner snapshot copy | owner publication revision | nonzero forces CHG effective OFF |
| `inhibit_dsg_reasons` | each authoritative safety owner for its snapshot | FET、Balance、Debug | owner snapshot copy | owner publication revision | nonzero forces DSG effective OFF |
| FET operational request | State safety snapshot (`operational_intent`) | FET Manager | State snapshot | State revision + evaluated identity | not permission by itself |
| legacy `g_bms_fet_request` | Protect lower-phase compatibility logic | lower-phase tests；not current FET authority | Protect context | none | retained compatibility seam；production manager consumes snapshots, not this global |
| FET confirmed state | FET Manager | Recovery、CAN、Debug | scheduler suspension | manager publication + input revisions | `UNVERIFIED/QUARANTINED` deny enable claim；readback is register-level only |
| recovery state | Recovery Coordinator serviced by StateTask | State、FET、Balance、CAN、Debug | scheduler suspension | `xready_generation`, `recovery_revision`, `publication_revision` | in-progress/failed keeps BOTH inhibit |
| `xready_generation` | Protect on inactive->active observation | Sample、Recovery、FET/Debug | Protect snapshot | natural `uint32_t` | new generation invalidates calibration/recovery/sample evidence |
| runtime XREADY clear authorization/ack | coordinator writes authorization request；Protect writes ack/W1C | both parties | scheduler suspension + Protect I2C transaction | generation + recovery revision + Protect revision | ambiguous finalization fails recovery；no blind W1C replay |
| calibration provenance | Recovery constructs；Sample admits/owns installed calibration | Sample acquisition | scheduler guarded multi-field update | generation + recovery revision + post-clear verified | arbitrary runtime `SetCalibration` denied after XREADY invalidation |
| HW recovery request/ack | State qualification writes request；Protect source owner writes ack | State/Protect | scheduler suspension + fresh SYS_STAT read | request ID + source generation + sample identity + qualification/protect revision + expiry | any new event, stale evidence, timeout or I2C failure invalidates exchange |
| heartbeat generation array | each listed task writes only its index | State health monitor、Debug | scheduler suspension for coherent snapshot | per-task monotonic counter | no clear API；stale task -> RTOS_HEALTH, BOTH inhibit, no IWDG feed |
| health monitor / watchdog armed | StateTask local monitor | State decision | StateTask local | baseline/last generation/last advance | first arm waits for all tasks to advance；once unhealthy feed denied |
| SOC snapshot | Task_SOC | BMS_Data diagnostic、Persistence、CAN/Debug | scheduler suspension | integrated count、gap/generation counters | invalid on bad initial evidence/queue gap as defined；bounded 0..1000 |
| balance command/readback | Task_Balance | Recovery、CAN/Debug | scheduler suspension + I2C mutex | sample identity + State/Protect/Recovery revisions + confirmed generation | stale/revision mismatch or transport failure -> all-off attempt and unconfirmed |
| CAN TX queue | CAN core enqueues；CANTx hardware service dequeues | CANTx | FreeRTOS queue | counters only | congestion increments drops；CAN has no FET effect |
| CAN RX queue | FIFO0 ISR producer；CANRxTask consumer | CANRx | FreeRTOS FromISR queue | received tick | overflow/drop diagnostic；no safety-authority mutation in ISR |
| CAN diagnostics | CANTx/CANRx/ISR by field | Debug | task snapshot via scheduler suspension；ISR increments bounded counters | counters | diagnostic counter race does not change safety state |
| persistence metadata/store | startup initializes/loads；Task_SOC services save | SOC/Debug | sole runtime caller；no shared mutex held during Flash | A/B sequence、active slot、diagnostic counters | newest bank never erased first；commit/readback failure preserves previous valid bank |
| debug line/static projections | Task_CANTx via `BMS_Debug_Service` | USART1 consumer | sole caller；read-only snapshot APIs | 1 s timestamped snapshot；8 B/service bounded drain | partial/no output affects observability only；no command path |

## 4. Hardware register writer matrix

| Register/resource | Pre-scheduler writer | Scheduler-era writer | Notes |
|---|---|---|---|
| `SYS_CTRL2.CHG/DSG` | `BMS_AfeStartup` safe-off/config | FET Manager only | StateTask execution context；Protect/Recovery do not write |
| `CELLBAL1..3` | `BMS_AfeStartup` all-zero | BalanceTask only | Recovery waits for confirmed all-off，不接管 writer |
| `SYS_STAT.XREADY` W1C | `BMS_AfeStartup` final-observed startup exception | ProtectTask only | coordinator authorizes generation/revision |
| other `SYS_STAT` W1C | none or source-specific startup restrictions | ProtectTask | independently handled bits；CC only after accepted sample path |
| BQ config/protection registers | `BMS_AfeStartup` | Recovery Coordinator single-step service | Recovery serviced by StateTask；不写 CHG/DSG |
| IWDG start/feed | none | StateTask only | no task/Flash/CAN feeder |
| bxCAN transmit mailbox | none | CANTxTask only | RX ISR only receives |
| Flash A/B pages | startup read only | Task_SOC persistence service | `bsp_flash` range-restricts to A/B pages |
| USART1 TX | startup init | CANTxTask debug service | read-only telemetry；无 UART command consumer |

## 5. Locking / wait rules

1. 不同时持有 `xDataMutex` 与 `xI2CMutex`。先复制/准备，再执行 bounded hardware transaction，再以 narrow API 发布。
2. Protect、Sample、FET、Balance 使用 20 ms 量级 I2C timeout；Recovery 每 service step 至多一个 transaction，不在 settle 时持锁。
3. required heartbeat task 不可无限等待：Protect/CANRx 有 100 ms bounded wait；State notification 有 policy timeout。
4. scheduler suspension 只用于短小的 aligned counter 或 fixed snapshot copy，不包围 I2C/Flash/UART。
5. Flash service 不持 I2C/data mutex，不从 Flash path feed watchdog。

## 6. 修改审查问题

若变更涉及 shared state，review 必须明确：

- 是否新增 writer？若是，为什么不违反本矩阵？
- identity 是 timestamp、sequence、generation 还是 revision，是否足以拒绝 stale evidence？
- transaction 中途 source 改变时，enable/readback/clear 是否会被拒绝？
- transport failure、finalization ambiguity 与 readback mismatch 分别是什么 failure semantics？
- Simulator evidence 与 REAL_HW evidence 是否在文档中分开？
