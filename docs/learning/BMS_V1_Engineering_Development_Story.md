# BMS V1 Engineering Development Story

这不是公司事故复盘，也不是量产经历包装。下面的问题来自本学习项目的代码 review、生产 C 测试、并发分析与 Keil Simulator fault injection。主线是：课堂上的“能读寄存器、能建 task”如何变成可解释、可恢复、能拒绝旧证据的软件工程。

## Story 1 — Software I2C：能拉高拉低不等于有一个 driver

1. 遇到了什么问题：BQ7694003 transport 需要 START/STOP、repeated START、ACK/NACK、clock stretch、超时与 CRC。用几个 GPIO helper拼 transaction 很快会让每个 BQ API各自处理错误。
2. 最直觉的方案是什么：在 `BQ76940_ReadByte()` 里直接 bit-bang PB8/PB9，固定 delay 后读取 SDA。
3. 为什么不够：开漏“高”应是 release，不是 drive-high；SCL可能被拉低；失败后仍要尝试 STOP；read最后一个byte要NACK；SDA stuck需要有界恢复。逻辑散在上层会让错误语义和bus state不一致。
4. 怎么定位：Phase 2 production-C tests把line actions/time source替换成fixture，逐个检查ACK、timeout、repeated start、STOP和9-clock recovery；code review把BSP pin、电气状态机和BQ transport分层。
5. 最终方案：`bsp_gpio/timer` 提供line/time primitives，`soft_i2c` 拥有bus state machine，`bq76940` 只处理address/register/CRC与transport status。
6. 解决了什么：BQ上层能区分 NACK、timeout、state error、recovery failed，且后续Protect/Sample只需在 `xI2CMutex` 下调用同一driver contract。
7. 学到了什么：driver boundary不是文件分类，而是把电气时序、状态与错误恢复集中到一个可测试的owner。

## Story 2 — ALERT / W1C：`read SYS_STAT -> clear` 的 ownership

1. 问题：SYS_STAT bit是W1C，ALERT由多个事件OR起来；ISR、Protect、startup/recovery如果都“顺手清”，事件身份和恢复证据会丢失。
2. 直觉方案：ISR读SYS_STAT，随后把读到的byte原样写回；或者每个发现bit的模块各自clear。
3. 为什么不够：ISR不能做I2C；W1C read/modify/write期间可能出现新event；write的payload/CRC ACK但final STOP失败时commit不明确；cleared bit只说明event latch状态，不证明cell/current物理条件恢复。
4. 定位：Phase 7 review构造startup-high ALERT、stuck-high、simultaneous bits、I2C失败、ambiguous finalization与queue-full场景，发现“等下一次edge”和blind replay都不成立。
5. 最终方案：EXTI1 ISR只give semaphore；ProtectTask独立处理每个bit、保留pending并做bounded level retry；Protect是runtime SYS_STAT W1C owner，startup只保留严格XREADY exception；ambiguous bit进入quarantine直到observed-low。
6. 解决：事件capture、action lifecycle与physical recovery被分开；lost edge不会永久饿死服务，non-idempotent write不被blind replay。
7. 学到：W1C首先是并发/所有权问题，其次才是寄存器操作。

## Story 3 — CC_READY：event 不是 measurement identity

1. 问题：CC_READY高只说明有一个coulomb reading event；如果queue满、XREADY跨epoch或clear时event coalesced，不能说“这就是当前sample”。
2. 直觉方案：看到CC_READY就读CC、clear bit，SampleTask/SOCTask需要时各读一次current。
3. 为什么不够：两个reader会消费不同硬件时刻；clear早于queue acceptance会丢sample；queue满若丢newest会让控制一直使用旧current；XREADY后旧CC不能绑定新AFE epoch。
4. 定位：production-C fixture注入queue full、enqueue失败、同帧XREADY+CC、generation change与W1C ambiguity，检查sample count与clear mask。
5. 最终方案：Protect是唯一CC event reader/producer；newest成功进 `xCcSampleQueue` 后才W1C，full时丢一个oldest并保newest；同时发布带sequence/generation的latest mailbox给Sample，SOC独占queue。
6. 解决：SOC和measurement对current来源可解释；queue gap与event ambiguity成为diagnostic，而不是被假装成连续积分。
7. 学到：事件、值、时间戳、sequence与generation是不同维度；“发生过”不是“这是当前证据”。

## Story 4 — XREADY：clear + reconfigure 为什么不够

