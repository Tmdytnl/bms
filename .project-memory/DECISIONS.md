# BMS V1 Durable Decisions

## Project Closure

### D-001 Git is the source of truth

Git、当前源码、测试、构建证据与当前用户指令优先于项目摘要。动态 HEAD 必须从 Git 读取，不在状态文档中复制成长期事实。

### D-002 Project Memory V2 has three active files

Normal handoff 只读取 `PROJECT_STATUS.md`、`TASK_BOARD.md` 与 `DECISIONS.md`。旧日志、archive、legacy snapshot 与 helper workflow 保留为历史材料，不参与当前状态计算。

### D-003 Final project state

BMS V1 的工程状态为 COMPLETE，Engineering Closure 为 COMPLETE，Release Baseline 为 ESTABLISHED，Open Blockers 为 NONE。

### D-004 Scope and evidence discipline

工程结论必须由 Git/source/tests/build evidence 支撑；不得猜测校准、阈值、器件或认证事实，也不得把测试方法名称扩张为其未证明的结论。

### D-005 Repository layout

- `docs/`：项目输入与 reference material；
- `deliverables/`：release、review 与阶段交付；
- `firmware/`：production source、project 与 tests。

## Frozen Safety Architecture

### D-006 State classification is not safety permission

`BMS_State_t` 是运行状态分类；FAULT 本身不是 FET 动作。任何潜在安全源都必须有明确的 CHG/DSG direction inhibit 或明确的 no-FET-effect。

### D-007 Authoritative safety snapshots

ProtectTask、StateTask 与 Recovery Coordinator 分别发布一致的权威快照。`BMS_Data` 聚合只用于诊断，FET Manager 不用它代替实时安全输入。

### D-008 Directional inhibit model

安全输出携带 `inhibit_chg_reasons` / `inhibit_dsg_reasons`。未知或未映射的 active safety source 默认双向禁止，直到存在经过审查的动作定义。

### D-009 Sole scheduler-era FET writer

StateTask 是唯一调用 FET Manager service 的调度上下文；FET Manager 是调度器启动后 SYS_CTRL2 CHG/DSG 的唯一写模块。ProtectTask 与 Recovery 只发布安全输入，不直接写 MOS 控制位。

### D-010 Protect owns runtime XREADY W1C

运行期 XREADY 只有 ProtectTask 可以执行 W1C。Recovery Coordinator 通过 request/ack 协调清除身份；pre-scheduler AFE startup 是独立启动路径。

### D-011 Phaseful recovery is BOTH-inhibited

从首次观察 XREADY 到 recovery COMPLETE，恢复状态始终禁止 CHG 与 DSG。新 generation 会中止并重启旧流程，失败状态继续保持双向禁止。

### D-012 Generation-bound calibration and measurements

恢复校准必须携带 XREADY generation、recovery revision 与 post-clear verification provenance。测量发布携带 `sample_sequence` 与 `afe_generation`，旧生命周期数据不能成为新生命周期证据。

### D-013 Sequence-bound State decisions

State 软件保护通过 evaluated sample identity 与 compare-and-publish 绑定一致测量；过期发布必须拒绝并重新计算，FET enable 不能依赖旧 generation decision。

### D-014 Identity-bound hardware recovery

HW_OV / HW_UV / HW_OCD recovery 使用 `request_id`、`source_generation`、Protect revision、measurement evidence 与 expiry 绑定完整交换，避免把过期请求、旧测量或瞬时寄存器状态误当成恢复完成。

### D-015 Generation-counter health model

每个必需任务只写自己的单调 `uint32_t` heartbeat generation。StateTask 比较连续窗口内是否推进，并且是唯一启动和喂 IWDG 的任务。

### D-016 Continuation ownership

Task_SOC 是 SOC estimate 唯一写者；Task_Balance 是调度器启动后 CELLBAL 唯一写者。CAN 与 UART 是诊断/服务通道，没有 FET、CELLBAL、fault bitmap 或 IWDG authority。

### D-017 Persistence transaction

Flash 持久化使用 A/B page、version、sequence、CRC32、wrap-safe newest-valid 与 commit-last。先写 payload、校验回读，最后写 commit marker，使任一中断阶段至少保留旧 valid slot。

## Release Decision

### D-018 BMS V1 Release Baseline accepted

`deliverables/release/BMS_V1_Release_Baseline.md` 是正式 Release Baseline。验收证据固定为 32/32 deterministic scenarios、3/3 targeted races、50,000 stress iterations / 0 failures 和 ARMCC5 Clean/Rebuild 0 errors / 0 warnings。
