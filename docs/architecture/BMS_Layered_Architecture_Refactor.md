# BMS BSP / DRV / FML / APL 四层架构

## 1. 架构目标

本次重构只改变代码组织和依赖方向，不改变 BMS V1 的产品语义、安全契约、七任务拓扑、任务优先级、正常周期、CAN 协议、Flash A/B 记录格式或 UART 非阻塞约束。

依赖方向固定为：

```text
User main -> APL -> FML -> DRV -> BSP / MCU
                 \----------> BSP（仅适配与编排）
```

- BSP：STM32F103C8T6 板级时钟、GPIO、Timer、EXTI、CAN、Flash、UART、IWDG 原语。
- DRV：SoftI2C transport 与 BQ76940 寄存器、测量、控制编码；无 RTOS、无 FML/APL 业务头。
- FML：采样、保护、状态、恢复、FET、SOC、均衡、通信协议、诊断格式、持久化算法；无 RTOS、无 BSP、无直接 STM32 外设访问。
- APL：启动编排、七任务调度、RTOS IPC/时间/同步、IRQ handoff、CAN hardware scheduling、UART drain、Flash port 绑定。

## 2. 依赖规则

1. FML 公共接口只使用领域类型和标准整数/布尔类型，不暴露 RTOS handle、tick 或 queue。
2. FML 的总线锁、数据锁和短临界区通过 `bms_runtime_port.h` 的六个窄接口绑定；生产实现在 APL，测试使用确定性替身。
3. FML 不包含 `bsp_*`、STM32 SPL/CMSIS 外设头或 RTOS 头。
4. DRV 不包含 APL/FML 业务头，也不调用 RTOS。
5. BSP 不包含 APL/FML 业务头；板级常量集中在 `bsp_board_config.h`。
6. APL 可以组合 FML、DRV 与 BSP，但不得接管 FML 的安全决策 authority。
7. `firmware/Tests/verify_architecture.py` 对这些规则以及任务/IRQ/sole-writer 位置执行确定性硬检查。

## 3. 主要文件映射

| 原位置 | 新位置 / 处理 |
|---|---|
| `firmware/Driver/bsp_*` | `firmware/BSP/` |
| `firmware/Driver/soft_i2c.*` | `firmware/DRV/SoftI2C/` |
| `firmware/Driver/bq76940*`、`crc8_bq76940.*` | `firmware/DRV/BQ76940/` |
| `firmware/App/bms_data.*`、`bms_fault.*` | `firmware/FML/Data/`、`firmware/FML/Core/` |
| `firmware/App/bms_sample.*`、`bms_ntc.*` | `firmware/FML/Measurement/` |
| `firmware/App/bms_protect.*` | `firmware/FML/Protect/` |
| State/FET/Recovery/Health/SOC/Balance | 对应 `firmware/FML/<Function>/` |
| CAN/Debug/Persistence | `firmware/FML/Communication/`、`firmware/FML/Storage/` |
| `app_rtos.*`、hooks | `firmware/APL/apl_rtos.*`、`apl_rtos_hooks.c` |
| 原 FML task entry 与 ISR | `firmware/APL/Task/apl_task_*.c`、`firmware/APL/apl_irq.c` |
| 原 `main.c` 全量 wiring | `firmware/APL/apl_system.c`；`main.c` 仅保留 init/start/safe-idle |

旧 `firmware/App/` 与 `firmware/Driver/` 已无生产 `.c/.h` 副本。

## 4. Task 与 functional authority

| APL task | 调度职责 | FML authority |
|---|---|---|
| Protect，priority 5 | ALERT wait、10 ms retry、CC queue transport、100 ms health wait | Protect 独占 HW/AFE fault lifecycle 与运行期 SYS_STAT W1C |
| Sample，priority 4 | 固定 250 ms 周期 | Measurement 生成完整帧，Data 原子发布 |
| State，priority 3 | 最大 100 ms 等待、urgent notify、IWDG start/feed | Health/Recovery/State/HW qualification/FET Manager 各自保留决策与事务权限 |
| SOC，priority 3 | 固定 1 s，消费最多 8 条 CC sample，驱动低频持久化 | SOC 整数积分与 Storage A/B transaction 语义 |
| Balance，priority 2 | 固定 1 s | Balance 是 scheduler-era CELLBAL sole writer |
| CAN Tx，priority 2 | 10 ms hardware/UART service，100 ms protocol publish | FML CAN 只编码协议，FML Debug 只格式化快照 |
| CAN Rx，priority 2 | 100 ms bounded queue wait | FML CAN 解码受限 service request，经 Protect owner 接纳 |

