# BMS V1 Software Implementation Gate

- 项目：BMS V1 Reference Firmware Project
- 判定日期：2026-08-13
- 当前阶段：最终软件基线已收敛；Phase 1 尚未开始
- 规格基线：`docs/spec/BMS_V1_统一项目方案_软件设计规格.md` SHA-256 `7e71125d6acdad5ba3d8203a5bd9cff51168051c895cfd31b76f663266576472`
- 强制勘误：`deliverables/review/BMS_V1_规格勘误表.md`（以最终文件SHA-256为准）
- 前序审查：`deliverables/review/BMS_V1_开发准备报告.md` SHA-256 `22cac90f445f8c420f27699c2a03ce50977cceb42209cf0b8d87457db788336c`

## 1. Gate定义

### Software Implementation Gate

判断开始Phase 1所需的软件选择、芯片事实、工具链/port、内存边界、中断/并发规则、协议规则和安全架构是否已形成唯一、可实施、可测试的基线。

Gate通过不表示代码已实现，不表示当前BMS工程已构建，也不表示目标板已运行。Phase 1将创建可复现Keil工程、BMS专用配置、公共模型和构建边界，后续Phase逐项实现并验证。

### Hardware Validation Gate

判断参考假设在目标PCB/BOM/电芯和真实环境中是否成立，包括标定、波形、热、功率级、总线、掉电、EMC/ESD与安全行为。该Gate当前未通过，但**不阻塞Phase 1或参考软件架构实现**。所有未取得的硬件结果必须标记`hardware assumption`、`calibration TODO`或`hardware validation TODO`，不得编造。

Phase 1只受Software Implementation Gate阻塞。

## 2. 证据与适用边界

用户确认的旧STM32F103C8T6工程使用：

- Keil MDK5；
- ARM Compiler V5.06 update 7 build 960 / ARMCC5；
- `startup_stm32f10x_md.s`、当前类型CMSIS/SPL；
- FreeRTOS `tasks.c`、`queue.c`、`list.c`、`event_groups.c`、`timers.c`、`stream_buffer.c`、`croutine.c`、`heap_4.c`、`portable/RVDS/ARM_CM3/port.c`；
- 链接结果`0 Error(s), 0 Warning(s)`。

旧工程链接摘要为：

| Code | RO-data | RW-data | ZI-data | RW+ZI |
|---:|---:|---:|---:|---:|
| 30,544 | 392 | 844 | 19,308 | 20,152 bytes |

这是一项**用户确认的外部真实构建证据**，足以关闭“工具链/port选择未知”和“该基础组合是否曾可编译”的问题。当前仓库没有对应旧`.uvprojx`、map或完整日志，本机也未发现ARMCC5，因此本轮不能复现该构建。

该证据不证明：

- 当前仓库已是可构建BMS工程；
- 新BMS `FreeRTOSConfig.h`、Keil target或ROM/RAM布局已经验证；
- 8 KiB heap、七任务栈和队列已经满足20 KiB SRAM；
- 当前BMS代码或目标硬件已运行。

旧工程中的MPU6050、NRF24L01、Motor、PID、IMU、Flow、Attitude和旧任务等业务代码禁止继承。Phase 1建立新的BMS专用工程并首次产生当前项目的可复现build/map证据。

## 3. 锁定的软件开发基线

### 3.1 MCU、时钟与工具链

| 项 | 最终基线 |
|---|---|
| MCU | STM32F103C8T6，Medium Density |
| Memory | Flash 64 KiB；SRAM 20 KiB |
| Defines | `STM32F10X_MD`、`USE_STDPERIPH_DRIVER` |
| Startup | 只选`startup_stm32f10x_md.s` |
| Clock | HSE 8 MHz；SYSCLK/HCLK 72 MHz；PCLK1 36 MHz；PCLK2 72 MHz |
| IDE/compiler | Keil MDK5；ARMCC5 V5.06 update 7 build 960 |
| MCU library | STM32F10x SPL V3.5.0；当前仓库CMSIS |
| 禁止迁移 | GCC、ArmClang6、HAL、CubeMX、CMSIS-RTOS wrapper或其他FreeRTOS port，除非将来有不可解决问题的真实证据 |

HSE/PLL/clock switch均必须有bounded timeout和readback；72 MHz tree未建立时进入safe startup failure，不得正常启动BMS RUN、CAN或错误时基的RTOS/TIM。

### 3.2 FreeRTOS与内存

