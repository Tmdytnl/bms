# BMS V1 Phase 8 报告

日期：2026-08-21
阶段：Measurement / SampleTask / AFE Startup Foundation
判定：

```text
PHASE 8 SOFTWARE IMPLEMENTATION: PASS
PHASE 8 REGRESSION/BUILD EVIDENCE: PASS
PHASE 8 HARD GATE: BLOCKED
BLOCKERS: 2
PHASE 9: NOT STARTED
```

## 1. 接管与 Git baseline

本报告由接管 Codex 中断工作的会话生成。Codex 在启动 Phase 8 final full gate 后额度耗尽；接管后确认 gate 证据链已在本地完整生成，随后**从零独立重跑一次 final full gate**（见 §9），两轮证据一致。

| 项 | 值 |
|---|---|
| 工作分支 | `codex/phase8-phase9`（本地，未 push，接管时已存在） |
| 接管前 HEAD | `efb2283`（fix: close Phase 7 safety boundaries，2026-08-20 22:39 +0800） |
| 接管后新增 commits | `9f5a9a0`（实现）、`4699142`（tests/harness）、`35d8c15`（证据）、`eb791d2`（独立重跑证据刷新） |
| 当前 HEAD | `eb791d2` |
| 远端 | `origin/codex/review-phase7` = `4cb25b6`；`codex/phase8-phase9` 尚未 push |
| 禁止覆盖 | `main` / `dsh/phase4` / `dsh/phase5` / `dsh/phase6` / `dsh/phase7` / `codex/review-phase7` 均 UNCHANGED |

现场恢复结论：`efb2283` 为唯一 local-only commit；无 staged changes；Phase 8 实现全部位于工作树（未提交但未覆盖）；reflog 无孤儿 commit；Codex 工作完整保留。

## 2. Phase 7 boundary fix（`efb2283`，保留，不重写）

Codex 已完成并通过测试的 Phase 7 边界修复，随接管原样保留：

- W1C + STOP finalization ambiguity contract；
- ACKed 与 confirmed committed 分离；
- ambiguous W1C containment；
- diagnostic；
- fault active/latched ownership；
- fault summary consistent snapshot；
- XREADY generation；
- CC mailbox generation；
- `XREADY | CC_READY` 同帧处理；
- generation active/quarantine semantics。

## 3. 证据边界

- ARMCC5 V5.06 update 7 (build 960) 生产 Clean+Rebuild（Keil MDK Plus 5.38，UV4 5.38.0.0）；
- ARMCC5 六个独立 test image 的编译/链接/fromelf 尺寸报告；
- Keil Simulator 对实际 production C 的执行（completion probe + failure counter）；
- `verify_phase8.py` 独立 verifier：144 项 manifest（唯一/current inputs）、生产 map 符号绑定、callgraph 栈规则、`uvoptx` 字节级恢复、blocker 判定；
- 本阶段所有执行均在真实 ARMCC5/Keil 工具链上完成；**Simulator 证据是软件证据，不是硬件证据**；transport 与 ISR 硬件交互仍属 `HARDWARE VALIDATION REQUIRED / DEFERRED`。

## 4. 输入基线

| 输入 | 状态 |
|---|---|
| Phase 1 Config/State/Fault | VALIDATED |
| Phase 2 BSP/SoftI2C/CRC | VALIDATED |
| Phase 3 BQ transport/calibration | VALIDATED |
| Phase 4 measurement | CANDIDATE（review 后 repaired） |
| Phase 5 protection/FET/balance control | CANDIDATE（review 后 repaired） |
| Phase 6 FreeRTOS/objects/tasks | CANDIDATE（review 后 repaired） |
| Phase 7 ALERT/EXTI/Protect path | CANDIDATE + boundary fix（`efb2283`） |

## 5. 新增 / 修改 production files

新增（SHA-256）：

- `firmware/App/bms_data.h`（`c22d99ea…`）— 测量快照/发布数据模型（Phase 4 已有基础，Phase 8 扩展原子帧模型）
- `firmware/App/bms_data.c`（`b1431e3e…`）
- `firmware/App/bms_ntc.h`（`7dcd533b…`）— NTC 表校验/插值基础设施（无内建曲线）
- `firmware/App/bms_ntc.c`（`042fcdeb…`）
- `firmware/App/bms_sample.h`（`2985e9eb…`）— Task_Sample 测量所有者
- `firmware/App/bms_sample.c`（`66b10a6b…`）
- `firmware/App/bms_afe_startup.h`（`2d511a4e…`）— AFE 启动状态机
- `firmware/App/bms_afe_startup.c`（`da4a5075…`）

