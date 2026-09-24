# BMS V1 可读性与内聚性重构方案

状态：已按用户确认的范围一次实施。实际代码差异与验证结果见同目录的实施报告；本文件保留重构前确定的目标和验收准则。

## 目标与当前起点

目标是让具备嵌入式 C 与 BMS 基础、首次接触本项目的工程师，能顺着代码理解运行顺序、状态归属、失败处理和硬件副作用；同时减少模块间不必要的依赖。图片提供分层思路，不要求新增本项目不存在的 Bootloader、UDS、网络管理等模块。

当前分支 `codex/refactor-readability-maintainability` 的起点 HEAD 为 `a93927061e60753bced8b42e65bcc39402a004fa`。源码已有 `User/main -> APL -> FML -> DRV -> BSP` 结构，`python firmware/Tests/verify_architecture.py` 在方案编写前通过。先前两轮重构报告已记录在本目录；本次关注尚未解决的阅读成本。工作区现有未提交的 `AGENTS.md`、`README.md`、`firmware/APL/apl_system.c`，以及未跟踪的 agent 配置、旧 `firmware/App/` 和 Keil `.vscode/`，均不作为本方案的代码改动输入，实施时保留并单独核对。

## 不变量

- 保持 V1 产品行为、阈值、七任务拓扑与优先级/周期、CAN 协议、Flash A/B 格式和 UART 非阻塞边界。
- 保持 Protect 的运行期 `SYS_STAT`/XREADY W1C、FET Manager 的调度器后 CHG/DSG、Balance 的调度器后 CELLBAL、StateTask 的 IWDG 唯一写者规则。
- 保持 `sample_sequence`、`afe_generation`、revision、request identity、readback、quarantine、commit-last 等证据顺序；`BMS_Data` 只作诊断聚合。
- 不为了缩短函数而拆散一个必须整体审查的硬件事务或状态机步骤；不用通用框架隐藏安全路径。
- 只在当前 STM32F103 + BQ76940 + FreeRTOS 目标存在真实依赖时建立接口，不引入为未来平台预留的空抽象。

## 一次性实施范围

一次完成全部项目自有生产 `.c/.h` 的定义处注释与命名审计，同时完成主安全路径的代码整理、恢复/均衡状态机的阅读走查、公共头文件的实际依赖审计和受影响测试/构建配置更新。实施过程中允许静态检查和局部编译发现错误，但不设置中途验收点，也不要求用户逐段批准；全部代码完成后只做一次完整的最终验证和整体审查。

主安全路径为 `FML/Measurement/bms_sample.c`、`FML/Protect/bms_protect.c`、`FML/State/`、`FML/Fet/bms_fet_manager.c` 及对应 APL task。恢复、均衡和其余 `FML`、`APL`、`DRV`、`BSP`、`Config`、`User` 均在同一次交付内覆盖。整理顶层阶段、补齐文件静态状态和结构体成员说明、改正含糊命名；仅在重复失败收尾或独立语义阶段确有收益时提取私有函数。

优先从 `BMS_Sample_RunOnce`、`BMS_Protect_Drain`、`BMS_FetManager_Service` 和 `BMS_Recovery_Service` 的**读者走查**开始。这些路径分别约有多阶段采样、事件与 W1C 交接、写入回读与隔离、恢复证据状态机。具体代码变更由逐函数的副作用/早退/锁/identity 表核对决定；长函数并不自动成为拆分目标。

低耦合的审计另看**公共头文件的直接依赖**，不能只看目录：目前 `FML/Communication/bms_can.h` 直接包含 Data、FET、Policy、Protect、Recovery、State 的类型，`FML/Balance/bms_balance.h` 也直接包含多位安全 owner 的类型。实施时逐个核对调用者是否真的需要这些完整类型、公共接口是否混合纯计算与运行服务，再决定是否收窄 include 或拆出更小的契约。不得把安全身份字段挪进无业务语义的万能公共结构，仅为减少 include 数量制造隐式耦合。

## 可读性验收场景

1. 新读者从 Sample task 进入，能找到完整测量何时发布、哪些可选测量可缺席、失败时为何旧快照保持不变，以及 `sample_sequence` 和 `afe_generation` 如何约束消费者。
2. 从 AFE ALERT 进入，能追踪 `SYS_STAT` 事件、CC sample 先入队后 W1C、硬件 fault 的 owner，以及寄存器写提交不明时为何保留隔离状态。
3. 从 State task 进入，能区分运行状态分类、Protect/State/Recovery 的方向性禁止、FET Manager 的最终事务与 readback；能指出新 fault 在 enable 窗口到达时的处理。
4. 从 XREADY 进入，能按阶段说明安全执行器全关、授权 W1C、配置回读、校准交接和首帧证据；在 COMPLETE 前找不到放行 CHG/DSG 的路径。
5. 跳转到修改范围内任一全局/文件静态变量、结构体成员、公共或私有函数的定义，能就地找到符合规范的用途说明；对名称仍无法说明的局部变量/参数，能找到邻近说明。

走查记录应指出具体入口、关键函数、失败分支和仍需来回跳转的地方；读者无法回答时继续调整代码或注释。函数行数、文件数和注释覆盖率只是定位线索，不作为单独通过条件。

## 验证与交付

整体修改时持续比较原始顺序、锁边界、早退后状态、外设写入与发布点。代码完成后统一运行架构门、Phase 9、Phase 8 六个 split Simulator 镜像与 ARMCC5 production Clean/Rebuild、trust-chain 检查；记录实际命令、结果和资源变化。不得沿用旧报告结果当作新代码的验证。

交付包含：完整代码差异、命名/注释规范、场景走查记录、行为差异审计和一次最终验证结果。按仓库约定在 GitHub Issues 跟踪这项新工程工作。发布基线与 `.project-memory/` 三个活动文件不在本次修改范围；本方案也不新增安全 authority 或架构层，因而暂不需要新的 ADR 或领域术语表。