| 项 | 最终基线 |
|---|---|
| Kernel | 当前仓库FreeRTOS V11.1.0，不升级 |
| Port | `portable/RVDS/ARM_CM3` |
| Heap | `heap_4.c`；initial target约8 KiB，非永久常量 |
| Scheduler | preemptive；1 ms tick；native FreeRTOS API |
| Tasks | Protect/Sample/State/SOC/Balance/CAN TX/CAN RX，共7个 |
| Priorities | 5/4/3/3/2/2/2；`configMAX_PRIORITIES=8` |
| Diagnostics | `configASSERT`；`configCHECK_FOR_STACK_OVERFLOW=2`；assert/overflow hook |
| Objects | `xI2CMutex`、`xDataMutex`、`xAfeAlertSem`、`xCanTxQueue`、`xCanRxQueue`、`xCcSampleQueue`、`xSysEvents` |

8 KiB仅是启动预算。必须用Keil map、RW/ZI、startup默认MSP stack `0x400`、C library heap `0x200`、queue/semaphore/event group、全局快照、七栈high-water和最坏调用深度复核，并保留明确SRAM裕量。startup保留量可在Keil工程内按证据调整，但不能遗漏；不得继承旧17 KiB FreeRTOS heap。

锁规则：不长期同时持有data和I2C mutex；先复制snapshot并释放data lock，再做bounded I2C；BalanceTask不能无限等待I2C；Flash erase/program期间不持有I2C/Data mutex。

### 3.3 NVIC与ISR

| 项 | 最终值 |
|---|---:|
| `configPRIO_BITS` | 4 |
| Priority grouping | `NVIC_PriorityGroup_4` |
| Library lowest | 15 |
| Library max syscall | 5 |
| Kernel raw priority | `0xF0` |
| Max syscall raw priority | `0x50` |
| BQ ALERT / EXTI1 | logical 6 |
| CAN RX0 | logical 7 |

调用FreeRTOS FromISR API的ISR必须满足logical priority number≥5。SVC/PendSV/SysTick由FreeRTOS port唯一提供；PendSV/SysTick为最低优先级；禁止SPL模板或BSP重复定义/重配SysTick。

EXTI ISR只检查/清STM32 pending、投递semaphore/notification和`portYIELD_FROM_ISR`。CAN ISR只drain FIFO、copy、enqueue、statistics和FromISR wake。二者都禁止I2C、Flash、printf、业务解析、FET控制和复杂计算。

### 3.4 BQ76940、ALERT与FET

- 单BQ7694003；13S；CRC variant；7-bit address `0x08`；REGOUT 3.3 V reference configuration。
- PB8/PB9 GPIO开漏软件I2C；timeout、line readback、stuck detect、bounded retry和通用9-clock+STOP recovery；不改硬件I2C。
- 13S logical-cell/VC和CELLBAL均用显式mapping table，跳过VC9、VC14，必须有13项golden mapping test。
- PB1/EXTI1 active-high ALERT不是纯fault；ProtectTask对CC_READY、XREADY、OVRD_ALERT、UV、OV、SCD、OCD逐bit处理，禁止互斥`else-if`。
- ALERT采用drain+retry；失败/锁超时/CRC错误/line仍高均保留pending，不等待一个可能永不再来的rising edge。
- CC_READY仅在CC原子读取、CRC/事务和本次样本成功进入消费路径后W1C。`xCcSampleQueue`满时，专用queue wrapper在受保护操作中丢弃恰好一个最旧样本并入队本次最新样本，同时递增overflow/missed计数并置诊断；只有确认本次最新样本已入队才清位。替换/入队失败则不清并进入bounded retry/fault path。
- XREADY锁存Fault并禁止自动开管；稳定等待、BQ reinit、calibration/保护配置/组状态验证成功后最后清除，再重新经过permission。
- OVRD_ALERT具有独立fault、safe action、diagnostic、CAN report、recovery和W1C。
- CHG/DSG只有single writer。所有模块只产生request/permission/inhibit；写前查SYS_STAT，按当前条件合成完整desired bits，写后readback并复查fault，禁止陈旧RMW自动重开FET。

### 3.5 采样、SOC与均衡

- SampleTask约250 ms；cell/CC约250 ms；TS约每8次读取一次（约2 s）。
- 所有测量带timestamp、valid、range和age/freshness；三组cell测量不是严格同步。
- SOC使用CC库仑积分+启动OCV+静置缓纠偏；NMC 4.2 V、20 Ah和典型OCV只作reference/calibration-required；SOC不作为安全保护依据，也不宣称量产Fuel Gauge精度。
- V1保持BQ内部CELLBAL；仅CHARGE、无fault、fresh、温度/最低电压/delta合格，一次最多一节，避免非法adjacent，任一异常stop-all。能力和温升由物理接口门禁证据承接，不自动引入外部均衡。

### 3.6 CAN

