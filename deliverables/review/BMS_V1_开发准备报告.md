# BMS V1 开发准备报告

- 审查日期：2026-08-13
- 审查范围：项目准备与技术审查；未进入 Phase 1，未生成 BMS 业务代码
- 项目基线：STM32F103C8T6 + STM32F10x SPL + FreeRTOS + 单 BQ7694003，13S/48 V，软件 I2C，CAN 500 kbit/s
- 证据状态：静态资料/源码审查完成；无构建、目标板或硬件实测结论

## 0. 执行结论

当前仓库是一个**设计资料、芯片资料和参考源码集合**，还不是可构建的 BMS 工程。最终设计规格、TI/ST 官方资料、SPL V3.5.0/CMSIS 参考源码和 FreeRTOS V11.1.0 内核源码均存在；但应用目录、`main.c`、工程/链接脚本、目标工具链定义、BQ/BSP/任务实现和测试全部尚未建立。

主架构与多数芯片基线成立：BQ7694003 型号、地址/CRC、13S 跳过 VC9/VC14 的映射，F103C8 的 64 KB Flash/20 KB SRAM，8 MHz HSE 到 72 MHz、TIM3 1 MHz、bxCAN 500 kbit/s 均得到官方资料支持。进入 Phase 1 前仍需先形成一份经确认的规格勘误：现有 FreeRTOS 配置与七任务优先级/NVIC 方案有两项 Critical 冲突；BQ 事件/FET 伪代码有数项 High 安全缺口；Flash、IWDG、ALERT 和硬件参数尚未闭环。

因此本轮结论是：**准备审查完成，但实现门禁未通过；等待用户确认开放问题和规格勘误后，才可进入 Phase 1。**

## 1. A — Repository Inventory

### 1.1 实际结构

```text
Bms_shop/
├─ AGENTS.md
├─ .agents/                       # 本地工作技能，不是固件
├─ .project-memory/               # 项目日志
├─ docs/                          # 输入规格与参考资料区
│  ├─ spec/
│  │  └─ BMS_V1_统一项目方案_软件设计规格.md
│  ├─ reference/
│  │  ├─ TI/                      # 6 份 TI PDF
│  │  └─ ST/
│  │     ├─ 6 份 ST PDF
│  │     └─ STM32F10x Standard Peripheral Library/
│  │        ├─ Start/             # CMSIS、system、8 个启动文件
│  │        ├─ Libarary/          # SPL V3.5.0，目录名原样拼作 Libarary
│  │        └─ SPL 工程模板中断/config 文件
│  └─ FreeRTOS/                   # kernel/include/port/heap/config 摘取副本
└─ deliverables/
   └─ review/
      └─ BMS_V1_开发准备报告.md    # 本轮独立整理的交付文档
```

扫描时 `docs/` 原有 108 个文件、48,490,667 bytes：1 个设计规格、12 个 PDF、35 个 `.c`、51 个 `.h`、8 个 `.s`、1 个 FreeRTOS 内部 `CMakeLists.txt`。`docs/spec` 1 个文件；TI 6 个 PDF；ST 6 个 PDF和 56 个 SPL/CMSIS/模板文件；FreeRTOS 33 个文件。

| 分类 | 实际状态 | 结论 |
|---|---|---|
| 最终设计文档 | 1 份，4,241 行 | 已完整通读；当前 SHA-256 `7e71125d…6472` |
| TI reference | 6 份 PDF | 完整清单存在；关键章节、表格、图已核查 |
| ST reference | 6 份 PDF | 完整清单存在；关键章节、表格、图及勘误已核查 |
| SPL | V3.5.0，要求的 GPIO/RCC/EXTI/TIM/CAN/USART/FLASH/IWDG/misc 均有 `.c/.h` | 可作为参考库基线，未证明已被任何工程编译 |
| CMSIS/启动 | `core_cm3.*`、`stm32f10x.h`、`system_stm32f10x.*`、目标 `startup_stm32f10x_md.s` 均存在 | CMSIS CM3 V1.30；目标启动文件存在但未被工程选择 |
| FreeRTOS | V11.1.0 kernel、RVDS/ARM_CM3 风格 port、`heap_4.c`、config 均存在 | 是人工摘取/扁平副本；配置未通过兼容性门禁 |
| 已有项目代码 | 只有参考库和模板 | 无 App/Driver/Protocol/Service/Config/User，无 BQ/BSP/task/CAN/param 实现 |
| 构建依赖 | 缺失 | 无顶层 CMake/Make/MDK/IAR 工程、scatter/linker script、source list、map/elf/hex/bin |
| 工具链 | 未选定 | 本机未发现 ARM 嵌入式编译器；所带 FreeRTOS port/startup 为 legacy ARMCC5 风格，不是 GCC port |
| 历史文件 | 无证据 | 根目录不是 Git 仓库；无 changelog/archive。非目标 startup 和模板不能冒充项目历史 |

`docs/FreeRTOS/include/CMakeLists.txt` 明确声明是 FreeRTOS 内部文件，不能用作用户顶层工程。仓库未发现 HAL、CubeMX 或 CMSIS-RTOS 的实际工程依赖。

## 2. B — 已读取资料与权威地图

### 2.1 已读取资料

