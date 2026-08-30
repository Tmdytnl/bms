# BMS 代码可读性与可维护性指南

本文适用于 BMS V1 当前的 `User/main -> APL -> FML -> DRV -> BSP` 结构。目标不是
统一成某种“企业模板”，而是让修改者能快速识别职责、authority、执行上下文、失败
语义与验证证据，并在不改变冻结行为的前提下安全演进代码。

## 1. 代码结构哲学

优先使用简单 C、显式控制流和局部状态。一个函数可以较长，只要它表达的是一个完整
transaction 或 state-machine step；只有出现真实阶段、重复决策或明显混合职责时才提取
private helper。不要用通用 DI container、event bus、函数指针框架或宏元编程隐藏安全路径。

结构调整遵循三个顺序：先确认 behavior/ownership 不变量，再整理代码边界，最后补充能
解释 WHY 的注释。格式化和重命名不能替代这三步。

## 2. 分层感知的命名

- BSP 名称描述 MCU primitive，例如 GPIO、EXTI、IWDG、CAN FIFO；不出现 SOC、状态机、
  FET permission 或 protection policy。
- DRV 名称描述 bus、BQ register、measurement conversion、control composition；不出现
  FreeRTOS task/queue 或上层业务状态。
- FML 名称表达 BMS domain：Measurement、Protect、State、FET、Recovery、Health、SOC、
  Balance、CAN protocol、Debug format、Persistence algorithm。
- APL 名称表达 composition、task、IPC、IRQ handoff、时间与硬件执行服务。`APL_Task*`
  表示 execution context，不自动表示它拥有同名安全 authority。

private helper 使用它在控制流中的真实语义，例如 `CaptureSafetyInputs`、
`AttemptSafeOffLocked`、`PublishDiagnostics`。避免 `DoStep1`、`HandleThing` 等需要读完整
实现才知道含义的名称。

## 3. API 动词约定

| Verb | 约定语义 |
|---|---|
| `Init` | 建立模块内 fail-safe 初值；不表示系统已经 technical-ready。 |
| `Set` / `Bind` | 注入依赖或配置；失败时旧配置是否保留必须在契约中说明。 |
| `RunOnce` | 执行一次完整领域计算或周期 transaction，不包含永久循环。 |
| `Service` | 有界推进一个 service/state-machine；可能返回“仍需后续服务”。 |
| `GetSnapshot` | 一致只读复制；调用者不能借返回值获得写 authority。 |
| `Submit` | 提交 request/evidence；不等于 owner 已接受或执行副作用。 |
| `Complete` | 完成两阶段 acknowledgment/commit，并显式退休对应 pending identity。 |

不要为了表面一致 mass rename。只有名称真实误导行为或暴露不必要实现细节时才调整，并
同步所有 call sites、测试与静态 gate。

## 4. 注释应该解释什么

优先解释：模块 authority、调用上下文、关键不变量、硬件副作用、commit point、失败后
保留什么证据、generation/revision 如何拒绝 stale data，以及 race 为什么需要二次复核。

关键例子包括：

- enable 前为何必须重新验证 Protect/State/Recovery revision；
- CC queue commit 为何必须早于 CC_READY W1C；
- safe-off 可以在 stale input 下继续，而 enable 必须拥有 current evidence；
- readback mismatch 为何只能发布 unverified/quarantined；
- recovery 为什么在 COMPLETE 前始终 BOTH inhibit；
- Flash 为什么先写 inactive body、验证后最后写 commit marker。

## 5. 注释不应该解释什么

不要逐行复述语法，例如“计数加一”“NULL 就返回”“flag 设为 true”。不要声称代码并未
提供的保证，例如把 register readback 写成 MOS 已导通，或把 Simulator PASS 写成真实硬件
通过。不要在 BSP 注释中写入 BMS policy，也不要在 DRV 注释中写具体 RTOS object 名称。

历史报告可以保留当时路径；当前架构文档与 production 注释必须使用当前模块名。任何
修改后的注释都应和实际分支、调用者、等待上限、failure path 一起复核。

## 6. 安全代码的注释风格

安全代码注释应把“意图”和“已经证明的事实”分开：write success 只是命令被 transport
接受，full-byte readback 才能形成 register-level confirmation；register confirmation 仍
不等于外部功率通路的物理状态。