1. 问题：runtime XREADY意味着AFE状态可能重置且BQ自动clear CHG/DSG；旧configuration/calibration/measurement都可疑。
2. 直觉方案：Protect看到XREADY后clear bit，重写几个register，然后恢复原FET request。
3. 为什么不够：谁clear会race；clear前需要FET/balance safe readback；clear后配置证据必须重建；settle与status verify不能在一个长I2C critical section里完成；new XREADY可在恢复中到达。
4. 定位：架构red-team把过程分解后发现“inactive bit”无法证明post-clear配置、calibration与first sample；Simulator又注入handoff generation race和recovery期间new event。
5. 最终方案：StateTask service 10-phase coordinator；Protect sole runtime W1C；每步至多一个I2C transaction；generation/revision串起authorization、ack、config、settle、verify、handoff与first valid sample。
6. 解决：任何阶段失败/新generation都会invalidate旧证据并保持BOTH inhibit，恢复不再是一个无法审查的bool callback。
7. 学到：recovery是protocol，不是error handler；phase本身就是可观察、可测试的工程资产。

## Story 5 — Calibration generation：cache 为什么不能跨epoch

1. 问题：startup读到的gain/offset放在cache里，看起来可以在XREADY后继续使用。
2. 直觉方案：只要 `calibration.valid=true`，recovery后把同一struct重新装回Sample。
3. 为什么不够：XREADY代表AFE epoch改变；旧valid只描述旧设备状态。若没有provenance，任意task都能把old evidence重新解释成new evidence。
4. 定位：review追问“valid由谁、对哪个generation、在哪次post-clear verify后产生”；generation-wrap与handoff-race tests在publication前改变XREADY。
5. 最终方案：runtime evidence必须含 `xready_generation`, `recovery_revision`, `post_clear_verified`, calibration；只由Recovery Coordinator构造，Sample admission复核current Protect state/phase/revision。
6. 解决：旧cache不能绕过recovery；configuration在handoff前后改变时publication被拒绝。
7. 学到：布尔valid通常缺少上下文；安全证据需要回答“对哪个世界状态有效”。

## Story 6 — Validity vs Freshness：合法数值也可能危险

1. 问题：Sample失败后，shared snapshot里仍有上一次3.7 V、0 A、25°C，数值完全合法。
2. 直觉方案：只检查range/valid flag；只要值没越保护阈值就继续。
3. 为什么不够：task/I2C卡死不会自动把旧数组清零；合法旧值会永久掩盖变化中的真实pack。
4. 定位：stale/wrap tests让timestamp推进而不publish，验证valid仍true但age跨limit；SIM-17检查DATA_STALE与both inhibit。
5. 最终方案：每group维护timestamp/age/valid/in_range/stale_latched；State独立检查freshness；任一critical group stale -> DATA_STALE BOTH inhibit，恢复需2个fresh accepted frames。
6. 解决：数据“质量”和“时效”分开，旧good snapshot保留用于diagnosis但不能继续授权。
7. 学到：数据模型若只有value+valid，无法表达实时系统最常见的失败——停止更新。

## Story 7 — Stale State publication：晚到的正确计算仍然是错的

1. 问题：StateTask读sample N并计算；期间SampleTask发布N+1；State随后把基于N的decision发布成“当前”。
2. 直觉方案：读snapshot时加mutex，计算完成后再写State snapshot；因为两次操作各自原子，所以认为安全。
3. 为什么不够：计算区间不能长持data mutex；两个原子操作之间仍有TOCTOU window。旧decision可能短暂enable FET。
4. 定位：pre-publish test hook在State commit前注入新identity；review以Task A/B时间线重放race。
5. 最终方案：decision携带 `evaluated_sample_sequence/evaluated_afe_generation`；`BMS_State_PublishIfCurrent()` 再读identity，仅匹配才publish；FET enable path还复核State revision。
6. 解决：旧计算可以完成但不能冒充当前authority；mutex无需覆盖整个decision。
7. 学到：snapshot consistency只保证“读到一张完整照片”，不保证“提交时照片仍是现在”。

## Story 8 — FET revision race：transaction 中 request 会变化

1. 问题：FET Manager读SYS_CTRL2、准备enable；此时Protect收到新fault或State发布新inhibit。
2. 直觉方案：每100 ms再运行一次manager；即使本次短暂写错，下次会纠正。
3. 为什么不够：一次短暂unsafe enable也不可接受；write finalization ambiguity后blind replay可能扩大不确定性；读回相同register也不能证明input decision仍current。
4. 定位：targeted race hook在read/write边界推进Protect revision；另有readback corruption和ambiguous enable场景。
5. 最终方案：捕获Protect/State/Recovery snapshots与revisions，enable前、read后、write/readback后反复revalidate；变化时转safe-off；ambiguous/mismatch进入QUARANTINED，拒绝enable replay。
6. 解决：hardware transaction与software authority绑定；FET Manager成为sole writer，其他模块只publish reasons/intent。
7. 学到：single writer减少冲突，但不自动解决writer内部TOCTOU；revision是transaction协议的一部分。

