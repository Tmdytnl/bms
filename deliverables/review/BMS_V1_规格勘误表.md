# BMS V1 规格勘误表

- 项目：BMS V1 Reference Firmware Project
- 收敛日期：2026-08-13
- 适用阶段：Software Implementation Gate 关闭；Phase 1 尚未开始
- 适用基线：`docs/spec/BMS_V1_统一项目方案_软件设计规格.md` SHA-256 `7e71125d6acdad5ba3d8203a5bd9cff51168051c895cfd31b76f663266576472`
- 前序报告：`deliverables/review/BMS_V1_开发准备报告.md` SHA-256 `22cac90f445f8c420f27699c2a03ce50977cceb42209cf0b8d87457db788336c`
- 本轮决策记录：用户提供的基线收敛说明 SHA-256 `8475ac7507ef42dacc4702de68be9df59634812d43c21006e91acde071c2ef4f`

## 1. 适用规则

本表是对上述规格精确修订的显式覆盖层。规格中与本表冲突的旧描述、伪代码、数值或开放问题，以本表的“正确设计”为准；未被本表覆盖的项目边界和架构继续有效。实施时不得只读取旧伪代码而忽略本表。

状态仅使用：

- `CLOSED`：选择或事实已经确定，不再需要设计决策。
- `CLOSED BY DESIGN`：风险已由明确、可实施和可测试的设计约束关闭；代码与测试仍在对应 Phase 完成。
- `MITIGATED`：已有可行缓解，但仍需后续资源或运行证据验证裕量。
- `HARDWARE VALIDATION REQUIRED`：不阻塞参考软件实现，但必须在真实硬件门禁中验证。
- `OPEN`：仍缺少会阻止 Phase 1 的软件决策。

## 2. 证据边界

| 证据类型 | 本轮可以得出的结论 | 不可扩大为 |
|---|---|---|
| 用户确认的旧工程真实构建日志 | Keil MDK5、ARMCC5 V5.06 update 7 build 960、SPL/CMSIS、FreeRTOS V11.1.0 RVDS/ARM_CM3 与 heap_4 的基础组合曾成功编译链接，结果 `0 Error(s), 0 Warning(s)` | 当前仓库或尚未创建的 BMS 工程已经构建成功 |
| 用户确认的旧工程链接摘要 | `Code=30544`、`RO-data=392`、`RW-data=844`、`ZI-data=19308`，旧工程 `RW+ZI=20152` bytes，证明旧 RAM 配置不可照搬 | BMS V1 的 8 KiB heap、七任务栈或全部队列已经通过 SRAM 预算 |
| 当前仓库静态检查 | 源文件、版本、寄存器/地址、port 语法、时钟和中断配置的静态一致性 | ARMCC5 本机复现构建或目标板运行证据 |
| TI/ST 官方资料审查 | 芯片事实、时序、资源、寄存器与硬件限制 | 目标 PCB、BOM、标定、温升、EMC 或安全认证结果 |

当前仓库没有旧工程 `.uvprojx`、map 或完整编译日志，且本机未发现 ARMCC5，因此无法重放旧构建。该边界不再阻止 Phase 1；Phase 1 的职责之一就是建立当前 BMS 专用 Keil 工程并获得可复现构建与 map 证据。

## 3. 核心问题勘误