## 5. 主要运行链路

### 5.1 AFE ALERT

```text
PB1
-> BSP EXTI pending/clear primitive
-> APL EXTI1 IRQ semaphore handoff
-> APL Protect task
-> FML Protect bounded drain / W1C / fault lifecycle
-> DRV BQ76940 register transport
-> DRV SoftI2C
-> BSP GPIO / Timer
```

CC_READY 使用两阶段交接：FML 暂存带 `transport_id` 的领域 sample；APL 以 newest-wins 规则写 `xCcSampleQueue`；APL 回报成功后，FML Protect 才在下一次 service 中尝试 CC_READY W1C。队列提交仍严格先于 W1C。

### 5.2 Sampling

```text
APL Sample 250 ms schedule
-> FML Measurement RunOnce(now_ms)
-> DRV BQ76940 Measurement
-> FML Data complete-frame publish
-> State / SOC / Balance / CAN read-only consumers
```

时间由 APL 以毫秒值传入；FML 不读取 RTOS tick。

### 5.3 State / FET

```text
APL State task
-> FML Health decision
-> FML Recovery request/ack coordinator
-> FML State classification
-> FML HW recovery identity qualification
-> FML FET Manager transaction
-> DRV BQ76940 control/register access
```

APL 只在 Recovery/Protect 返回“需要唤醒”时发 notification；它不代替安全授权。IWDG 仍仅在 State task execution context start/feed。

### 5.4 SOC persistence

```text
APL SOC task
-> xCcSampleQueue bounded drain
-> FML SOC plain BMS_CcSample_t consumption
-> FML Storage A/B commit-last algorithm
-> APL Flash storage port
-> BSP Flash page-restricted primitives
```

### 5.5 CAN / UART

```text
APL CAN Tx/Rx tasks + APL CAN IRQ
-> FML CAN protocol encode/decode
-> APL queue/mailbox scheduling
-> BSP CAN hardware

APL CAN Tx task
-> FML Debug snapshot formatting
-> APL max-8-byte non-blocking drain
-> BSP UART TryWriteByte
```

## 6. Safety writer ownership

- scheduler-era CHG/DSG：仅 `BMS_FetManager_Service()` 写 SYS_CTRL2；AFE startup 可在 scheduler 前建立安全初值。
- scheduler-era CELLBAL：仅 `BMS_Balance_RunOnce()` 直接写 CELLBAL；startup 保留 all-off staging/readback。
- runtime XREADY/SYS_STAT W1C：仅 FML Protect；Recovery 继续使用 generation/revision request/ack。
- measurement identity：仍为 `sample_sequence + afe_generation`。
- calibration provenance：仍绑定 XREADY generation、recovery revision 与 post-clear verification。
- HW recovery：仍绑定 source generation、measurement identity、qualification revision 与 expiry。
- `BMS_Data`：仍是诊断/共享投影，不是 FET authority。

## 7. 正确增加新模块

1. 先判断功能属于硬件原语、器件驱动、领域功能还是应用调度，并放入唯一对应层。
2. 新 FML API 使用 plain domain data 和显式 `now_ms`；不得包含 RTOS/BSP/STM32 类型。
3. 如果需要新硬件能力，在 BSP 提供最窄的无业务语义 primitive；器件协议放 DRV。
4. 如果需要新周期、queue、notification 或 IRQ handoff，在 APL 实现并调用 FML 的 RunOnce/Service API。
5. 不得新增绕过 Protect/FET Manager/Balance 的寄存器写路径。
6. 更新 Keil project、确定性测试与 architecture gate，运行 Phase 8 refactor mode、Phase 9 和 `verify_architecture.py`。
