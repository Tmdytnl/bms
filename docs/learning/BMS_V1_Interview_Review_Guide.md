# BMS V1 Interview Review Guide

目的不是背稿，而是能从源码、证据和边界回答追问。每次复习都应能指出至少一个具体 struct/function/test。

## 1. 1-minute introduction

> 我完成了一个 13S NMC BMS 嵌入式工程，目标平台是 STM32F103C8T6 + BQ7694003，使用 ARMCC5 和 FreeRTOS。项目从 software I2C、BQ transport/measurement、ALERT/W1C 开始，形成七任务架构、软硬件故障 ownership、directional FET arbitration、XREADY phaseful recovery、generation-bound calibration、task health/IWDG、SOC、均衡、CAN、Flash A/B persistence 和 UART telemetry。核心设计是把 State 分类与 FET safety permission 分离，用 sequence/generation/revision 拒绝旧数据和 transaction race。Release Baseline 已通过 32 个确定性场景、3 个 targeted races、5 万次 stress、49 项 trust-chain tests，以及 ARMCC5 Clean/Rebuild 0 error/0 warning。

说完应准备指向：`app_rtos.c`, `bms_fet_manager.c`, `bms_recovery.c`, `verify_phase9.py`, Release Baseline。

## 2. 3-minute project walkthrough

1. Hardware/stack：STM32F103C8T6、BQ7694003、13S、FreeRTOS V11.1.0、software I2C PB8/PB9、ALERT PB1、CAN PA11/12、UART PA9/10。
2. Startup：policy/clock fail-closed；AFE pre-scheduler state machine先safe-off、配置/readback、calibration、800 ms settle，再创建RTOS对象和7 tasks。
3. Data：Sample每250 ms staged acquisition，`BMS_Data`原子publish，measurement带 `sample_sequence/afe_generation`，validity/freshness分开。
4. Safety：Protect拥有HW/AFE，State拥有SW/stale/health；各自发布directional inhibit。`BMS_Data` aggregate只diagnostic。
5. FET：State intent + 3 authoritative snapshots进入FET Manager；它是scheduler-era唯一SYS_CTRL2 writer，revision变化/ambiguous enable会safe-off/quarantine。
6. Recovery：Protect sole runtime XREADY W1C；StateTask service 10 phases，calibration provenance与first current-generation sample闭环。
7. Continuations：SOC整数积分；Balance sole CELLBAL writer；CAN显式wire encoding和source-specific service request；Flash A/B CRC32 commit-last；UART只读状态行。
8. Evidence：production-C Simulator、static verifier、target build 与 hardware matrix 形成分层且可追溯的验证链。

## 3. Architecture questions

### 为什么 State 不直接控制 FET？

State只知道operational classification与SW protection。HW/AFE fault由Protect拥有，XREADY readiness由Recovery拥有；如果State直接写FET，它要复制或反推其他owner状态，容易用stale aggregate。当前由State发布intent/State action，FET Manager直接消费Protect/State/Recovery snapshots并复核revisions。

### 为什么 `state == FAULT` 不能等于 both-off？

FAULT是显示/分类；例如SW_OV只应inhibit CHG，允许在其他条件满足时放电恢复。把FAULT强制both-off会丢失directional policy，也不能处理STANDBY中DATA_STALE等technical inhibit。真实权限由 `inhibit_chg_reasons/inhibit_dsg_reasons`决定。

### 为什么 Protect 与 State 分 owner？

Protect的input是SYS_STAT/ALERT/transport event，拥有W1C/source generation；State的input是coherent measurements/health，拥有debounce/hysteresis。分owner后HW active不会被State看到SYS_STAT low跨域清除，SW decision也能绑定sample identity。

### 为什么 Balance 必须 single writer？

Recovery直接写0和Balance写选择会形成interleaving；readback/generation无法归属。现在Recovery只发布inhibit并等待Balance current-generation confirmed all-off。

### Layering 有什么实际收益？

software I2C处理电气state；BQ transport处理address/CRC；measurement/control做纯换算/编码；application owner做policy/lifecycle。这样每层能用不同fixture测试，硬件替换也不会把产品策略塞进driver。

## 4. Driver questions

### Software I2C 最难的不是哪里？

不是shift 8 bits，而是open-drain release、clock stretching、wrap-safe timeout、failure后的STOP、read最后byte NACK、bus-stuck recovery与state consistency。说出 `SoftI2C_LineOps_t` 和 `SoftI2C_Status_t`。