修改：

- `firmware/App/bms_protect.h/.c`（`8a317603…` / `e797cd08…`）：latest-CC mailbox、XREADY generation/active 窄契约、mailbox sequence
- `firmware/App/app_rtos.c/.h`：`Task_Sample` placeholder 移除，实现归 `bms_sample.c`
- `firmware/Config/bms_config.h`（`45173be8…`）：采样周期/超时/新鲜度/范围等 Phase 8 策略 + build asserts
- `firmware/User/main.c`（`c5ff9b0e…`）：`BMS_Data_Init` + `BMS_Sample_Init` + `BMS_Sample_SetDevice`；**不连接** AFE startup/calibration/NTC table（fail-closed）
- `firmware/Project/Keil/BMS_V1.uvprojx`（`ad9b2fca…`）：加入 `bms_ntc.c`、`bms_sample.c`、`bms_afe_startup.c`

## 6. 新增测试文件

- `firmware/Tests/test_phase8_data.c/.h`、`test_phase8_sample.c/.h`、`test_phase8_sample_stub.c/.h`、`test_phase8_afe_startup.c`、`test_phase8_main.c`、`test_phase8_data_host_shim.h`
- `firmware/Tests/build_phase8.ps1`（可复现 gate runner）、`verify_phase8.py`（fail-closed verifier）、`phase8_tests.sct`、`phase8_simulator.ini`
- `firmware/Tests/test_phase7_logic.c`、`test_phase6_task_stub.c`：Phase 7/6 regression 扩展
- 证据：`firmware/Tests/Build/Phase8/*`、`firmware/Project/Keil/Build/BMS_V1_Phase8_build.log`

## 7. 架构

```text
main (startup, 调度器前)
  ├─ BMS_Data_Init() + BMS_Sample_Init()
  └─ BMS_Sample_SetDevice()               (调度器启动后立即，XREADY active 前)

Task_Sample (250 ms 周期, priority 3)
  └─ BMS_Sample_RunOnce(now_ms)
       ├─ XREADY generation precheck      (active/mismatch → reject, 零 AFE 读)
       ├─ 13 组 cell 读 (xI2CMutex, 20 ms 超时) + BAT pack 读
       ├─ latest-CC mailbox 消费 (仅同 epoch) + 周期 TS1 温度组
       ├─ 本地完整 staging → BMS_Data_PublishMeasurement (零等待 xDataMutex)
       └─ XREADY generation postcheck     (epoch 变化 → reject, 保留 pending)

Task_Protect (Phase 7, priority 5)
  └─ CC_READY → 读 CC → 入队 → latest-CC mailbox (generation-tagged)

BMS_Data (原子快照)
  └─ PublishMeasurement / GetSnapshot / GetFreshnessSnapshot (bounded stack)

BMS_AfeStartup (Phase 8 实现, 生产未连接)
  └─ WAKE → PROBE → 12-register 写+读回 → ADCGAIN/OFFSET → PROTECT 组
     → 800 ms settle → 最终 SYS_STAT → XREADY W1C(仅最终授权一次)
     → 全量重配置 → 完成；XREADY 二次出现 → fail closed
```

## 8. 设计要点（Phase 8 实现）

- measurement data model：13-cell/BQ-pack core 必须完整；current/TS1 为独立定时可选组；
- atomic staging → publish：仅全部必需事务成功后原子发布；发布失败保留前帧；
- previous-good-frame retention；validity 与 freshness 分离；sticky stale latch；
- 32-bit timestamp，wrap-safe 无符号减法；stale latch 防止 timestamp full-wrap 复活；
- sample sequence 自然 wrap；`UINT32_MAX → 0` configuration revision wrap 有专项测试；
- latest-CC mailbox：generation-tagged、同帧 `XREADY | CC_READY` 防污染、跨 generation 禁止复用；
- I2C mutex 与 data mutex 分属不相交模块；发布期间不持 I2C 锁；
- AFE startup 状态机：XREADY 恢复/reconfiguration、安全 off 写+读回、ambiguous W1C terminal；
- calibration binding：绑定 inactive XREADY generation；设备更换/同 generation 更换 → invalidation；
- configuration A→B→A ABA 保护；config revision pending state 拒绝同值 setter；
- SampleTask 栈：192 words = 768 B；callgraph 静态最大深度 464 B + 64 B context reserve → runtime margin 240 B；ARMCC5 callgraph 栈规则（无 Unknown）；
- 无内建 NTC 曲线、无猜测 PROTECT3 延时/阈值、生产不连接 AFE startup/calibration/NTC table（fail-closed）。

