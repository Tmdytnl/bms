# BMS V1 可读性与内聚性重构实施报告

日期：2026-09-25。起点：`a93927061e60753bced8b42e65bcc39402a004fa`，分支：`codex/refactor-readability-maintainability`。图片仅作为分层参考；本次继续使用项目已有的 BSP/DRV/FML/APL 结构。

## 实施范围

- 对项目自有生产 `.c/.h` 审核定义处注释：文件静态与跨文件状态说明用途和归属，结构体成员说明单位或有效条件，函数定义前说明职责，公共声明补充就地接口说明。命名与注释规则见 [`BMS_Code_Naming_And_Comments_Standard.md`](../architecture/BMS_Code_Naming_And_Comments_Standard.md)。
- 将误导性的 `BMS_CanFrame_t.ext_id` 改为 `standard_id`，同步 APL 和测试调用点；协议仍只接受 11-bit 标准帧，载荷布局和六帧 ID 不变。将 FET 模块的 `s_device`、`s_snapshot` 改为明确归属的 `s_afe_device`、`s_fet_snapshot`。
- `BMS_Sample_RunOnce` 使用私有 `BMS_SampleConfiguration_t` 一次捕获设备、校准、NTC、配置版本与 XREADY 身份。捕获仍在原短临界区内，I2C 读取和发布前复核顺序不变。
- FET Manager 将七处相同的“持锁退出→释放总线→发布事务版本”收束到 `BMS_FetManager_FinishLocked`。失败早退和隔离状态仍沿原分支执行；无锁获取失败仍直接发布，不调用释放锁的辅助函数。
- 将 CAN 编解码契约移到 `bms_can_codec.h`，将 Balance 的无硬件写入选择契约移到 `bms_balance_evaluate.h`。运行服务头文件不再引入这些接口所需的 Data、Protect、Recovery、State、FET 类型；调用者显式包含所需头文件。架构门新增此依赖边界检查。

## 阅读路径与安全边界走查

| 入口 | 读者应沿着的路径 | 失败时保留的状态 |
|---|---|---|
| `APL_TaskSample` | `BMS_Sample_RunOnce` → 捕获配置/XREADY → 13S 与 BAT mandatory core → 关联 Protect 的同代 CC → 到期 TS1 → 发布前复核 → `BMS_Data_PublishMeasurement` | 读/锁/身份任一步失败时不发布半帧；可选 CC/TS1 缺席按各自质量元数据表达。 |
| `EXTI1_IRQHandler` | 最小 ISR 通知 → `APL_TaskProtect` → `BMS_Protect_Drain` → CC 队列交接 → Protect 独占 `SYS_STAT` W1C | 交接失败保留 CC_READY；STOP 提交不明时隔离相应 W1C 位，不能盲目重放。 |
| `APL_TaskState` | Health/Recovery → `BMS_State_RunOnce` 分类与方向性禁止 → `BMS_FetManager_Service` 合并三个 owner 的快照 → `SYS_CTRL2` 写入/回读/版本复核 | 新 fault 在 enable 窗口到达或写提交不明时尝试安全全关并保持 unverified/quarantine；寄存器回读不代表外部 MOS 物理状态。 |
| XREADY | Protect 捕获世代 → Recovery 要求 FET/CELLBAL 全关证据 → Protect 授权 W1C → 配置重建/回读 → 校准交接 → 同代首帧 → COMPLETE | COMPLETE 前 Recovery 保持双向禁止；旧世代样本和校准不被复用。 |

上述是源代码控制流走查与模拟验证结论，不是硬件实测。未调整阈值、周期、任务拓扑、Flash A/B 格式、UART 边界或安全寄存器唯一写者。

## 统一验证

| 检查 | 本轮结果 |
|---|---|
| `python firmware/Tests/verify_architecture.py` | PASS，含 CAN/Balance 运行头文件依赖检查与唯一写者检查。 |
| `firmware/Tests/build_phase9.ps1` | PASS；ARMCC5 测试镜像、Keil Simulator、production Clean/Rebuild；24 个 Phase 9 场景、3 个竞态、8 个 continuation 场景与 50,000 次压力迭代通过。 |
| `firmware/Tests/build_phase8.ps1 -ArchitectureRefactorMode` | PASS；六个旧阶段镜像与 production build 均执行。SampleTask 生产调用图最大栈深度 464 字节，unknown=0。 |
| `firmware/Tests/run_phase8_split_simulators.ps1` | PASS，六个 split Simulator 镜像。 |
| `python tools/phase8/test_validate_blocker_artifact.py` | 49 项通过。 |
| `python firmware/Tests/verify_phase9.py` | PASS，重验资源边界、协议、持久化和模拟器证据。 |
| `git diff --check` | PASS。 |

另执行过一次 `build_phase8.ps1 -SkipSimulator -ArchitectureRefactorMode`，脚本按设计返回 `NOT_EXECUTED skipped=Simulator`；随后完整执行上述 Phase 8 命令并通过。构建过程改写的受跟踪日志和 map 已恢复为运行前内容；本报告记录的是本轮终端返回值。模拟器与编译不能代替板级电气验证，本轮未新增硬件合格声明。

## 工作区边界

开始前已有未提交的 `AGENTS.md`、`README.md`、`firmware/APL/apl_system.c` 空白行，以及未跟踪的 `deliverables/agent-config/`、`firmware/App/`、Keil `.vscode/`。本次未把这些现有内容作为重构输入；`apl_system.c` 的原有尾随空格已在触及该文件时清理。实施报告与规范放在 `deliverables/`，未改写只读参考 `docs/`。

按仓库约定尝试创建 GitHub Issue 时，自动审批以外部目标尚未核准、内容包含内部架构与实现信息为由拒绝；因此本轮 Issue 尚未创建，相关代码和本地交付不受影响。