| 级别 | 资料 | 版本/覆盖 | 本轮用途 |
|---:|---|---|---|
| L1 | `docs/spec/BMS_V1_统一项目方案_软件设计规格.md` | V1.0，4,241 行 | 项目边界、架构、策略、接口、Phase 计划 |
| L2 | TI `01_...SLUSBK2I_EN.pdf` | Rev I，67 页；实现正文逐页抽取，关键页视觉核验 | BQ 型号、寄存器、CRC、公式、时序、13S |
| L2 | ST `01_...DS5319_EN.pdf` | Rev 20，114 PDF 页 | F103C8 资源、引脚、时钟/Flash 电气事实 |
| L3 | TI Top Design Considerations | SLUA749A，25 页 | XREADY、平衡、系统注意事项 |
| L3 | TI Embedded Scheduler | SLUA775，10 页 | 三组调度、CC_READY、更新/覆盖行为 |
| L3 | TI BMS Configurations | SLUA810，11 页 | 减串连接约束 |
| L3 | ST RM0008 English | Rev 21，1,136 页 | RCC/GPIO/AFIO/EXTI/TIM/CAN/USART/IWDG |
| L3 | ST PM0075 | Rev 2，31 页 | Flash 组织、擦写、等待/错误处理 |
| L3 | ST PM0056 | Rev 7，156 页 | Cortex-M3 异常、NVIC、SysTick |
| L3 | ST ES096 | Rev 15，31 页 | F103x8/B 勘误 |
| L4 | BQ76930/40 EVM Guide | SLVU925C，66 页 | EVM/连接参考；注意其 U1 是 BQ7694000 |
| L4 | STM32F10x SPL source/templates | V3.5.0 | API、startup/system/模板参考 |
| L5 | TI 中文数据手册 | Rev F，66 页 | 仅辅助检索；版本旧于英文 Rev I |
| L5 | RM0008 中文 | Rev 10，754 页 | 仅辅助检索；首页说明英文为准且修订较旧 |

12 份 PDF 共 2,467 个 PDF 页面对象（TI 245 页、ST 2,222 页）。TI 的封装图、ST 的表/框图等图形密集页不能只靠文字抽取；本轮已对 13S 表/EVM 原理图、BQ 寄存器与时序页、F103 资源/引脚、时钟树、Flash 组织、NVIC 分组和勘误关键页做视觉复核。这里的“完整阅读”是逐页抽取覆盖、项目相关章节人工细读和关键图表视觉核验，不声称对 2,467 页逐字人工朗读。

### 2.2 权威等级与冲突规则

1. **Level 1 — 项目意图**：最终 BMS V1 设计规格决定“做什么、如何组织”。
2. **Level 2 — 芯片事实**：对应型号的 TI/ST 英文 datasheet 决定型号、寄存器、bit、公式、资源和绝对限制。
3. **Level 3 — 官方机制**：TI application report、ST RM/Programming Manual/Errata 决定工作机制、时序、编程流程；Errata 对 RM 中受影响的特性具有修正作用。
4. **Level 4 — 参考实现**：EVM、SPL 模板、示例代码只能证明参考做法，不能替代目标 BOM/板卡/工程验证。
5. **Level 5 — 辅助材料**：中文翻译、旧版资料和学习材料仅辅助理解。

冲突处理：L1 决定产品选择，L2/L3 决定芯片事实。若 L1 写错芯片事实，必须先显式登记勘误，由规格修订吸收，不能在代码中静默“纠正”。

## 3. C — 最终架构复述

STM32F103C8T6 是唯一 MCU，按官方 64 KB Flash/20 KB SRAM、Medium Density、SPL 和原生 FreeRTOS 构建。8 MHz HSE 经 PLL×9 得 72 MHz；PCLK1=36 MHz、PCLK2=72 MHz。SysTick 只服务 1 ms FreeRTOS tick；TIM3 以 72 MHz timer clock、PSC=71 形成 1 MHz 微秒时基。

单颗 BQ7694003 负责 13 节电芯、电流积分、TS1 温度、OV/UV/OCD/SCD 硬件保护、低边 CHG/DSG 控制及被动均衡。PB8/PB9 使用 100 kHz 级软件开漏 I2C，CRC 版本地址为 7-bit `0x08`，所有事务必须有线电平 timeout 和经板级验证的恢复策略。13S 物理/ADC/均衡映射跳过 VC9、VC14；不能用逻辑序号直接访问连续 VC。

PB1/EXTI1 接收 active-high ALERT。ALERT 是各 SYS_STAT 源的 OR，不是纯 fault：约 250 ms 的 CC_READY 同样触发。ISR 只清 STM32 pending 并通知 ProtectTask；ProtectTask 在任务上下文串行读取/处理所有 bit，成功消费后才 W1C，并把 CC 样本送给 SOCTask。硬件保护先行关管；软件保护基于带 freshness 的快照，用更保守阈值、迟滞和去抖形成二级保护。

固定七任务为 Protect、Sample、State、SOC、Balance、CAN TX、CAN RX。SampleTask 约 250 ms 读电芯/BAT，约 2 s 读温度；SOCTask 使用 CC_READY 样本做高分辨率库仑积分，以首次 OCV 初始化、静置 OCV 缓慢纠偏，不作为保护依据且不宣称独立 fuel-gauge 精度。BalanceTask 仅在充电、无故障、温度/最低电压/压差合格时每次最多均衡一节。