## 9. Final full gate 证据（两轮一致）

### 9.1 Codex 原轮（提交于 `35d8c15`）

- `RUN_STARTED_UTC=2026-08-20T17:33:30Z`；production Clean/Rebuild `0 Error(s), 0 Warning(s)`，Code=24324 / RO=284 / RW=268 / ZI=10588；
- 6 个 test image `TEST_IMAGE_BUILD_PASS`（Phase4/6/7 regression + data/sample/AFE）；
- 144 个 manifest inputs（唯一/current，SHA-256 `bddaf572…e4e395b`）；
- Simulator：6 套件全部 `completed=1` / `failures=0`（56 个 probe tokens）；
- callgraph：Task_Sample max depth 464 B，unknown=0；Sample stack 768 B / static 464 B / context reserve 64 B / margin 240 B；
- verifier：780 PASS / 0 FAIL / 2 BLOCKER；`uvoptx` 原 hash `ebbd08c9…` 恢复；无残留 UV4/ARM 进程。

### 9.2 接管独立重跑（提交于 `eb791d2`，从零执行）

- `RUN_STARTED_UTC=2026-08-21T13:12:02Z`；production Clean/Rebuild `0 Error(s), 0 Warning(s)`，Code=24324 / RO=284 / RW=268 / ZI=10588（**与 Codex 轮逐项一致**）；
- 6 个 test image 重建并重跑；Simulator 6 套件全部 `completed=1` / `failures=0`；
- verifier 独立复现 **780 PASS / 0 FAIL / 2 BLOCKER**；map/simulator/verifier 结果与 Codex 轮**字节一致**（仅时间戳字段不同）；
- `uvoptx` 恢复原 hash `ebbd08c9…`；无残留 UV4/ARM 进程。

## 10. Hard Gate 判定

```text
RESULT: PHASE8 TEST/EVIDENCE PASS
RESULT: PHASE8 HARD GATE BLOCKED (2 blocker(s))
```

```text
PHASE 8 SOFTWARE IMPLEMENTATION: PASS
PHASE 8 REGRESSION/BUILD EVIDENCE: PASS
PHASE 8 HARD GATE: BLOCKED
BLOCKERS: 2
PHASE 9: NOT STARTED
```

## 11. 两个 unconditional blockers（未绕过）

1. **NTC curve / table**：缺少用户批准并绑定 revision 的真实目标；`verify_phase8.py` 在 `GATE_POLICY_REVISION = phase8-hard-gate-redteam-r3-20260821` 下无条件 block；源码无内建 `BMS_NtcPoint_t` 表，`BMS_Sample_SetNtcTable` 未在生产链接。
2. **AFE protection / PROTECT3 exact policy + calibration/configuration handoff**：缺少用户批准并绑定 revision 的 PROTECT3 延时与生产配置；未猜测延时/阈值，`BMS_AfeStartup_Init` 要求每个 protection 组显式 present flag，零初始化/缺失的 PROTECT3 delay 不得成为 code zero。

解除方式：仅允许通过**未来 gate revision 显式绑定用户批准的不可变 artifact revision 及其批准证据**；禁止以源码字符串、默认数组、注释或猜测绕过（verifier 已设计为不可被新表/标识符静默批准）。

## 12. Phase 9

`PHASE 9: NOT STARTED`。原任务规定仅当 `PHASE 8 HARD GATE PASS` 才允许进入 Phase 9；当前两个 blockers 未解除，**禁止进入 Phase 9**。Phase 10 永不开始。

## 13. 遗留事项

- 两个 external blockers 等待用户批准输入（见 §11）；
- UART1（PA9/PA10 @115200）V1 要求仍未实现；
- 权威 XREADY recovery 已在 Phase 8 AFE startup 中实现但**未接入生产**（等待 blocker 2 批准）；
- 硬件验证（真实 preemptive/ISR/I2C contention/栈/长期压力）与 board evidence 仍属 HARDWARE VALIDATION REQUIRED；
- `deliverables/phase8/` 其余补充材料可在批准 blockers 后扩展。