- bxCAN，PA11 RX/PA12 TX，PCLK1=36 MHz。
- SPL timing：Prescaler=9、BS1=6 tq、BS2=1 tq、SJW=1 tq，总8 tq，500 kbit/s，87.5% sample point。
- 自定义29-bit Extended ID；所有hardware transmit仅由CANTxTask完成。
- RX ISR drain 3-deep FIFO至空并统计FOVR/queue-full；协议逻辑在CANRxTask。
- broadcast只允许heartbeat、discovery、telemetry/read-only query；CHG/DSG enable、fault reset、parameter write和任何安全状态变化必须unicast，验证`DstAddr==local`并经过protocol/state/permission/fault检查。
- `CRC8_CAN_App`固定CRC-8/ATM：poly=0x07、init=0x00、refin=false、refout=false、xorout=0x00；`"123456789"→0xF4`。与`CRC8_BQ76940`分离命名和测试。
- 未被勘误覆盖的29-bit ID位域、地址、MsgType、payload、ACK和sequence/replay规则继续沿用规格§§35–38；本Gate不另造第二套线协议。

### 3.7 Flash、参数与SOC日志

| 区域 | 地址范围 | 大小 |
|---|---|---:|
| Application | `[0x08000000, 0x0800F400)` | 61 KiB |
| SOC Log | `0x0800F400..0x0800F7FF` | 1 KiB |
| Parameter A | `0x0800F800..0x0800FBFF` | 1 KiB |
| Parameter B | `0x0800FC00..0x0800FFFF` | 1 KiB |

Keil ROM/scatter固定application region base=`0x08000000`、length=`0x0000F400`。最高占用地址必须`<=0x0800F3FF`；若“image end”采用exclusive-end语义则可等于`0x0800F400`。旧62 KiB应用假设作废。

Parameter A/B使用magic/version/length/`uint32_t sequence`/payload/CRC/16-bit commit marker；永不先擦唯一有效bank。命令先进入staging并验证；如需修改BQ保护配置，在保留旧active可回滚的事务中试应用并readback。写inactive全部字段时commit保持erased，readback/CRC后以最后一次halfword program写commit，再次验证；之后只更新RAM cached-active，**不另写持久化active flag**。commit前任何失败都恢复旧BQ且不切RAM。启动验证commit/magic/version/length/CRC，并用`(int32_t)(a-b)>0`（有效序距<`2^31`）选择newest；新bank完整commit前旧bank不擦，只有下一次更新时才成为inactive。both-invalid进入配置故障/安全禁止和明确的reference fallback流程。

SOC使用独立1 KiB append-log，最快每5分钟保存一次；有空slot即append，满页才erase；启动扫描最新有效sequence。不实现复杂文件系统，不与参数页高频共用。

Flash操作必须考虑单Flash停顿、最大erase时间、CAN FIFO、ALERT、IWDG和mutex约束；软件故障注入属于实施验证，真实brownout属于硬件门禁。

### 3.8 IWDG、层次与SPL选择

- IWDG nominal目标约2 s；不新增第8任务，既有StateTask是system-health supervisor，也是唯一允许调用`BSP_IWDG_Feed`/`IWDG_ReloadCounter`的应用任务。每个监督窗口检查其他required task heartbeat；StateTask自身健康由循环进度/deadline直接判定，不依赖本窗口末尾才设置的自heartbeat。实现阶段按LSI min/typ/max计算shortest/nominal/longest并选择PR/RLR，不声称精确2.000 s。
- 固定`App/ Driver/ Protocol/ Service/ Config/ User/`层次；下层不得include App；Protocol不得直接访问BQ/Flash/FET；避免循环依赖。
- 工程只加入实际所需SPL：`misc`、RCC、GPIO、EXTI、TIM、CAN、USART、FLASH、IWDG；实际使用PWR时再加入；不加入硬件I2C和无关SPL模块。
- 未来生成的固件、工程、scatter和测试统一位于`firmware/`，不写入`docs/`或`deliverables/`。

## 4. Software closure matrix