### BQ CRC/STOP ambiguity如何处理？

CRC mismatch与NACK分别映射。若payload/CRC ACK但final STOP失败，返回 `BQ76940_STATUS_WRITE_FINALIZATION_AMBIGUOUS`；对W1C/enable这种non-idempotent write不能blind replay。

### 13S VC mapping是什么？

logical Cell1..13 -> VC1..8、VC10..13、VC15，跳过shorted VC9/VC14；一次读VC1_HI..VC15_LO 30-byte window，再显式映射。

### ALERT ISR 为什么不读BQ？

software I2C耗时且需要mutex，FromISR不适用；ISR只clear EXTI/give semaphore，ProtectTask做bounded drain和level retry。

### W1C 与普通 RMW 的区别？

写1清对应bit，写0不影响；read到的bit在write前可能变化，且一次write可能clear多个event。必须source owner决定clear mask，逐bit确认处理成功，处理ambiguous finalization。

### CC current如何换算？

`I[mA] = polarity * raw * 8440[nV/LSB] / Rsense[µΩ]`，使用64-bit intermediate；当前4 mΩ/+charge是SIM假设，必须实测。

## 5. RTOS questions

### 为什么是七tasks？

按latency/ownership分：Protect最高优先响应ALERT；Sample固定采集；State控制/health；SOC与Balance有独立低频state；CAN TX/RX分硬件出口和输入validation。不是“功能多就多task”，而是writer和blocking pattern不同。

### 任务优先级和period？

优先级为 5/4/3/3/2/2/2：Protect bounded ≤100 ms；Sample 250 ms；
State 最大有界等待 100 ms 且支持 urgent notification；SOC/Balance 1000 ms；
CANTx 10 ms service + 100 ms publish；CANRx bounded ≤100 ms。

### 两个mutex如何使用？

`xI2CMutex`只包bounded BQ transaction；`xDataMutex`只包snapshot copy/publish；禁止嵌套。Flash不持两者。

### 为什么snapshot后还需要revision？

snapshot只保证读取时coherent，计算/transaction期间owner仍可更新。revision/identity在commit/readback前后拒绝TOCTOU stale work。

### Heartbeat bit为什么不好？

monitor snapshot/clear与task set会race。每task monotonic generation无clear，每task sole writer；State比较是否advance。

### Stack evidence怎么回答？

有ARMCC5 static callgraph与配置，例如State 1104 B max depth / 1536 B allocation；但没有REAL target high-water。诚实结论是STATIC/CONFIGURATION REVIEW ONLY，需 `uxTaskGetStackHighWaterMark` 实测。

## 6. Safety questions

### Directional inhibit是什么？

每个source同时有fault identity和CHG/DSG action。例如OV->CHG，UV/OCD->DSG，stale/XREADY/health->BOTH。unknown active source默认BOTH。

### XREADY为什么全程BOTH inhibit？

它表示AFE epoch/config/calibration/FET寄存器可能变化；直到post-clear全配置、calibration handoff和first valid current-generation sample完成，都没有完整technical readiness。

### FET Manager的quarantine是什么？

enable write ambiguous或readback mismatch时，不能说enable成功，也不能安全blind replay；transaction state变QUARANTINED，deny enable并允许safe-off attempt。readback只证明register bits，不证明MOS。

### HW fault为何不能SYS_STAT low自动恢复？

W1C可能已经clear event latch，但电压/电流condition仍在。State用fresh measurement qualify，Protect再fresh read status和核对source generation/request ID后只清private active。

### Generic clear-all有什么问题？

不同source的reset evidence不同；clear-all会绕开SCD/OVRD/XREADY/COMM policy与owner。项目只允许source-specific, evidence-gated request。

## 7. Data consistency questions

### sequence与generation分别是什么？

sequence区分accepted publications；generation区分AFE/XREADY epoch。新sequence不一定是新generation；新generation使旧calibration/sample/recovery evidence失效。

### compare-and-publish的时间线？

State读sample N并计算；Sample发布N+1；State commit前读identity，发现不等于N，拒绝旧decision并重算。

### old evidence问题还有哪些？

FET transaction捕获旧revisions、HW recovery request引用旧source generation、calibration cache跨XREADY、CC mailbox跨epoch、Balance selection对旧sample/recovery revision。