## Story 9 — Task heartbeat：scheduler alive 不代表每个task alive

1. 问题：SysTick和某些task仍运行时，Sample或Protect可能因等待/bug不再progress。
2. 直觉方案：Idle hook或一个global heartbeat bit表示RTOS alive，State每窗口clear bits。
3. 为什么不够：global signal看不到局部stall；monitor clear与task set会race；event-driven task无限等event时无法定期证明liveness。
4. 定位：架构review画出snapshot->clear race；SIM-23/24分别停止Sample/Protect generation，检查State decision和IWDG feed。
5. 最终方案：7个task各自sole-write monotonic `uint32_t generation`；State只snapshot/compare，不clear；event-driven tasks使用bounded wait；所有first advance后才arm IWDG。
6. 解决：可指出具体stale task；RTOS_HEALTH使BOTH inhibit并停止feed；任何其他task不能伪造它的progress。
7. 学到：watchdog不是“定时喂狗”功能，而是对系统progress contract的最后执行器。

## Story 10 — Balance single writer：方便的第二个writer会变成bug

1. 问题：Recovery想确保all-off，BalanceTask平时写CELLBAL；让Recovery也直接写0看起来最方便。
2. 直觉方案：所有遇到fault的模块都可以写CELLBAL=0，反正目的是安全。
3. 为什么不够：两个task可交错，readback属于谁不清楚；Recovery写0后Balance可能用旧snapshot立刻写回；generation/revision evidence断裂。
4. 定位：owner matrix把每个register writer列出；verifier扫描Recovery不包含CELLBAL register；race analysis检查Balance写前/后source变化。
5. 最终方案：BalanceTask是scheduler-era sole writer；Recovery/State/Protect只发布inhibit/readiness；Balance复核identities，失败时自己attempt all-off并发布confirmation。
6. 解决：Recovery能等待“当前generation的confirmed all-off”，而不是自己制造不可归属的register side effect。
7. 学到：安全目标相同不代表多个writer安全；所有权本身提供因果链。

## Story 11 — Flash commit-last：CRC 为什么还不够

1. 问题：MCU可能在erase/program任意位置掉电。只有一个slot或先写“valid”会让boot接受partial record，或丢掉最后good state。
2. 直觉方案：写struct加CRC；boot CRC正确就用。保存时先erase旧页再写新数据。
3. 为什么不够：erase旧页后掉电没有fallback；CRC/body可能写完但commit语义不明；raw struct含padding/endianness；sequence wrap也不能普通比较。
4. 定位：persistence fixture把storage抽象为read/erase/program，在每个program ordinal注入power cut；另注入corruption和sequence wrap。
5. 最终方案：34-byte explicit encoding，A/B banks，inactive先erase；32-byte body含CRC并readback；16-bit commit marker最后program；boot full decode并wrap-safe选newest。
6. 解决：commit前掉电仍选previous bank；newest corrupt回退older valid；driver限制只写F800/FC00页。
7. 学到：CRC回答“数据是否完整”，commit protocol回答“这次更新是否完成”，A/B回答“失败后是否还有已知好版本”。

## Story 12 — CAN protocol vs hardware：frame logic不是总线证据

1. 问题：Simulator能构造/解析0x180..0x185/0x280，ARMCC5能编译bxCAN BSP，但容易在报告中写成“CAN完成”。
2. 直觉方案：frame test PASS + register初始化代码编译通过，就把CAN标为PASS。
3. 为什么不够：transceiver供电/STB、termination、bit timing tolerance、dominant/recessive voltage、ACK、bus load、EMI/ESD都不在Simulator里；PA11/PA12 register配置不证明线上有frame。
4. 定位：把CAN拆成protocol/core、MCU target binding、physical bus三层，并在validation matrix为每层分配不同evidence。
5. 最终方案：显式wire encoding与service authority tests；静态verifier锁定500k/filter/ISR/sole TX；REAL_HW guide要求CAN analyzer/scope测transceiver/bus/bus-off。
6. 解决：software release可以诚实地说protocol和target binding已实现，同时不冒充physical bus PASS。
7. 学到：工程成熟度的一部分是准确描述证据边界；“我写了driver”与“硬件链路通过”是两个结论。

## 结语：项目真正形成的闭环

这个项目最重要的变化不是模块数量增加，而是每个关键行为逐渐具备：明确owner、coherent snapshot、identity/revision、fail semantics、negative constraints、可重复test与清晰hardware boundary。未来真实板bring-up不会从“猜哪里坏了”开始，而会沿UART/snapshot/waveform把每个软件证据与物理证据对齐。