| ID | Gate状态 | 已关闭的设计决定 | 后续实施/验证义务（不阻止开始Phase 1） |
|---|---|---|---|
| C-01 | CLOSED BY DESIGN | max priorities=8，七任务优先级固定 | 新config、assert和调度测试 |
| C-02 | CLOSED BY DESIGN | max syscall=5/`0x50`，EXTI=6，CAN=7，Group_4 | NVIC读回、FromISR assert与ISR测试 |
| H-01 | CLOSED BY DESIGN | OVRD_ALERT完整处理链 | fault/recovery/W1C测试 |
| H-02 | CLOSED BY DESIGN | CC成功入消费路径后才W1C；满队列原子丢最旧、保最新 | read/CRC/queue-full/并发consumer注入测试 |
| H-03 | CLOSED BY DESIGN | XREADY完整reinit后最后清 | 恢复状态机和失败测试 |
| H-04 | CLOSED BY DESIGN | FET single-writer、当前permission合成 | RMW竞态/readback/fault测试 |
| H-05 | CLOSED BY DESIGN | ALERT drain+retry/pending | 长高、锁超时、叠加bit测试 |
| H-06 | MITIGATED | Flash门禁、无锁进入、实时预算 | 集成压力与目标板停顿测量 |
| H-07 | CLOSED BY DESIGN | commit-last、无持久化active flag、启动按valid+sequence选择 | 掉电/wrap/both-invalid软件注入；brownout硬件测试 |
| H-08 | MITIGATED | 初始8 KiB heap并禁止继承旧17 KiB | map/RW/ZI/MSP/high-water与安全裕量 |
| H-09 | CLOSED BY DESIGN | assert和stack check=2 | hooks与故障触发测试 |
| H-10 | CLOSED | Keil MDK5+ARMCC5+RVDS/ARM_CM3锁定 | Phase 1建立当前可复现工程 |
| H-11 | CLOSED BY DESIGN | nominal约2 s；StateTask是唯一喂狗者且无自heartbeat依赖 | 官方公式选PR/RLR并计算三点窗口 |
| H-12 | CLOSED BY DESIGN | bounded clock startup/fail-safe | 时钟读回与故障注入 |
| H-13 | PHYSICAL INTERFACE EVIDENCE | 软件退化行为和物理保证边界明确 | power-stage/默认态/失联行为观测 |

“CLOSED BY DESIGN”表示选择与约束已唯一确定，不等于相应代码已写完；这些实现与测试正是Phase 1–12的工作内容。

## 5. Hardware Validation Gate

当前状态：**NOT PASSED / DEFERRED**。以下事项均不阻止Phase 1：

- 真实PCB、BOM、BQ7694003丝印和13S连接；
- 4 mΩ Rsense实值/公差/极性；
- 10 kΩ NTC真实曲线与温度标定；
- 目标NMC电芯容量与OCV-温度曲线；
- MOS/power-stage特性、默认安全态和I2C失效后的物理关断边界；
- ALERT RC、电平、持续高行为及PA8→TS1 wake波形；
- software-I2C在卡线、AFE无电和SHIP/POR下的真实恢复；
- 内部均衡电流、duty、相邻约束和温升；
- Flash brownout/PVD/BOR、真实erase/program停顿与耐久；
- CAN收发器、终端、bus-off、真实负载、EMC/ESD；
- LSI/IWDG实际窗口、HSE故障注入、系统复位行为；
- 量产Fuel Gauge、EMC/ESD、安规与安全认证。

参考实现必须为这些项目保留可配置接口、诊断和测试点，并明确标注assumption/TODO；禁止把参考值或仿真结果写成实测。

## 6. 剩余软件矛盾复核

| 审查项 | 结论 |
|---|---|
| 芯片/内存事实 | F103C8 64 KiB Flash、20 KiB SRAM与Medium Density一致；无新冲突 |
| ARMCC5/port | 仓库RVDS/ARM_CM3使用ARMCC5语法，用户旧工程构建证据与之匹配；无port选择blocker |
| Flash overlap | 三个保留页连续且不重叠；应用区精确为61 KiB；无地址blocker |
| FreeRTOS/NVIC | 4 bits、Group_4、`0xF0/0x50`、EXTI6/CAN7满足port规则；无中断blocker |
| BQ寄存器/13S | 目标03、地址/CRC、VC9/VC14跳过与官方资料一致；无寄存器/mapping blocker |
| CAN timing/CRC | 36 MHz/(9×8)=500 kbit/s，87.5%；应用CRC参数和安全广播规则已唯一确定 |
| SRAM | 8 KiB是可开始实施的受控目标，必须以当前工程map/high-water收敛；不是Phase 1前置blocker |
| IWDG | nominal目标和计算方法确定，具体PR/RLR属于实现任务；不是Phase 1前置blocker |
| 规格旧正文 | 旧RMW、Protect伪码、62 KiB、60 s、broadcast等均由强制勘误精确覆盖；无未决设计 |

真正阻止Phase 1的软件blocker：**无**。

## 7. Gate判定

本判定仅授权在用户明确命令后开始Phase 1，不授权本轮创建完整工程或业务代码。当前尚未进入Phase 1。

**SOFTWARE IMPLEMENTATION GATE: PASS**

**READY FOR PHASE 1**