StateTask 管理 INIT/STANDBY/CHARGE/DISCHARGE/FAULT、软件保护、故障恢复、数据 stale、统一的 CHG/DSG permission 和系统健康。所有模块只能提交“许可/请求”，一个串行化路径生成 FET 最终状态，防止彼此覆盖。IWDG 只能由监督者在全部必需任务满足健康条件后喂狗。

CAN 使用 bxCAN、PA11/PA12、500 kbit/s、自定义 29-bit Extended ID。RX ISR 只 drain FIFO 并入队，CANRxTask 校验地址/序号/CRC/安全条件；所有硬件发送只由 CANTxTask 完成。参数采用 Flash A/B、校验、序号和 commit-last 设计；实际页固定在官方 64 KB 边界内。所有 Flash 写入都必须考虑全 Flash 取指停顿、掉电、耐久与看门狗。

## 4. D — Module Dependency Map

```text
Config  <-------------------- 所有层只读配置/编译期约束
  ↑
Driver  <---- Service <---- App
  ↑             ↑          ↑
  └---------- Protocol ----┘

外部：CMSIS/SPL -> Driver；FreeRTOS -> Service/App/Protocol 的适配边界
```

| 层 | 允许职责 | 允许依赖 | 禁止 |
|---|---|---|---|
| Config | 引脚、周期、阈值、协议常量、编译断言 | 无业务层 | 可变运行状态、硬件访问 |
| Driver | GPIO/TIM/UART/CAN/Flash/IWDG/soft-I2C/CRC/BQ 寄存器与物理量 | Config、CMSIS/SPL；I2C backend | SOC、状态机、fault recovery、CAN 业务格式 |
| Service | 数据快照、param store、health、时间/持久化服务 | Driver 抽象、Config、FreeRTOS | 直接解析业务 CAN ID；决定产品状态机 |
| Protocol | 29-bit ID、序列化/解析、命令校验与响应编码 | Config、Service 的窄接口、CAN frame DTO | 直接访问 BQ/Flash/CAN 寄存器；直接开管 |
| App | 七任务、保护、状态、SOC、均衡、permission 仲裁 | Driver API、Service、Protocol、FreeRTOS | 被下层反向调用；绕过单一写者 |

必须用事件/队列或窄接口消除反向依赖；Driver 不包含 App 头文件，Protocol 不直接调用 FET/Flash driver。建议固定锁顺序：业务计算不同时持有 `xDataMutex` 与 `xI2CMutex`；需要硬件更新时先取本地 snapshot、释放 data lock，再限时取得 I2C lock。Flash 临界区不得持有 I2C/data mutex。

## 5. E — RTOS Resource Map 与并发审查

### 5.1 任务与对象

| Task | 规格优先级 | 周期/触发 | 主要输入/输出 | 并发约束 |
|---|---:|---|---|---|
| ProtectTask | 5 | ALERT semaphore + 有界重试/轮询 | SYS_STAT、CC -> fault/CC queue | 最高应用优先级；I2C lock 必须有 timeout；drain 到 ALERT 低 |
| SampleTask | 4 | 250 ms；TS 每 8 次 | VC/BAT/TS -> snapshot/event | 持 I2C 锁只做事务；锁外换算/提交 |
| StateTask | 3 | 100 ms | snapshot -> state/protect/permission/health | 数据锁只复制；Flash 操作不得在实时临界段直接执行 |
| SOCTask | 3 | CC queue，超时唤醒用于维护 | CC/timestamp -> SOC | 积分使用真实时间戳，不把固定 250 ms 当唯一真值 |
| BalanceTask | 2 | 1 s | snapshot -> CELLBAL desired | I2C 有界等待；故障/无效时优先 stop-all |
| CANTxTask | 2 | queue + 10 ms/周期 deadline | snapshots/urgent queue -> bxCAN | 唯一 CAN_Transmit 调用者；避免 self-enqueue 饥饿 |
| CANRxTask | 2 | RX queue + 健康 timeout | frame -> validated command | 不用无限等待后再要求每秒 heartbeat；安全命令不可广播执行 |

对象：`xI2CMutex`（带优先级继承）、`xDataMutex`、`xAfeAlertSem`、`xCanTxQueue(24)`、`xCanRxQueue(12)`、`xCcSampleQueue(8)`、`xSysEvents`。EXTI1 和 CAN RX0 ISR 调 `FromISR` API，均不得持锁、I2C、Flash、printf 或做业务解析。

### 5.2 优先级、死锁与持锁结论