| ID | 原问题 | 原设计/旧配置 | 正确设计 | 修改原因 | 官方依据类型 | 实现影响 | 状态 |
|---|---|---|---|---|---|---|---|
| C-01 | ProtectTask 优先级越界 | 参考 `FreeRTOSConfig.h` 为 `configMAX_PRIORITIES=5`，合法任务优先级仅 0..4；ProtectTask 计划为 5 | 新建 BMS 专用 `FreeRTOSConfig.h`，固定 `configMAX_PRIORITIES=8`；七任务优先级保持 5/4/3/3/2/2/2 | 避免 ProtectTask 被静默钳制为 4，恢复保护任务的调度层级 | FreeRTOS kernel 配置语义；项目设计决策 | Phase 1/6 生成专用配置；用 `configASSERT`、编译和调度测试验证 | CLOSED BY DESIGN |
| C-02 | FromISR 优先级门槛冲突 | 旧 config 使用 raw `0xBF`，在 4 priority bits 硬件上有效为 `0xB0`/库优先级 11，与 EXTI=6、CAN=7 冲突 | `configPRIO_BITS=4`，`configLIBRARY_LOWEST_INTERRUPT_PRIORITY=15`，`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5`；派生 raw kernel=`0xF0`、max syscall=`0x50`；`NVIC_PriorityGroup_4`；EXTI1=6，CAN RX0=7 | FromISR ISR 必须使用数值不小于 5 的逻辑优先级；raw 未实现低位必须为 0 | STM32 4-bit NVIC；FreeRTOS RVDS/ARM_CM3 port assert | 统一用公式派生 raw 值；调度前配置 grouping；启用 port assert | CLOSED BY DESIGN |
| H-01 | ProtectTask 遗漏 OVRD_ALERT | 规格只定义 bit，旧 ProtectTask 伪代码未消费、清除或上报 | OVRD_ALERT 必须有独立 fault、FET safe action、diagnostic、CAN report、recovery policy 和成功消费后的 W1C | 该事件可使 FET 已被硬件关闭并维持 ALERT，不能由其他 fault 隐式覆盖 | TI BQ769x0 datasheet：SYS_STAT/OVRD_ALERT；项目安全策略 | 增加 fault 表示、处理分支、恢复与叠加事件测试 | CLOSED BY DESIGN |
| H-02 | CC_READY 读取失败仍被 W1C | 旧伪代码无论CC读取/入队是否成功都加入clear mask | 仅当原子读取、CRC/事务和本次样本成功进入消费路径后W1C；读取/CRC失败不清。`xCcSampleQueue`满时，必须通过专用queue wrapper在受保护操作中丢弃恰好一个最旧样本并入队本次最新样本，同时递增overflow/missed计数并置诊断事件；只有确认本次最新样本已入队才清CC_READY。替换/入队失败则不清并进入bounded retry/fault path | 新CC约每250 ms覆盖旧值且无overrun标志；明确“保最新、丢最旧”可维持SOC数据新鲜度并消除W1C二义性 | TI embedded scheduler；BQ SYS_STAT W1C；FreeRTOS queue语义 | 实现唯一producer queue wrapper；增加queue-full、并发consumer、read-fail和替换失败测试 | CLOSED BY DESIGN |
| H-03 | XREADY 被立即清除 | 旧伪代码 latch fault 后立刻把 XREADY 加入 clear mask | XREADY→锁存 Fault/禁止自动开管→必要稳定等待→重新初始化 BQ→重读 calibration→重写并验证保护配置和各组状态→成功后最后清 XREADY→重新经过 permission；失败保持 Fault | 立即清除会掩盖 AFE 异常并导致过早恢复 | TI datasheet 与 Top Design Considerations；项目恢复策略 | 实现显式恢复状态机和故障注入测试 | CLOSED BY DESIGN |
| H-04 | 陈旧 SYS_CTRL2 RMW 可能重新开管 | 规格驱动 API 建议普通 read-modify-write；硬件可能在 read/write 间自动清 CHG/DSG | CHG/DSG 只有 single writer；写前重查 SYS_STAT，按当前 state/fault/freshness/permissions/command 重新合成完整 desired bits并保留所需控制位，写后 readback 并再次确认 fault/actual | 防止软件用旧快照覆盖 BQ 硬件保护动作 | TI SYS_CTRL2 与硬件保护行为；并发安全设计 | 所有模块只提交 request/permission/inhibit；禁止直接写 FET bit | CLOSED BY DESIGN |
| H-05 | rising-edge + binary semaphore 可能永久漏 ALERT | 旧 ProtectTask I2C timeout/read fail 后 `continue`，只等待下一次 rising；ALERT 保持高时无新 edge | EXTI 启用后主动检查 pin；ProtectTask 使用 drain+retry：逐 bit 消费、只清成功项、重读 SYS_STAT/pin，直到线低或进入 bounded retry/fault path；mutex/CRC/read failure 均保留 pending | EXTI 是边沿触发，不会为持续高电平重复通知 | STM32 EXTI 边沿机制；TI ALERT OR/W1C 机制 | 增加 pending 状态、限次/限时重试、长高与叠加事件测试 | CLOSED BY DESIGN |
| H-06 | Flash 擦写阻塞实时执行 | 旧描述把参数写视为普通低频任务，未充分计入单 Bank Flash erase/program 对取指和 ISR 的停顿 | 定义 Flash 操作门禁与系统状态；写前不持有 I2C/Data mutex；预算 page erase/program、CAN FIFO、ALERT、IWDG；只在允许窗口执行并记录停顿/溢出诊断 | Medium-density 单 Flash 擦写期间 Flash 读取停顿，page erase 最大约 40 ms | ST PM0075、STM32 datasheet/errata | Phase 12 实现临界区策略并做并发压力验证；硬件时序另进硬件门禁 | MITIGATED |
| H-07 | “A/B 掉电不会同时损坏”表述过强 | 规格仅写inactive page→verify→latest，并宣称掉电不会同时损坏两份；参数流程先改active RAM/BQ再持久化 | record含magic/version/length/`uint32_t sequence`/payload/CRC/16-bit commit marker；永不先擦唯一有效bank。命令先写staging并验证；如需BQ配置则在保留旧active可回滚的事务中试应用并readback。写inactive全部字段时commit保持erased，readback/CRC后以最后一次halfword program写commit，再次验证，之后仅更新RAM cached-active，**不写额外持久化active flag**。commit前任何失败都恢复旧BQ且不切RAM。启动验证commit/magic/version/length/CRC，以`(int32_t)(a-b)>0`（有效序距<`2^31`）选择newest sequence并重建BQ；新bank完整commit前旧bank保持不动，只有下一次更新时才可作为inactive擦除；both-invalid进入配置故障/安全禁止并使用明确的reference fallback流程 | STM32 Flash不提供跨页事务原子性；commit必须是最后持久化写，active选择必须能由两bank自身恢复 | ST Flash halfword/program/erase机制；掉电安全与配置事务设计 | 软件完成逐半字掉电、wrap、both-valid/invalid、BQ apply失败和回滚测试；真实brownout仍需硬件验证 | CLOSED BY DESIGN |
| H-08 | 17 KiB heap 几乎耗尽 20 KiB SRAM | 旧配置heap≈17 KiB；旧工程链接`RW+ZI=20152` bytes；当前startup默认MSP stack=`0x400`、C library heap=`0x200` | BMS初始`configTOTAL_HEAP_SIZE≈8 KiB`，但不是不可变常量；结合map、RW/ZI、startup MSP/C heap、七栈high-water、队列/对象/快照重新核定并保留安全裕量 | 旧业务与17 KiB heap不可继承；startup保留量和FreeRTOS heap均占SRAM；参考实现仍必须适配20 KiB | MCU SRAM容量；用户确认的旧工程map摘要；startup与FreeRTOS heap_4 | Phase 1建立预算，Phase 6/集成用map与high-water收敛；可在Keil工程中有依据地调整startup保留量/栈/对象/heap，不得假装通过 | MITIGATED |
| H-09 | assert/stack overflow 诊断关闭 | 参考 config 未定义 `configASSERT`，stack check 默认 0 | BMS config 启用 `configASSERT`、`configCHECK_FOR_STACK_OVERFLOW=2`，实现 assert handler 与 `vApplicationStackOverflowHook` | 必须捕获优先级、FromISR、stack 和 port 配置错误 | FreeRTOS V11.1.0 kernel/port | 在 Phase 1/6 建立可编译 hook、故障触发和发布策略 | CLOSED BY DESIGN |
| H-10 | 工具链与 port 未确定 | 原报告只能确认 repo 内是 legacy RVDS/ARM_CM3 风格，无法选择 GCC/ARMCC | 正式锁定 Keil MDK5、ARM Compiler V5.06 update 7 build 960/ARMCC5、`STM32F10X_MD`、`USE_STDPERIPH_DRIVER`、`startup_stm32f10x_md.s`、SPL V3.5、当前 CMSIS、FreeRTOS V11.1.0、`portable/RVDS/ARM_CM3`、`heap_4.c` | 用户提供旧 F103 工程真实 `0 Error(s), 0 Warning(s)`证据，且所带 port/startup 语法与 ARMCC5 匹配 | 用户确认的外部真实构建证据；仓库静态版本检查 | Phase 1 只建立 ARMCC5 工程；不得切 GCC/ArmClang/HAL/Cube；不得继承旧业务代码 | CLOSED |
| H-11 | IWDG 无目标窗口 | 规格仅说明supervisor喂狗，没有确定timeout目标与PR/RLR | 设计目标nominal≈2 s；不新增第8任务，既有StateTask是system-health supervisor，也是唯一允许调用`BSP_IWDG_Feed`/`IWDG_ReloadCounter`的应用任务。每个监督窗口检查其他required task heartbeat；StateTask自身健康由其循环进度/deadline直接判定，不要求读取本窗口末尾才写入的自heartbeat，避免自依赖。实现阶段依据LSI 30/40/60 kHz及官方公式选择PR/RLR并列shortest/nominal/longest | LSI容差大，不能宣称精确2.000 s；监督归属、无第8任务和喂狗条件必须唯一 | ST datasheet LSI范围、RM0008 IWDG公式；固定七任务架构 | Phase 9计算PR/RLR、heartbeat窗口和StateTask deadline；目标板测量归入硬件门禁 | CLOSED BY DESIGN |
| H-12 | HSE fail 后仍按 72 MHz 运行 | 现 `system_stm32f10x.c` 的 HSE failure 分支为空，静态 `SystemCoreClock`仍为72 MHz | bounded HSE timeout + PLL timeout + clock switch/readback verification + fail-safe startup；72 MHz tree 未建立时不得启动正常 BMS RUN/CAN/RTOS时序 | 错误时钟会破坏 tick、CAN和TIM3时基 | ST RCC/clock tree；项目 fail-safe策略 | Phase 1/2 建立失败路径和时钟验证；不把参考 system 文件原样当完整启动策略 | CLOSED BY DESIGN |
| H-13 | I2C 完全失效时无法保证物理关管 | 旧 fail-safe 文案可能被误读为 MCU 总能通过 SYS_CTRL2 关 FET | 保留真实边界：通信完全失效时软件 fault latch、stop balance/nonessential control、bounded recovery、diagnostic、禁止主动开管；物理关断底线依赖 BQ 硬件保护、power stage和默认安全态 | 软件无法通过失效链路作物理控制保证 | TI AFE职责边界；系统安全架构 | 软件实现退化策略；原理图、故障注入和实际 FET行为进入硬件门禁，不阻塞 Phase 1 | HARDWARE VALIDATION REQUIRED |

