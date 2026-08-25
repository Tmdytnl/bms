# BMS V1 Hardware Bring-up Guide

状态：REAL_HW execution guide；当前各项均未由 M3 实测。

本指南把软件 RC 带到真实板的验证拆成可停、可观察的阶段。每一阶段只在前一阶段 PASS 后继续。仪器是推荐能力，不表示项目已拥有这些设备。真实原理图、BOM、test-point 名称、FET topology 与 protection component rating 必须在上电前补齐。

## 0. 通用安全规则

- 首次 bring-up 使用限流、隔离且能快速断电的电源；不要一开始连接高能量 13S 电池包。
- cell-input 仿真网络、AFE absolute maximum、输入顺序和 common-mode 必须依据最终 schematic/BQ datasheet，由具备实验室安全能力的人员操作。
- 示波器地线不可默认直接接 pack 任意节点；先确认仪器隔离/差分探头方案。
- 没有原理图/BOM/板级电气审查时，Stage 0 不能 PASS。
- 任一器件异常发热、供电过流、气味/声响、地参考不确定或探头可能短接，立即断电。

## Stage 0 — visual / power safety preparation

- Objective：证明板卡身份、供电路径、极性、测试点和能量限制已知，建立“可以安全上电”的前提。
- Prerequisite：最终 schematic、BOM、PCB revision、AFE variant、cell-input connection plan；确认 MCU=`STM32F103C8T6`、AFE=`BQ7694003`。
- Instrument：万用表；可调限流电源；可选热像仪/显微镜。
- Steps：目检焊接/极性/短路；断电量测主 rail 对地阻值；标出 3V3、BQ REGOUT、PACK/VC/SRP/SRN、ALERT、I2C、FET gate、CAN/UART test points；确认 Flash page/MCU part 与 project target；设置低电压低限流初始电源。
- Expected evidence：带 board revision 的照片、阻值/rail 预期表、上电 checklist、探头地参考说明。
- Failure clues：供电 rail 近似短路、器件方向不符、未知 jumper、MCU/AFE part 不符、无安全 cell-input fixture。
- Stop condition：任何板级身份/极性/absolute maximum 不明确；不要“试着上电看看”。
- PASS criterion：电气审查签名/记录完成，初始限流与断电方法明确。

## Stage 1 — MCU standalone

- Objective：在不依赖 AFE 的情况下证明 MCU 供电、reset、HSE/PLL、SWD 与 72 MHz clock tree。
- Prerequisite：Stage 0 PASS；AFE/cell/FET 能保持安全隔离或未使能。
- Instrument：限流电源、万用表、SWD debugger、示波器（可选 MCO/clock probe）。
- Steps：仅给 MCU 相关 rail 上电；观察 3V3 与 current；连接 SWD，读 device ID/Flash size；下载带符号 image；在 `main()`、`BSP_Clock_Verify()`、`BMS_SafeIdle()` 设断点；确认 startup 进入预期路径。
- Expected evidence：稳定 rail、正确 device ID、可重复 reset/halt/program；clock verifier 不报 mismatch。
- Failure clues：SWD 不连接、reset pin 被拉低、HSE 不起振、PLL/source/divider mismatch、下载越过 IROM boundary。
- Stop condition：rail 超限/过流、MCU part 不符、clock 异常导致 timer/I2C 时基不可信。
- PASS criterion：连续多次 power cycle 均能进入 `main()` 且 clock verify PASS；这仍不是 AFE/system PASS。

## Stage 2 — UART observability

- Objective：建立无需 debugger 停机的 runtime 可观测通道。
- Prerequisite：Stage 1 PASS；PA9/PA10 未与板级其他功能冲突；TTL 电平匹配。
- Instrument：USB-UART/logic analyzer/oscilloscope；terminal 115200 8N1。
- Steps：量测 PA9 idle level；连接 GND/RX/TX；运行到 `BSP_UART1_Init115200()`；确认 M3 `BMS1` 行（系统未通过 AFE 时可能停在 safe idle，因此可先用受控诊断 image 验证 UART binding）；解码 bitrate/frame。
- Expected evidence：PA9 waveform 约 115200 baud、8N1、无 framing error；正式 runtime 每秒一行，字段不乱码。
- Failure clues：TX/RX 交叉错误、3.3V/5V 不匹配、BRR/clock mismatch、terminal line ending/baud 错误。
- Stop condition：接口电平可能损坏 MCU；不要用 UART 命令绕过 startup/safety（正式软件也没有 command parser）。
- PASS criterion：波形与 terminal 均能稳定接收已知测试串；记录示波器截图和 terminal log。

## Stage 3 — software I2C waveform

