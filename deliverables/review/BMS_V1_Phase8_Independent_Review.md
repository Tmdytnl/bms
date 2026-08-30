# BMS V1 Phase 8 独立代码审查报告

- 审查性质：只读 Review；未修改 production code、tests、配置或 Git 历史。
- 审查范围：`origin/codex/review-phase7..codex/phase8-phase9`。
- 基线 SHA：`4cb25b60a2374c06a0a59f903bb73dd6bd716d74`
- 候选 SHA：`08bf1944d12ea57f439a8710718e7057ac7fe5e4`
- 说明：本地不存在 `codex/review-phase7`，已按其已验证的远程跟踪引用 `origin/codex/review-phase7` 解析同一 SHA；工作树干净。

## Verdict

```text
ACCEPT FOR SAFETY REVIEW
```

此结论记录 Phase 8 独立评审检查点；两个 policy input identity 的后续批准绑定与最终 disposition 已纳入 Release Baseline。

## Findings

### Critical

无。

### High

无。

### Medium

无。

### Low

#### L-01：Phase 8 报告的当前 Git 状态已过期，且 Sample 优先级写错

- **File:** `deliverables/phase8/BMS_V1_Phase8_Report.md:21-25,100`
- **Problem:** 报告仍将候选描述为“尚未 push”、`HEAD=eb791d2`，但当前候选为已推送的 `08bf194`；同时把 `Task_Sample` 写为 priority 3，而 `app_rtos.h:29` 的实际优先级为 4。
- **Concrete failure scenario:** 独立 Reviewer 若以报告而非 Git/代码为准，会审查错误 revision，或对 Protect(5)/Sample(4)/State(3) 的调度关系作出错误判断。
- **Why existing tests missed it:** gate 校验源码、构建、map 和 manifest，不校验报告中的 Git/架构叙述。
- **Minimal fix direction:** 将这些表述明确标记为“接管时历史状态”，或更新为最终 pushed SHA 与 priority 4；无需重跑 gate，除非把报告纳入新的 evidence-hash contract。
- **Required regression:** 文档发布前用 `git rev-parse` 与 `app_rtos.h` 的优先级定义做一次一致性检查。

## Safety properties

| Property | Status | Review basis |
|---|---|---|
| Atomic snapshot | PASS | `BMS_Data_PublishMeasurement()` 在 `xDataMutex` 内更新整个 staged frame；读端在同一 mutex 内复制。未发现 new-cell/old-pack 或 old-active/new-metadata interleaving。|
| Validity/freshness | PASS | valid、age、range、sticky stale 分离；失败帧和失败 publish 保留 previous-good；新的对应组 publication 才解除 stale。|
| Timestamp wrap | PASS | 无符号差值覆盖正常环回；首次超龄由 stale latch 固化，避免完整 32-bit wrap 复活。完整一圈期间从未执行 reader 的情形仍须 watchdog/硬件运行时保证。|
| XREADY generation | PASS | inactive→active 时 generation 原子递增、mailbox invalid；Sample 在 I2C 前和 publish 前 guard，最终 guard 与 publish 在 scheduler suspension 内。物理 ALERT 尚未由 ProtectTask 捕获的极窄窗口属于硬件/时序边界。|
| CC generation | PASS | latest CC 同时带 sequence 与 XREADY generation；同帧 XREADY+CC 先失效 latest 再处理 CC；新 epoch 首个 core publish 无同代 CC 时原子置 current invalid。|
| Configuration ABA | PASS | setter 递增 revision，final compare 防 A→B→A；MAX→0 immediate wrap 有 production-C test seam 覆盖。完整 2^32 次配置变更 alias 未被宣称解决。|
| I2C concurrency | PASS | Sample 每个事务后释放 I2C，再读 mailbox/转换/发布；Data 模块不获取 I2C；未见反向 `Data→I2C` 锁链或 double give。Protect 只持 I2C 调 drain/recovery。|
| Sample stack | PASS | ARMCC5 当前 callgraph 中 `Task_Sample` block 为 Max Depth 464 B、无该 task chain 的 `+ Unknown`，配置 768 B，扣 64 B context 后余 240 B；证据方法与 watermark/ISR 压力观察维度分别记录。|
| AFE startup | PASS | 状态机 fail-safe 写/读回 FET-off 与 CELLBAL-off；probe/wake 有界；保护配置显式 present；800 ms settle；XREADY W1C 前失效旧证据并强制完整重配；readback/ambiguity 终态 fail-closed。生产尚未接线，符合 blocker 下的 fail-closed 策略。|
| W1C ambiguity | PASS | ACKed data/CRC 与实际 side effect 明确分离；ambiguous STOP 不报告成功、不盲重放，CC/XREADY quarantine 与 diagnostics 存在。持续 high 下 old/new CC identity 不可由软件证明，已保留为硬件验证要求。|
| Hard-gate blocker enforcement | PASS | verifier 的两个 blocker 由 `block()` 无条件产生，并要求未来 gate revision 显式绑定批准 artifact；普通数组、注释、字符串、默认值或调用不会使本 revision 自动 PASS。main 未接入 NTC/AFE startup/calibration。|

## Test/evidence assessment

- **production C coverage:** 强。Data、NTC、Sample、AFE startup 和 Phase 7 Protect 实际 C 源被 ARMCC5 test image 编译、链接并由 Simulator 执行；并覆盖 previous-good、publish fail、stale/wrap、XREADY crossing、CC epoch、同帧 XREADY+CC、device replacement、config ABA、MAX→0、mutex model 和 AFE fail paths。驱动和调度竞争使用 stub，因此不是实机抢占证明。
- **simulator evidence:** 可信的软件执行证据：六 image 均 `completed=1 / failures=0`。不能外推为 BQ 电气、ALERT 时序、I2C 波形或 MOS 硬件 PASS。
- **ARMCC5 evidence:** 可信。ARMCC5 5.06u7，production Clean/Rebuild，`0 Error(s), 0 Warning(s)`，Code/RO/RW/ZI=`24324/284/268/10588`。
- **manifest integrity:** 强。144 个输入 hash 与当前文件匹配，证据时间不早于输入；uvoptx 临时 patch 和字节级恢复可验证。
- **callgraph evidence:** Task_Sample 的当前链路可追溯且 task-specific Max Depth=464 B。整张 callgraph 的全局 header 存在其他函数的 Unknown 提示，因此不得把该结论扩展成全固件或运行时栈已验证。

## Physical interface observations recorded by the review

该历史 review 记录了以下物理接口观测维度：BQ W1C commit point 与 ALERT timing；I2C stuck-bus/STOP/brownout；preemptive/ISR contention；Task_Sample stack watermark/长时压力；MOS/FET 行为；current calibration；NTC；watchdog reset；EMI、thermal、电源与 PCB/BOM。

## Recommended next action

```text
4. Ready for later Codex Sol High safety review
```

前提不变：该复审不得把 simulator 替代硬件验证，也不得解除两个 external blockers；L-01 可作为独立文档卫生修订处理。