对于 W1C、enable、commit-last 等不可随意重放的副作用，注释应指出 sole writer、当前
identity、commit point 和 ambiguous finalization 的处理。不要用“应该没问题”“重试一下”
等模糊措辞。

## 7. Authority 与 execution context

文档和注释统一区分：

- authority：谁能改变领域权威状态或发出特定硬件写；
- execution context：哪个 task/ISR/pre-scheduler flow 调用该 authority；
- owner：谁维护某份 snapshot、generation、revision 或 queue endpoint。

例如 FML FET Manager 是 scheduler-era SYS_CTRL2.CHG/DSG authority，APL State task 只是
它的执行上下文；FML Protect 是 runtime SYS_STAT W1C authority，APL Protect task 只调度
I2C 与两阶段 queue handoff。不能因为函数从某个 task 调用，就把安全 authority 写成该 task。

## 8. 并发与 race 注释

并发注释要说明临界区保护的对象和边界，而不是只写“加锁”。当前约束是：

- bus lock 与 data lock 不嵌套；硬件 transaction 完成后才发布共享数据；
- scheduler exclusion 只包围短小 snapshot/counter/revision，不包围 I2C、Flash、UART 或等待；
- ISR 只复制/投递，业务 decode、W1C、FET 与 fault lifecycle 留在 task；
- 跨硬件 transaction 的输入必须携带 generation/revision，并在 enable/commit 前后重验；
- queue 的 producer/consumer 以及 full/drop/newest-wins 语义必须唯一且可追踪。

若修改 race guard，注释至少回答：什么状态可能在窗口内改变、用哪个 identity 检出、检测
后是拒绝 enable、尝试 safe-off、保留 pending 还是进入 quarantine。

## 9. Driver 与 BSP 注释风格

DRV 注释聚焦 register/electrical protocol：open-drain、clock stretch、ACK/NACK、CRC、
STOP ambiguity、W1C、bit preservation、conversion domain 与 timeout。BSP 注释聚焦 pin、
clock、NVIC、FIFO、Flash page、USART/IWDG register 行为和有界等待。

上层业务语义留在 FML/APL。例如 BSP IWDG 只说明 start/feed 的硬件效果，上层 Health 决定
何时允许启动或刷新；SoftI2C 只说明完整 transaction 需要由上层独占，不点名 FreeRTOS
mutex。

## 10. 安全地重构大函数

1. 先写出原流程的 observable ordering、side effects、failure returns 和 owner updates。
2. 找出真实语义阶段；没有阶段就不要机械切碎。
3. helper 默认 `static`，参数显式传入；不要新增 composition backchannel 或 mutable global。
4. 保留原调用顺序、timeout、retry bound、revision update 和 commit point。
5. 每提取一段，检查 early return 是否仍留下原来的 fail-safe 状态。
6. 对硬件 transaction 比较 write/readback/W1C 顺序，对并发路径比较锁边界与重验点。
7. 更新测试/gate 后运行完整 ARMCC5、回归、race、stress、persistence 与 trust chain。

`APL_SystemInit()` 是典型例子：允许提取 board、AFE transport、functional initialization、
persistence restore 和 runtime creation，但顶层仍必须清楚显示 fail-safe 启动顺序。

## 11. 新模块检查清单

- 它属于 BSP、DRV、FML 还是 APL？是否企图形成新层？
- 它拥有哪份状态，authoritative writer 是谁，读者是谁？
- 谁调度它？task/ISR/pre-scheduler context 是否有界？
- 一个 `RunOnce`/`Service` 周期做什么，失败返回代表什么？
- 是否有 hardware side effect；SYS_CTRL2、CELLBAL、SYS_STAT W1C、IWDG、Flash writer 是否冲突？
- snapshot coherence 用什么同步；是否违反 bus/data lock order？
- stale evidence 用 sequence、generation、revision、request ID 还是 expiry 拒绝？
- public header 是否只暴露必要契约；private helper/state 是否为 `static`？
- 注释是否解释 WHY/authority/failure，而没有复述代码或越层描述？
- Keil membership、architecture gate、production build、scenario/race/stress/persistence 测试如何覆盖？

## 12. Review 结束条件

一次可维护性改动只有在以下条件同时满足时才完成：行为差异已经分类并复核；冻结 timing、
threshold、protocol、persistence layout、task topology 和安全 writer 未改变；production build
无 warning/error；architecture gate 与完整证据链通过；current docs/注释没有继续指向旧路径
或夸大验证结论。