- Objective：验证 PB8/PB9 open-drain、电阻、约 100 kHz timing、ACK sampling 与 bus recovery 电气行为。
- Prerequisite：Stage 1/2 PASS；AFE 可断开或已安全供电；确认 pull-up rail/value。
- Instrument：示波器或 logic analyzer；万用表。
- Steps：先观察 idle high；执行无害 probe；量测 START/STOP、SCL high/low、rise time、SDA setup/hold；验证 PB8 SCL/PB9 SDA；受控拉低 SDA 测 9-clock recovery，再释放；受控 clock stretching 验证 1000 µs timeout（若 fixture 支持）。
- Expected evidence：open-drain 无主动 drive-high；half cycle 名义 5 µs；idle/bus-free 能恢复；ACK/NACK 与 software status 对应。
- Failure clues：push-pull high、pull-up 太弱/太强、rise time 过长、pin swap、SCL/SDA stuck、timer 不是 1 MHz。
- Stop condition：过冲/电平超限、强驱动冲突、线路持续低且 recovery 原因未知。
- PASS criterion：保存 annotated waveform；频率、rise/fall、timeouts 落入 datasheet/board budget。

## Stage 4 — AFE communication

- Objective：证明 BQ7694003 wake、I2C address/CRC、基本 register read/write 与 startup safe-off/readback。
- Prerequisite：Stage 3 PASS；AFE rail、REGOUT、TS1/WAKE connection 与 cell fixture 安全。
- Instrument：示波器/logic analyzer、万用表、SWD/UART。
- Steps：观察 PA8 wake edge与 10 ms settle；执行 `BQ76940_Init()`/startup；读只读/known registers；核对 CRC bytes；观察 probe retries；记录 startup state/failure、transport status、failed register/readback；确认 SYS_CTRL2 CHG/DSG off 与 CELLBAL1..3 zero 的 register readback。
- Expected evidence：稳定 ACK+CRC；actual calibration registers可读；startup 按 bounded state前进。
- Failure clues：无 ACK、地址/CRC配置错误、REGOUT异常、WAKE无边沿、final STOP ambiguity、readback mismatch。
- Stop condition：AFE supply/cell pins异常、写入结果 ambiguous 且目标可能非幂等；不要 blind replay W1C/FET enable。
- PASS criterion：重复 power cycle/probe 均稳定；保留 bus trace、calibration raw values、startup log。

## Stage 5 — cell measurement

- Objective：验证 13S logical-to-VC mapping、每 cell 精度、cell-sum pack 与 BQ BAT diagnostic。
- Prerequisite：Stage 4 PASS；可控 13-channel cell simulator/resistor fixture；所有 VC 输入在 datasheet 范围。
- Instrument：precision DMM、cell simulator/regulated channels、logic analyzer。
- Steps：先用等电压输入；逐一改变 logical Cell1..13，确认 mapping 为 VC1..8、VC10..13、VC15；跨多个电压点记录 software mV vs DMM；核对 pack cell sum 与 BAT reading差异；检查 valid/in-range/age/sequence。
- Expected evidence：channel 无错位，calibration gain/offset 被应用，未使用 VC9/VC14；误差表含每点/每通道。
- Failure clues：两 cell 同时变化、mapping错位、offset/gain不一致、BAT/cell sum异常、stale latch不清。
- Stop condition：任一 cell input/common-mode 超限或 fixture ground 不安全。
- PASS criterion：达到项目负责人设定的真实硬件 accuracy acceptance；在定义前状态保持 `HARDWARE VALIDATION REQUIRED`。

## Stage 6 — NTC

- Objective：用真实 NTC/BOM 验证 TS1 raw -> resistance -> temperature，而非沿用模拟 B3950 结论。
- Prerequisite：Stage 4 PASS；真实 NTC part、bias resistor、tolerance、temperature range 已知；real policy artifact 准备。
- Instrument：temperature chamber/controlled bath、reference thermometer、DMM。
- Steps：在多个温点稳定后记录 TS1 raw、计算 resistance、UART temperature 与 reference；覆盖 charge/discharge cutoff附近；评估 self-heating、bias/REGOUT/tolerance；生成 verified integer table/domain并替换 simulation table，重新回归。
- Expected evidence：calibration curve、误差/重复性/滞后、out-of-range handling。
- Failure clues：table方向/单位错误、raw接近 rail、插值段错误、sensor位置导致热延迟。
- Stop condition：真实 part身份未知或温控/探头不能安全使用。
- PASS criterion：approved REAL_HW NTC artifact 与 accuracy report；M3 未提供此 PASS。

## Stage 7 — current / CC