- 规格优先级结构本身合理，但现 `FreeRTOSConfig.h` 的 `configMAX_PRIORITIES=5` 只允许 0..4；Protect=5 会被内核钳制为 4，且 `configASSERT` 未启用，形成 Critical 静默降级。
- 规格预期 `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5`、EXTI1=6、CAN=7；现 config 使用 raw `0xBF`，硬件有效为 `0xB0`（库优先级 11），因此 6/7 不能调用 `FromISR`。正确值必须由 `(5 << (8-4))` 统一导出为 `0x50`，不是保留 `0xBF`。
- `xI2CMutex` 的优先级继承可缓解 Sample/Protect 优先级反转，但不能弥补无界持锁。Balance 当前伪代码使用 `portMAX_DELAY`，应改为有界等待/失败即保持安全状态。
- 规格没有明确跨锁顺序；若 State/param/FET 路径同时拿 data/I2C，存在 AB-BA 风险。实施时禁止嵌套两把 mutex，采用 snapshot + 单一写者。
- ProtectTask 拿锁 20 ms 失败后直接等待下一上升沿会漏事件；必须设置 pending/retry 或轮询线电平。
- CANRxTask `portMAX_DELAY` 与“每任务每窗口 heartbeat”矛盾：总线空闲会让健康监督误判。改用有界 receive timeout 或将阻塞等待定义为健康状态。
- CANTxTask 既生产周期帧又消费同一队列，需规定队列满策略、紧急帧保留容量/独立路径，避免普通帧填满后 fault 上报失败。

### 5.3 当前 FreeRTOS 基础审查

| 项 | 当前文件 | 结论 |
|---|---|---|
| Kernel | V11.1.0 | 源文件齐全；`FreeRTOSConfig.h` banner 是 V202212.00，来源代际不一致 |
| Port | legacy RVDS/ARM_CM3 风格 | 适配 ARMCC5；最终工具链未选，GCC port 缺失 |
| Heap | 唯一 `heap_4.c` | 候选明确但尚无工程 source list；本轮不替换 |
| Heap size | 17 KiB | 与规格 8 KiB 冲突；在 20 KiB SRAM 上几乎不给 `.data/.bss` 和 MSP 留余量，High |
| Stack check/assert | 默认关闭 | 与规格 `configCHECK_FOR_STACK_OVERFLOW=2` 冲突；FromISR/优先级错误不可诊断，High |
| Event groups | kernel 默认启用 | 文件存在；可用性仍需编译测试 |
| Timers | 文件存在、功能默认关闭 | 当前架构不必开启；若使用 `xEventGroupSetBitsFromISR` 才需 timer pend path |
| 异常 handlers | port 宏映射 SVC/PendSV/SysTick | 当前 SPL 模板三者已注释，静态无重复；集成时仍须唯一 |

## 6. F — BQ76940 实现准入清单

### 6.1 器件、连接与启动

- [ ] BOM/丝印确认 `BQ7694003DBT/DBTR`；EVM U1 实为 `BQ7694000DBT`，不能替代目标变体证据。
- [ ] 固定 7-bit `0x08`、写 `0x10`、读 `0x11`、REGOUT 3.3 V、CRC enabled。
- [ ] 实图核对 VC5B/VC10B、`VC9=VC8`、`VC14=VC13`、各组至少 3 cells、电源/二极管/RC/CAP/REGSRC/TS/ALERT。
- [ ] 逻辑 VC/CB 均为 `[1..8,10..13,15]`；永久屏蔽 CB9/CB14；逐路注入验证 13S 映射。
- [ ] PA8 到 TS1 的硬件能形成受控 rising edge，且不破坏 TS1 温度测量；约 1 ms I2C、10 ms boot、800 ms 首批 cell 的等待均有状态和 timeout。
- [ ] `CC_CFG=0x19`、ADCGAIN/OFFSET、PROTECT/OV/UV、ADC/TEMP/CC 全部写后读回；首个 CC_READY 预计约 400 ms。

### 6.2 I2C/CRC/恢复

- [ ] 开漏释放后读取 SCL/SDA；START/STOP/ACK/NACK/clock-stretch/每个阶段均有有界 timeout。
- [ ] CRC golden vectors 覆盖单字节和 block read/write；首读数据 CRC 包含 `0x11`，首写包含 `0x10`，后续字节只含当前 data。
- [ ] HI/LO 使用同一自动递增事务原子读取；CRC/NACK/部分事务失败不提交数据。
- [ ] 9-clock + STOP 是通用 I2C 恢复假设，不是本地 TI 资料给出的 BQ 专属保证；必须在 SDA/SCL 卡死、BQ 无电和 SHIP 实板场景验证。
- [ ] 通信恢复先区分 bus stuck、无电、SHIP/POR；不得每秒无条件向正常 TS1 重复 wake。

### 6.3 转换、采样与温度

- [ ] Gain 组合/offset 有符号、电芯 raw14、CC int16 和 `raw*8440/Rsense_uΩ -> mA` 建立极值 golden vectors；32 位中间值安全。
- [ ] BAT 明确实现 `4 × gain × BAT_raw + 13 × offset`，统一 µV/mV 并使用宽整数。
- [ ] TS 使用目标 03 的 REGOUT 和 RTS/NTC 公差、实测 NTC 表；不能把示例 10 k/3.3 V 公式当量产曲线。
- [ ] 三个 5-cell group 调度独立，13 节不宣称同步采样；每个样本带时间戳、valid/range/age。

### 6.4 SYS_STAT / ALERT / 保护 / FET