## 4. 其他规格收敛项

| ID | 原问题 | 原设计/旧配置 | 正确设计 | 修改原因 | 官方依据类型 | 实现影响 | 状态 |
|---|---|---|---|---|---|---|---|
| E-01 | Flash 应用区仍按 62 KiB | 规格只保留Parameter A/B：A=`0x0800F800`、B=`0x0800FC00`，应用end `<0x0800F800` | 新增SOC Log=`0x0800F400..0x0800F7FF`；A=`0x0800F800..0x0800FBFF`；B=`0x0800FC00..0x0800FFFF`；应用区`[0x08000000,0x0800F400)`，长度**61 KiB**；Keil ROM region固定base=`0x08000000`、length=`0x0000F400`，最高占用地址`<=0x0800F3FF`，exclusive end可等于`0x0800F400` | 三个1 KiB page均在官方64 KiB内且无重叠；旧62 KiB会覆盖SOC页 | ST C8 Flash容量与1 KiB page组织；用户设计决策 | Keil ROM/scatter限制61 KiB并对最高占用地址断言 | CLOSED BY DESIGN |
| E-02 | SOC记录60 s/5 min冲突且无独立区域 | 规格同时给出“变化≥1%或60 s”和“V1可5 min”，未分配独立页 | 独立使用 `0x0800F400` append-log；最快每5分钟保存一次；有空slot则append，满页才erase；record至少含magic/SOC permille/remaining capacity/sequence/CRC；启动扫描最新有效sequence | 避免与参数A/B共用及每60 s擦页造成耐久风险 | ST Flash耐久/页机制；用户持久化决策 | Phase 12 实现简单日志与wrap/损坏测试，不引入文件系统 | CLOSED BY DESIGN |
| E-03 | CAN安全命令可匹配broadcast | 规格命令检查允许 `DstAddr==BMS or broadcast`，未限制命令类型 | broadcast只允许heartbeat、discovery、telemetry/read-only query；CHG/DSG enable、fault reset、parameter write及任何安全状态变化必须unicast且`DstAddr==local`，并通过protocol/state/permission/fault检查 | 防止广播绕过节点身份与安全策略 | 项目安全协议决策 | Protocol层按命令分类；添加广播拒绝矩阵测试 | CLOSED BY DESIGN |
| E-04 | CAN application CRC参数和命名不完整 | 规格只写“application CRC8”，可能与BQ CRC混用 | `CRC8_CAN_App`使用CRC-8/ATM：poly=0x07、init=0x00、refin=false、refout=false、xorout=0x00；`"123456789"→0xF4`；与`CRC8_BQ76940`分离 | 两种协议输入拼接和CRC语义不同，模糊API易误用 | 用户协议决策；标准CRC test vector | 独立模块/API/golden vectors；禁止一个模糊`crc8()`承担二者 | CLOSED BY DESIGN |
| E-05 | 13S逻辑索引可能被实现为连续VC | 规格多处描述13S，但实现者可能把logical index直接等于VC寄存器序号 | 显式logical-cell-to-VC与logical-cell-to-CELLBAL表；跳过VC9、VC14；永久禁止隐式相等假设 | BQ76940减串连接不是VC1..VC13连续映射 | TI datasheet 13S配置表；SLUA810 | Phase 3/4加入13项mapping golden test | CLOSED BY DESIGN |
| E-06 | CAN RX ISR示例只取一帧 | 规格ISR示例单次`CAN_Receive`，FIFO深度3 | ISR循环drain FIFO至空，copy/enqueue，统计FOVR和queue-full，只做FromISR wake；业务解析在CANRxTask；所有hardware TX仅CANTxTask | 高负载或Flash停顿时单帧读取会增加溢出风险 | ST RM0008 bxCAN FIFO；RTOS ISR规则 | Phase 11 加burst/overflow/Flash并发压力测试 | CLOSED BY DESIGN |
| E-07 | BalanceTask无限等待I2C | 旧伪代码对`xI2CMutex`使用`portMAX_DELAY` | 采用bounded timeout；失败时保持/转入stop-all安全路径并记录诊断；不得同时长持xDataMutex和xI2CMutex | 避免优先级反转放大、健康监督假死和ALERT服务饥饿 | FreeRTOS mutex机制；项目锁策略 | Phase 5/10加入锁超时与恢复测试 | CLOSED BY DESIGN |
| E-08 | 参考参数可能被误当量产标定 | 规格包含4 mΩ、10 kΩ NTC、20 Ah与典型NMC OCV/阈值 | 这些只作为Reference Configuration；集中在Config层并标`calibration required`/`hardware assumption`；SOC不作安全保护输入 | 当前项目定位是可迁移参考固件，不具备目标电芯/BOM实测 | 用户项目定位；TI/ST器件边界 | 不阻塞软件实现；目标硬件替换必须经配置与正式规格修订 | HARDWARE VALIDATION REQUIRED |
| E-09 | 内部均衡的产品能力未验证 | 规格固定BQ内部CELLBAL，但TI对76930/40推荐外部均衡 | V1保持内部CELLBAL、一次最多一节、仅CHARGE/无fault/fresh/温度与电压门限满足，异常stop-all且避免非法adjacent；能力限制写明，外部均衡仅可作为V2 | V1目标是验证控制链而非量产热性能 | TI balancing guidance；用户V1架构决策 | 软件路径可实现；电流、约70% duty与温升进入硬件门禁 | HARDWARE VALIDATION REQUIRED |
| E-10 | BQ软件I2C恢复可能被描述为器件保证 | 9-clock+STOP是通用恢复策略，缺少目标板证据 | 保留line readback、timeout、stuck detection、bounded retry和9-clock+STOP，但明确不是TI专用保证；区分bus stuck、AFE无电和SHIP/POR | 通用I2C策略不能代替目标器件/板级恢复验证 | I2C通用机制；TI资料证据边界 | 软件可先实现并注入测试；实际恢复效果进入硬件门禁 | HARDWARE VALIDATION REQUIRED |