- Objective：验证 4 mΩ假设是否匹配真实 Rsense、polarity、CC LSB、offset与 current accuracy。
- Prerequisite：Stage 4 PASS；可控双向电流路径、Rsense rating/Kelvin connection已审查；初始从低电流开始。
- Instrument：electronic load/source、precision shunt/DMM/current probe、oscilloscope。
- Steps：0 A 记录 offset；小幅 charge/discharge验证“+ charge / - discharge”；逐点记录 CC raw/current；验证 CC_READY cadence、queue/mailbox sequence、generation；建立 Rsense/temperature calibration并更新 artifact/policy。
- Expected evidence：polarity无歧义；current vs reference曲线、offset/drift、CC event timing。
- Failure clues：符号相反、Kelvin连接误差、queue overflow、CC_READY W1C后事件丢失、Rsense发热。
- Stop condition：电流路径/FET状态未知、shunt功耗超限、线缆/fixture发热。
- PASS criterion：REAL_HW Rsense/current mapping approved，包含误差与温漂；否则保持 deferred。

## Stage 8 — ALERT

- Objective：验证 PB1 ALERT 电平/边沿、EXTI1 ISR、semaphore、stuck-high drain/retry。
- Prerequisite：Stage 4/7 PASS；可安全产生 CC_READY 或受控 status event。
- Instrument：oscilloscope/logic analyzer、SWD/UART。
- Steps：触发单事件，关联 ALERT rising、EXTI pending、Protect wake、SYS_STAT read/W1C；制造多个同时 bits；在 I2C busy时观察 pending保留；让 ALERT high持续一段时间，确认无需新 edge仍重试；记录 ISR priority/raw value。
- Expected evidence：ISR只交接，task drain；ALERT低后服务完成；ambiguous W1C diagnostics可观察。
- Failure clues：edge丢失后永不服务、ISR内I2C、priority违反 `configMAX_SYSCALL_INTERRUPT_PRIORITY`、line polarity/drive错误。
- Stop condition：事件来源不可控或 W1C 可能清除未记录的新事件。
- PASS criterion：annotated ALERT/SCL/SDA trace + task log证明 bounded retry/level recovery。

## Stage 9 — HW OV/UV/OCD/SCD

- Objective：分别测得实际 threshold、delay、SYS_STAT/ALERT 与 register-level FET response。
- Prerequisite：Stages 5/7/8 PASS；production thresholds/rating/fixture已批准；MOS conduction测试仍留在 Stage 10。
- Instrument：cell simulator、bidirectional current source/load、scope、current probe、logic analyzer。
- Steps：每类 source 从安全方向缓慢逼近，量测触发点/延时；一次只测一个 source，再测 simultaneous events；记录 SYS_STAT、Protect source generation、active/latched、directional inhibit；降回 recovery区，验证 HW_OV/UV/OCD 的 request/ack 不仅依赖 SYS_STAT low；SCD只走 source-specific service reset policy。
- Expected evidence：实际 threshold/delay 分布、ALERT timing、software source/action与 policy一致。
- Failure clues：RSNS/code错误、directional action错、W1C被当作 physical recovery、SCD latch被 generic clear。
- Stop condition：输入/电流接近器件或 fixture rating、保护未按预期动作、physical state不可确认。
- PASS criterion：每个 source 独立报告 PASS；仿真值 4250/2800/10.5A/22.25A 不能直接成为硬件 PASS。

## Stage 10 — FET

- Objective：把 SYS_CTRL2 register evidence 与实际 CHG/DSG gate、MOS conduction、switch timing关联。
- Prerequisite：Stage 9 software/source behavior稳定；schematic明确 high/low-side topology、precharge/charger/load与安全断开。
- Instrument：isolated/differential oscilloscope、current probe、electronic load/source、DMM。
- Steps：先无功率路径测 gate；分别请求 CHG/DSG on/off；测 gate voltage、delay、rise/fall、body diode方向；注入 SW_OV/SW_UV/BOTH source并验证 directional conduction；注入 readback/transport failure观察 quarantine/safe-off attempt；量测 physical off leakage。
- Expected evidence：requested/effective/observed register与 gate/conduction timestamp correlation；FET timing表。
- Failure clues：register on但 gate/off、gate on但无 conduction、方向互换、shoot-through/oscillation、enable ambiguity重复。
- Stop condition：异常电流/温升、gate超额、MOS未按 source断开；立即去能量化。
- PASS criterion：CHG/DSG 各方向的物理开关与 fault response均满足批准 acceptance；M3 只证明 register logic。

## Stage 11 — IWDG