- [ ] 同一 stat 快照逐 bit 处理 CC_READY、XREADY、OVRD_ALERT、UV/OV/SCD/OCD，不用互斥 `else-if`。
- [ ] 只有成功消费的事件才 W1C；CC 读取/入队失败不得清 CC_READY。
- [ ] 增加 OVRD_ALERT 故障、关管、上报和安全恢复路径；当前伪代码完全遗漏。
- [ ] XREADY 不立即 W1C；保持锁存、等待、重初始化三组/校准/配置并验证后再清，FET 不得自动恢复。
- [ ] 服务使用有界 drain loop：读—处理—只清已处理位—重读，直至 stat=0/ALERT 低或触发重试故障。
- [ ] PROTECT1/2/3、OV/UV delay、量化方向和最差容差形成表驱动并读回；真实 Rsense 决定阈值。
- [ ] SYS_CTRL2 只有一个写者；写前重查 stat/permission，由当前许可重新合成 CHG/DSG 而非保留陈旧输出位；写后读回并重查 fault，防止硬件自动关管后被旧 RMW 重新打开。
- [ ] 硬件保护恢复后也只由统一 permission 显式重新开管；FET actual 由读回/状态推断，不把 command 当 actual。

### 6.5 平衡

- [ ] V1 一次一节、仅充电、无 fault、数据新鲜、温度/最小电压/压差通过；所有退出条件 stop-all。
- [ ] 验证实际输入电阻、内部通路约束、平衡电流/约 70% duty、温升、滤波恢复和跨 group 影响；TI 对 76930/40 推荐外部平衡，内部方案需性能/热验证。
- [ ] XREADY 或 SHIP→NORMAL 会清 CELLBAL；软件维护 desired/actual 并读回，不假设旧状态。

## 7. G — STM32 实现准入清单

| 子系统 | 必须固定/验证的参数 |
|---|---|
| Target/build | `STM32F10X_MD`、`USE_STDPERIPH_DRIVER`、只选 `startup_stm32f10x_md.s`；RAM=20K；应用 Flash=62K；image end `<0x0800F800` |
| Clock | HSE=8 MHz、PLL×9、HCLK=72、PCLK1=36、PCLK2=72；HSE/PLL/SW 均需 timeout/fail-safe；读回验证，不能 HSE 失败后仍假称 72 MHz |
| GPIO/AFIO | PB8/PB9 OD，硬件 I2C1/TIM4 AF 关闭；PA11/12 CAN 默认映射，USB关闭；PB1 映射 EXTI1；PA8 wake 电路待实图确认 |
| TIM3 | timer clock=72 MHz、PSC=71、ARR=0xFFFF、UG 后启动；模减延时小于 65,536 µs；不启 CH4/占 PB1 |
| EXTI1 | active-high/rising，启用前清 PR，启用后立即检查已高；ISR priority=6；任务级 drain/retry 到线低 |
| CAN | PCLK1 36 MHz；SPL Prescaler=9、BS1=6 tq、BS2=1 tq、SJW=1 tq，500 k/87.5%；raw BTR 字段是数量减 1；extended/filter packing 实测；TTCM=0 |
| CAN ISR | `USB_LP_CAN1_RX0_IRQHandler`；priority=7；循环 drain 3-deep FIFO，统计 FOVR/queue-full；明确 ABOM/NART/TXFP/RFLM/bus-off 策略 |
| USART1 | PA9/10，PCLK2=72 MHz，115200 8N1，BRR=0x0271；保持未用 TIM1_CH2 关闭或按 ES096 workaround |
| Flash | C8 writable end `0x0800FFFF`，`0x08010000` 仅 exclusive end；1 KB pages；A=0x0800F800/B=0x0800FC00；HSI on、halfword、BSY/error/readback |
| Flash atomicity | 唯一有效页永不先擦；magic/version/length/sequence/CRC；数据/校验完成后最后写 commit marker；定义 wrap 比较、BOR/PVD/低压禁止写、10k 耐久预算 |
| IWDG | 只由 supervisor 喂；先定 PR/RLR，按 LSI 30/40/60 kHz 计算 timeout 上下界，覆盖启动、40 ms page erase+program、调度最坏延迟 |
| NVIC/RTOS port | 4 priority bits、PriorityGroup_4；FromISR IRQ 数字优先级 >=5；PendSV/SysTick lowest，SVC/handlers 唯一；SysTick reload=71999 且不由 BSP 重配 |
| Errata | Flash BSY 延迟、LSI 稳定、USART1/TIM1、bxCAN TTCM 不支持、低功耗/Cortex-M3 条目纳入验收 |

静态 `system_stm32f10x.c` 的 72 MHz 分支确实配置 HSE×9、APB1/2；但因为没有工程、没有运行读回，不能声称硬件已在 72 MHz。其 HSE fail 分支为空，是必须补的 High 风险。

## 8. H — 冲突与风险报告

### Critical — 进入 Phase 1 前必须关闭

| ID | 冲突/风险 | 证据 | 应修改项与影响 |
|---|---|---|---|
| C-01 | ProtectTask 优先级越界 | 规格 task=5；现 `configMAX_PRIORITIES=5` 只允许 0..4，且 assert 关闭 | 修订最终 FreeRTOSConfig 设计为至少 6、规格建议 8；否则 Protect 与 Sample 同级，保护实时性设计失效 |
| C-02 | FromISR 优先级门槛冲突 | 规格 max syscall=5、EXTI=6/CAN=7；现 raw `0xBF` 等效库优先级11 | 规格勘误明确 `0x50`/公式及 priority grouping；否则 ISR 调 kernel API 违反端口约束、可能破坏内核 |