## 5. Hardware Validation Gate 清单

以下项目均不阻塞 Phase 1，但不得标为已验证：

| 项目 | 当前参考假设/软件处理 | 后续必须取得的证据 | 状态 |
|---|---|---|---|
| 目标PCB/BOM与器件变体 | STM32F103C8T6、BQ7694003、13S | 原理图、BOM/丝印、PCB连接与上电确认 | HARDWARE VALIDATION REQUIRED |
| Rsense | 4 mΩ reference value | 实值、公差、Kelvin连接、极性和电流标定 | HARDWARE VALIDATION REQUIRED |
| NTC/温度 | 10 kΩ reference model | NTC曲线、偏置/REGOUT公差和温箱标定 | HARDWARE VALIDATION REQUIRED |
| 电芯/SOC | NMC 4.2 V、20 Ah、典型OCV表 | 目标电芯容量、OCV-温度曲线和长期误差 | HARDWARE VALIDATION REQUIRED |
| MOS/power stage | 仅实现BQ低边CHG/DSG逻辑 | 极性、默认态、关断路径、故障能量与物理行为 | HARDWARE VALIDATION REQUIRED |
| ALERT/WAKE | PB1/EXTI1、PA8→TS1参考 | ALERT RC/电平/长高行为和TS1 wake波形 | HARDWARE VALIDATION REQUIRED |
| 软件I2C恢复 | bounded recovery与9-clock+STOP | 断线、SDA/SCL卡死、SHIP/POR和AFE无电场景 | HARDWARE VALIDATION REQUIRED |
| 内部均衡 | 一次一节、严格安全门禁 | 输入电阻、平衡电流/duty、邻接限制和温升 | HARDWARE VALIDATION REQUIRED |
| Flash掉电 | commit-last与append-log软件注入 | brownout/PVD/BOR、真实擦写停顿与掉电循环 | HARDWARE VALIDATION REQUIRED |
| CAN | 500 kbit/s/29-bit、软件协议测试 | 收发器、终端、bus-off、负载、EMC/ESD与真实总线 | HARDWARE VALIDATION REQUIRED |
| IWDG/clock | nominal约2 s与HSE fail-safe设计 | LSI实测窗口、HSE故障注入和复位行为 | HARDWARE VALIDATION REQUIRED |

## 6. 软件开放项结论

经对本轮决策、现规格、当前SPL/CMSIS/FreeRTOS文件和前序报告逐项复核：

- 芯片密度、64 KiB/20 KiB边界、ARMCC5/RVDS port方向、Flash三页地址、4-bit NVIC、`0x50` syscall门槛、EXTI=6、CAN=7、13S映射和CAN时序均不存在新的芯片事实冲突。
- 规格正文仍保留旧伪代码和旧62 KiB/60 s等描述，但本勘误表已对其做精确覆盖；这是后续实现必须遵循的修订，不再是未决设计。
- `H-06`、`H-08`等仍需要Phase中的map、high-water、单元/集成与压力验证；“尚未实现”不等于“阻止开始Phase 1”。
- 硬件标定和实板证据全部转入Hardware Validation Gate。

**Software blocker：None。**