### timestamp不能代替sequence吗？

timestamp可能相同/回绕/来自不同group；sequence由accepted atomic publication推进，明确表达software identity。时间仍用于freshness，两者职责不同。

## 8. Flash questions

### A/B流程？

boot读两槽full decode，选wrap-safe newest valid。save对inactive bank：erase -> 32-byte body halfword program/readback -> commit marker last -> full verify；完成前旧bank authoritative。

### CRC为什么不够？

CRC不能保证“这次transaction被正式commit”，也不保证旧good copy仍在。commit marker和A/B分别解决这两个问题。

### 为什么不能raw-copy struct？

padding、alignment、endianness、compiler model不稳定。当前34-byte explicit encoding固定wire/persistent layout。

### Simulator power-cut证明到哪？

证明每个program ordinal失败后boot selection/previous-bank preservation的软件算法；不证明silicon brownout、Flash stall、endurance。

## 9. CAN questions

### protocol/core与target binding怎么分？

core 负责 frame explicit encoding/validation/source-specific request；BSP 负责 PA11/12、bit timing/filter/FIFO/mailbox/bus-off；transceiver/bus 由独立 interface contract 和 integration record 覆盖。

### 500 kbit/s怎么得到？

PCLK1 36 MHz，prescaler 9，1+BS1 6+BS2 1=8 tq：36M/(9*8)=500k，sample point (1+6)/8=87.5%。

### CAN command能控制FET吗？

不能。exact 0x280 command只生成有identity/expiry的source-specific reset request，Protect按source policy评估；CAN无直接FET/fault/IWDG authority。

### CAN absent会怎样？

diagnostic counters/retry，SIM policy下不改变local protection；但physical network功能当然是unavailable。

## 10. Interviewer challenge questions

### “你如何证明项目完成？”

回答：从 Git/source 开始，依次给出冻结 ownership、32 个确定性场景、3 个目标竞态、5 万次压力测试、49 项 trust-chain、ARMCC5 0/0、资源基线和相同 HEX；每个结论都能定位到源码、测试入口和 release 工件。

### “Simulator 在证据链中承担什么角色？”

它运行 ARMCC5 编译的 production C，用于验证 decision、event sequencing、injected races、A/B power-cut model 与 stress invariants；静态 verifier、target build、map/callgraph、HEX 和接口矩阵提供其他相互独立的证据维度。

### “如何说明 Release Baseline 的工程成熟度？”

回答完整功能集合、单写者 ownership、身份绑定竞态防护、失败语义、可重复回归、资源约束、工具链与工件一致性；不使用模糊的“功能很多”替代可检查证据。

### “哪些参数是假设值？”

4 mΩ、current polarity、NTC B3950 table、OV/UV/OCD/SCD、software thresholds/temp、health/IWDG nominal、SOC/balance policy 属于 `SIM-HW-POLICY-V1`；MCU/AFE/pins/clock/13S 属于 source-fixed target identity。

### “如何复现硬件接口集成？”

按 Hardware Integration Guide 绑定 schematic/BOM/board revision，从 visual/power safety、MCU、UART、software-I2C waveform、AFE communication 逐级记录；高能量 FET/SCD 项使用受控 fixture 和明确 stop condition。

### “这个项目你最能证明的亮点？”

选择一个深入讲：XREADY 10-phase recovery；FET revision race；State compare-and-publish；Flash commit-last。必须画出owner/identity/failure semantics和对应test，而不是只说“用了FreeRTOS”。

### “你会怎么改进？”

改进遵循受控演进：持续收集 heap minimum/stack high-water、trace timing 以及 I2C/ALERT/FET/CAN/Flash 接口记录，把 approved artifacts 绑定为可追溯 policy，并扩展 CI 或等价 target runner；frozen safety architecture 保持不变。

## 11. 自测清单

面试前不看文档回答：

- 7 tasks 的period/priority/owner；
- 2 mutex/3 queues/1 semaphore分别谁用；
- 3个targeted race的时间线；
- XREADY 10 phases；
- 20 fault IDs中至少解释 directional examples；
- current换算与4 mΩ证据等级；
- A/B 34-byte record/commit offset思想；
- Code/RO/RW/ZI与“为什么不能20KiB-RWZI当stack margin”；
- 三条hardware evidence你最先收什么。