### High — 代码生成前形成确定设计/测试

| ID | 风险 | 位置/影响 | 处理要求 |
|---|---|---|---|
| H-01 | OVRD_ALERT 漏处理 | 规格定义 bit，Protect 伪代码不处理；FET 可已被硬件关且 ALERT 长高 | 独立 fault/action/recovery/W1C 路径 |
| H-02 | CC 读失败仍清 CC_READY | 样本被无条件丢弃；下次约250 ms覆盖且无 overrun flag | 只在原子读取和消费成功后清；失败重试/记 missed |
| H-03 | XREADY 立即清除 | TI 要等待并验证；立即清会掩盖堆叠异常并过早恢复 | 锁存、等待、reinit、三组/配置验证、再清 |
| H-04 | SYS_CTRL2 陈旧 RMW 重新开管 | 读回到写回之间硬件可能因 fault 自动清 FET | 单一写者；写前/后检查；CHG/DSG 从当前 permission 合成 |
| H-05 | ALERT 上升沿+二值 semaphore 丢事件 | ALERT 已高时无第二个 rising；拿锁失败直接 continue 更危险 | EXTI 启用后检查电平；pending retry；有界 drain 到低 |
| H-06 | Flash 操作阻塞全 Flash 取指/中断 | page erase 最大约40 ms，影响 ALERT/CAN/RTOS/I2C/IWDG | 定义写入门禁/临界区/停顿预算并压力实测；不可在普通 StateTask 路径随意擦写 |
| H-07 | A/B“掉电不会同时损坏”保证过强 | Flash 不提供事务原子性；错误擦页/低压/复位仍可双坏 | commit-last、CRC/readback、唯一有效页保护、brownout门禁 |
| H-08 | FreeRTOS heap=17 KiB/20 KiB SRAM | 再加 MSP/C heap、全局和队列，极可能无安全余量 | 先做静态预算/map；采用确认后的 heap（规格建议8 KiB），不可直接复用17 KiB |
| H-09 | assert/stack overflow check 关闭 | 优先级/FromISR/栈错误静默 | 最终 config 打开 `configASSERT` 和 stack check=2，提供 hooks |
| H-10 | 工具链/port/build 未定 | 当前 port/startup 是 ARMCC5 风格，无工程/linker，无法构建 | 用户确认工具链；选择与之匹配的官方 port/startup；不得随意升级 kernel |
| H-11 | IWDG 参数未闭环 | 只有“1s监督”，无 PR/RLR；LSI 30–60 kHz | 决定可接受 min/max timeout 后计算并测试 |
| H-12 | HSE fail 后时钟谎报 | system 文件失败分支为空，config仍硬编码72 MHz | 启动 fail-safe；运行时读回；禁止以错误时钟启动 CAN/RTOS |
| H-13 | I2C 完全失效时软件不能保证物理关管 | 规格已明确：失去 BQ 通信后 MCU 可能无法经 SYS_CTRL2 关闭 CHG/DSG | 保持这一安全声明；保护底线依赖 BQ 硬件保护、外围默认关断与功率级设计，并用原理图/故障注入验证，禁止在软件文档中作更强保证 |

### Medium

- CAN RX ISR 示例只取一帧；FIFO 深度 3，必须 drain、统计 overrun，并与 Flash 停顿并发压力测试。
- CAN 控制协议允许 `broadcast` 地址匹配，却没有明确禁止广播执行 CHG/DSG/parameter write/fault reset；安全变更命令应只接受单播，广播只读/发现策略需确认。
- Application CRC8 与 BQ CRC8 都写成 CRC8，但规格未为 CAN 明确独立多项式/init/reflection/test vector；必须分名和定义。
- BAT 公式、PROTECT3 OV/UV delay、RSNS 位/量化方向、容差和读回仍不完整。
- 9-clock bus recovery 是通用 I2C 假设，非本资料集中的 BQ 专属保证；必须板级验证。
- 每秒 wake/probe/reinit 未区分正常在线/SHIP/无电/总线挂死，可能干扰 TS1。
- 参数更新流程写“先 update RAM、apply BQ、成功 swap active”措辞不一致；应为 staging->BQ验证->原子 swap。
- SOC 持久化提出60 s/5 min两个口径，但仅分配两页参数 A/B，没有独立记录布局；若共用参数页会快速消耗耐久且破坏参数原子性。
- CANRxTask 无限等待与健康心跳矛盾；CANTx 队列容量/紧急帧保障未定义。
- BQ 三组采样非同步；算法需时间戳，不能将 13S 视为同时采样。
- 规格“内部平衡”逻辑可行但 TI 对 BQ76930/40 推荐外部平衡；必须核对输入电阻、电流、约70% duty和热预算。
- Flash `0x08010000` 应命名 exclusive end；链接应用长度必须 62K，map 断言末端低于 A 页。
- RM 描述 TTCM，但 ES096 明确本器件 bxCAN TTCM 不支持；必须保持 TTCM=0。
- 参数 record 没有最终 commit marker/sequence wrap/brownout 细节。