- Objective：验证实际 LSI 下 nominal 4000 ms 配置、arm条件、task stall reset与 reset cause。
- Prerequisite：七任务能集成运行；能安全恢复/记录 reset。
- Instrument：scope/logic analyzer、SWD、可观察 heartbeat GPIO或UART时间戳（如使用临时受控 test image）。
- Steps：正常运行确认所有 heartbeat first advance后才 arm；分别暂停 required tasks（Protect/Sample/State/SOC/Balance/CANTx/CANRx）；观察 RTOS_HEALTH、停止 feed、reset delay；重复不同温压；读 reset cause。
- Expected evidence：实际 timeout分布、no-feed到reset时间、正常运行无 spurious reset。
- Failure clues：其他 task/Flash path仍 feed、从未 arm、LSI偏差越界、paused task未被检测。
- Stop condition：watchdog reset会造成不安全 power state且尚无受控 fixture。
- PASS criterion：measured LSI/timing acceptance通过；软件 nominal值不替代测量。

## Stage 12 — CAN

- Objective：验证 PA11/PA12 bxCAN 与真实 transceiver/bus 500 kbit/s。
- Prerequisite：Stage 1/2 PASS；transceiver供电/STB/termination/ground设计确认。
- Instrument：CAN analyzer、scope/differential probe、可控 bus load。
- Steps：确认 bitrate/sample point；抓取 `0x180..0x185` 11-bit frames；发送 exact `0x280` valid/invalid/stale commands；测 filter不接收非目标ID；测试 no-mailbox、RX burst/FIFO overrun、bus-off/recovery；确认 CAN absence不改变 local safety authority。
- Expected evidence：frame decode与 source一致、waveform/termination/ACK正常、error counters与软件 diagnostics对应。
- Failure clues：ID格式错、bit timing/termination错误、ISR不drain、queue drop、service request绕过 source policy。
- Stop condition：bus电平/ground/reference不兼容或 transceiver发热。
- PASS criterion：protocol、physical layer、load/error recovery分别有 PASS 证据。

## Stage 13 — Flash brownout

- Objective：在 STM32 silicon 上证明 A/B commit-last 在真实 erase/program/brownout 下保留 newest-valid/previous-valid，并评估 realtime side effects。
- Prerequisite：integrated image稳定；可自动 power-cycle；Flash endurance budget与测试次数批准。
- Instrument：programmable supply/power switch、scope、SWD、UART logger、CAN/ALERT stimulus。
- Steps：先量测 page erase/halfword program；在 erase、body各 halfword、pre-commit、commit、post-commit阶段切电；每次 reboot记录选中 slot/sequence/SOC；注入 bit corruption；同时施加 CAN/ALERT load，观察 FIFO/health/IWDG；执行有限 endurance计划。
- Expected evidence：commit前掉电回旧 bank；完整 commit后取新 bank；corrupt newest回退旧 valid；无 page越界。
- Failure clues：先 erase active bank、partial record被接受、CRC/commit顺序错误、Flash stall使安全任务不可接受延迟。
- Stop condition：自动测试超出 endurance budget、brownout造成供电反灌/不可控 reset。
- PASS criterion：brownout matrix 与 endurance sampling满足批准标准；Simulator power-cut只作为前置证据。

## Stage 14 — integrated run

- Objective：在受控真实板上把 measurement/protection/FET/SOC/balance/CAN/Flash/health组合运行，验证长时一致性。
- Prerequisite：Stages 0–13 的相关功能逐项 PASS；未通过的功能必须物理隔离并明确不纳入 integrated PASS。
- Instrument：logger、CAN analyzer、scope、temperature/current/voltage references；安全供电与负载。
- Steps：执行 startup/power-cycle、standby、charge、discharge、温度/电压/电流边界、stale、XREADY、task stall、CAN burst、Flash save；长期记录 `BMS1`、CAN、reference instruments、reset cause；对 sequence/generation/revision/fault/inhibit/physical state做时间关联。
- Expected evidence：可追溯 run log、scenario matrix、无未解释 reset/overrun、software snapshot与物理行为一致。
- Failure clues：跨模块偶现 race、资源/stack/heap不足、长时 drift、power/thermal coupling、telemetry与物理状态矛盾。
- Stop condition：任何 safety invariant或物理限制被违反；停止对应能量路径并保留现场证据。
- PASS criterion：项目负责人批准的 REAL_HW validation matrix 全部必需项闭环。即使本阶段通过，也不能自动推出 production certification、EMI/ESD/thermal 合规。

## Bring-up 记录模板

每次执行至少记录：board/schematic revision、firmware commit、toolchain、policy artifact identity、instrument model/calibration date、wiring/photo、步骤、raw logs/waveforms、observed vs expected、PASS/FAIL、unresolved 与下一次变更。不要只记录“现象正常”。