### Low

- 同目录存在 8 个 startup，工程通配会产生多向量/Reset_Handler；显式只选 MD。
- `Libarary` 拼写异常易造成路径配置错误；保持实际路径或在工程布局阶段有意识重整，勿修改原始 SPL 副本。
- 状态名 `INIT` 与 BQ 的 boot 过程可并存，但文档不得把 boot 实现成第三个 BQ 设备模式；官方仅 SHIP/NORMAL。
- 温度/OCV/容量/阈值均为示例或默认，不能标成已校准或实测。

## 9. I — Phase 1~12 实施计划

本计划保持规格的 12 阶段顺序，但加入每阶段门禁。以下“修改文件”是计划路径，不代表本轮已创建。

| Phase | 输入 | 计划修改文件（均为后续新建/修改） | 输出 | 验证方法 | 完成条件 | 下一阶段依赖 |
|---:|---|---|---|---|---|---|
| 1 基线/模型 | 本报告、确认后的规格勘误、工具链决定 | `Config/bms_config.h`；`App/bms_types.h`、`bms_data.[ch]`、`bms_fault.[ch]`、`bms_state.h`；`User/main.c`；目标工程与 linker/scatter 文件 | 可编译空工程、单位/极性/边界静态约束 | 编译宏、link map、host unit compile、无 HAL 搜索 | 64K/20K/62K 边界与目录/接口冻结 | P2 使用工程、配置和公共模型；P6 使用构建基线 |
| 2 MCU底层/I2C/CRC | P1 工程、SPL、确认引脚 | `Driver/bsp_gpio.[ch]`、`bsp_timer.[ch]`、`bsp_uart.[ch]`、`soft_i2c.[ch]`、`crc8.[ch]`；`Tests/test_crc8.c`、`test_soft_i2c.c` | 1 MHz 时基、100 kHz I2C backend、CRC 库 | scope/logic analyzer；timeout/stuck-line；CRC vectors | 时钟/引脚/CRC/恢复通过 | P3 使用 I2C/CRC backend |
| 3 BQ寄存器/校准 | P2 backend、TI Rev I | `Driver/bq76940.[ch]`；`Tests/test_bq_transport.c`、`test_bq_conversion.c` | CRC read/write/block、gain/offset、基础转换 | mocked transaction transcript、寄存器 golden vectors、目标03 probe | 所有保留位、读写和校准读回通过 | P4/P5 使用经验证的寄存器访问与转换 |
| 4 13S采样 | P3、13S实图、Rsense/NTC资料 | `Driver/bq76940.c`；`Config/bms_config.h`；`Tests/test_bq_mapping.c`、`test_bq_measurement.c` | 带 timestamp/valid 的 13S/pack/current/temp | VC/CB mapping unit test；逐路电压/CC/NTC台架 | VC9/14 跳过、公式/极性/新鲜度通过 | P5 使用物理映射；P8/P10 使用测量 API |
| 5 BQ保护/FET/均衡 | P3/4、最终硬件参数与容差 | `Driver/bq76940.[ch]`；`Tests/test_bq_protection.c`、`test_bq_fet_balance.c` | PROTECT 编码、permission-safe FET writer、CELLBAL | fault injection、readback、RMW竞态、热/平衡测试 | OVRD/XREADY/恢复/FET/平衡门禁通过 | P7 使用 SYS_STAT/FET API；P9/P10 使用保护/均衡 API |
| 6 RTOS基础 | P1、工具链匹配 port、SRAM预算 | `Config/FreeRTOSConfig.h`；`App/app_tasks.[ch]`；`User/stm32f10x_it.[ch]`；工程 source list；`Tests/test_rtos_config.c` | 7任务和对象，NVIC/stack/heap闭环 | build、map、assert、stack high-water、scheduler smoke | Critical C-01/C-02 关闭且 SRAM 裕量合格 | P7/P8/P9/P11 使用任务、对象与 ISR 约束 |
| 7 ALERT/Protect | P3/5/6 | `Driver/bsp_exti.[ch]`；`User/stm32f10x_it.c`；`App/bms_protect.[ch]`、`app_tasks.c`；`Tests/test_alert_protect.c` | 有界 drain、全 SYS_STAT 处理、CC 事件链 | 叠加 bit/长高/锁超时/读失败/XREADY/OVRD 注入 | 不丢事件、不误清、不在 ISR 访问 I2C | P8 使用 sample-ready/fault；P9/P10 使用 CC/fault 事件 |
| 8 Sample/data | P4/6/7 | `App/bms_data.[ch]`、`app_tasks.c`；`Tests/test_sample_freshness.c` | 250 ms cell/BAT、约2 s temp、stale模型 | cadence/jitter、失败/partial read、三组时间戳测试 | 数据提交原子，valid/range/age 可靠 | P9/P10/P11 使用可信 snapshot |
| 9 状态/软件保护/健康 | P5/7/8、最终阈值/IWDG决定 | `App/bms_state.[ch]`、`bms_protect.[ch]`、`bms_fault.[ch]`、`app_tasks.c`；`Service/system_health.[ch]`；`Driver/bsp_iwdg.[ch]`；相关测试 | 状态机、二级保护、单一FET仲裁、监督 | threshold/debounce/hysteresis/race、任务挂死、IWDG min/max | 所有恢复均经当前安全条件和 permission | P10 使用状态/许可；P11 使用安全命令接口；P12 使用健康门禁 |
| 10 SOC/均衡App | P4/5/7/8/9、电芯OCV/Qmax | `App/bms_soc.[ch]`、`bms_balance.[ch]`、`app_tasks.c`；`Config/bms_config.h`；`Tests/test_soc.c`、`test_balance.c` | 高分辨率积分、OCV初始化/缓纠偏、一节均衡 | 时间戳/丢样/符号/边界/长期误差仿真；均衡门禁 | 不用 SOC 做保护；无实测曲线时保持实验/待标定状态 | P11 使用 SOC/均衡快照；P12 使用运行记录接口 |
| 11 CAN | P6/8/9/10、确认后的协议安全规则 | `Driver/bsp_can.[ch]`；`Protocol/bms_can_protocol.[ch]`、`bms_command.[ch]`；`Config/can_protocol_cfg.h`；`App/app_tasks.c`；`User/stm32f10x_it.c`；相关测试 | 500 kbit/s extended协议、单TX出口、安全命令 | bit timing、filter/序列/CRC/广播/queue/bus-off/压力 | RX不溢出、fault有带宽、安全命令不可绕过 permission | P12 使用参数命令与集成通信；完成后无其他功能阶段依赖 |
| 12 Flash/集成验收 | P1/6/9/10/11、brownout/IWDG与SOC记录决定 | `Driver/bsp_flash.[ch]`；`Service/param_store.[ch]`、经确认时的 `soc_store.[ch]`；`Tests/` 集成/掉电测试；`README.md`；工程/linker 文件 | A/B+commit、运行记录、集成工程与可追溯验证记录 | 掉电逐半字注入、CRC/sequence wrap、10k耐久预算、full build/map/static/target matrix | 满足规格§64且无伪造实测，才可宣布 V1 实现完成 | 无；回到评审/发布门禁，不自动扩展 V1 范围 |

每个阶段只提交其自身文件、接口、测试和证据；若前一阶段完成条件未满足，下一阶段不得靠 TODO 掩盖硬件安全缺口。

## 10. J — 真正需要用户确认的问题

下列问题无法从现有仓库和 12 份官方资料中唯一决定，且会阻碍正确实现：

1. **目标工具链/工程格式是什么？** 当前 startup 和 FreeRTOS port 是 legacy ARMCC5/RVDS 风格；若选 GCC/arm-none-eabi 或 ArmClang6，必须换用匹配的官方 port/startup/链接描述，但不升级 FreeRTOS kernel。请在进入 Phase 1 前指定 MDK-ARM5、ArmClang6、GCC 或其他目标。
2. **请提供/确认目标硬件证据**：原理图、BOM/器件丝印和必要的 PCB 连接页。必须确认 BQ7694003（非 EVM 的4000）、13S VC/VCxB/VC9/VC14、PA8→TS1、ALERT RC、TS2/TS3、实际 Rsense、MOS/充放电极性。没有这些只能实现可配置逻辑，不能完成板级准入。
3. **真实电池与保护参数是什么？** 需要电芯型号/化学体系、容量/Qmax、实测或供应商 OCV-温度曲线、NTC 型号/表、Rsense 及目标 OV/UV/OCD/SCD/SW OC/温度阈值与延时。规格中的 20 Ah、4 mΩ 和阈值是示例/默认，不能当量产标定。
4. **IWDG 允许的复位窗口是什么？** 请给出希望的最小/最大故障检测时间；随后才能在 LSI 30–60 kHz、Flash 最坏停顿和调度预算下决定 PR/RLR。
5. **CAN 安全策略**：是否明确禁止广播执行 CHG/DSG enable、fault reset 和参数写？同时请确认 CAN application CRC8 的多项式/init/reflection/xorout/test vector（它不能仅用“CRC8”四字留白）。
6. **SOC 运行记录的 Flash 布局与写入策略**：L1 已确定 V1 保存简化 SOC 运行记录，但同时给出“变化≥1%或60 s”和“可每5 min一次”两个口径，且没有分配独立页或 wear-level 结构。请确认最终触发频率、记录区域/页数、是否采用日志式 wear-level；不得与两页参数 A/B 共用并高频擦写。

以上六项之外，寄存器 bit、地址、13S 映射、时钟、Flash 页、CAN 时序以及 I2C 完全失效时的软件安全边界不再向用户提问，已由仓库/官方资料核实。

## 11. 实现门禁与停止点

进入 Phase 1 前必须同时满足：

1. 用户确认本报告的六个开放问题，至少先确认工具链和硬件资料可用性；
2. 最终设计规格形成显式勘误，关闭 C-01、C-02、H-01~H-05，并澄清 Flash/IWDG/CAN/SOC 持久化策略；
3. Phase 1 只建立工程/配置/数据模型与构建边界，不提前生成 BQ 业务、七任务行为或完整工程；
4. 后续所有“通过”都绑定构建/单测/目标板证据；本报告没有、也不暗示任何硬件实测成功。

本报告到此停止，未进入 Phase 1。
