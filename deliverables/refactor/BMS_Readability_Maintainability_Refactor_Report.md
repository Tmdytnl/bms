# BMS 可读性、可维护性与注释增强重构报告

## 1. Executive Summary

本 batch 从精确架构基线完成了全仓库 Architecture / Safety / Concurrency /
Readability / Maintainability / Comment 审计，并连续完成 APL 封装、启动与任务编排
重构、共享存储私有化、两个遗留缺陷修正、中文学习型注释、当前架构文档和验证门禁
增强。冻结的 BSP / DRV / FML / APL 分层、安全 authority、七任务拓扑和运行参数保持
不变。ARMCC5、Phase 8/9、race、stress、persistence、trust/static 和 architecture gate
最终均通过。

## 2. Branch / Starting HEAD / Final HEAD

- Branch: `codex/refactor-readability-maintainability`
- Starting HEAD: `5d81c7f50479219e0e71bf98bb0fe2a0a8ee8c3f`
- Final validated implementation HEAD before this report artifact:
  `bc55424962ea86840a1c0febf861bd35e5b970b4`
- Final pushed branch tip includes this report commit；该自包含 commit 的 SHA 由提交后 Git
  产生，并在最终交付消息中精确记录。

## 3. Commits

1. `0850075e1c451f0526bbca0e027186a54137a5bc` —
   `refactor: improve application encapsulation and readability`
2. `9495f77b7ef7e1cb01c2dca01cfb304d506bd908` —
   `test: repair regressions and strengthen refactor gates`
3. `bc55424962ea86840a1c0febf861bd35e5b970b4` —
   `docs: refresh architecture and maintainability guidance`
4. 本报告由独立的 final-report commit 保存；精确 SHA 见最终交付消息。

## 4. Pre-Implementation Audit Findings

内部 Finding Ledger 在修改前一次性完成，主要 findings 为：

- public `apl_rtos.h` 暴露七个 raw FreeRTOS handle，扩大 object identity 耦合面；
- ProtectTask 通过 `APL_SystemAfeDevice()` composition backchannel 回取依赖；
- `BMS_Data` backing store 和 legacy Protect FET request 以 mutable global 暴露；
- `APL_SystemInit()` 与 StateTask 的真实阶段/authority 顺序不够直观；
- `BMS_Soc_RunOnce(NULL, nonzero_count, ...)` 锁存 gap 后仍可能解引用 NULL；
- Sample 配置 guard 的 bool/token 语义名存实亡，scheduler 启动前和外部已 suspend
  场景仍建立不必要的 exclusion；
- SYS_CTRL2 的 CC_EN/CHG/DSG semantic masks 在多模块重复；
- FML/BSP/DRV 注释残留 raw RTOS object、上层业务 owner 和旧 composition wording；
- current architecture 文档仍含旧 `App/Driver/app_rtos` 路径/authority 表述；
- KF-01 BSP PriorityGroup 注释 stale，KF-02 旧报告 final HEAD 仍为占位表达；
- Phase8 ArchitectureRefactorMode 未核验 simulator failure markers；精确起点可复现
  Phase7 11 个、Phase8 Sample 10 个失败但脚本仍误报 PASS；
- trust source-table regression 仍读取已退役 `firmware/Driver` 路径；
- 未发现需要新 Architecture Authority 决策的 deeper safety defect。

## 5. Findings Resolved

上述 in-scope findings 全部处理。特别是 SOC NULL/count fail-closed、startup-aware
concurrency guard、Phase8 exact marker gate 和旧回归场景修正均有目标测试或全量证据。
Architecture gate 新增 raw-handle confinement、private-header boundary、FML mutable extern、
dependency injection 和 Git/Keil-aware legacy-source 检查。

## 6. Findings Intentionally Not Changed

- 未机械拆分 FET Manager、Protect、Recovery、Sample、Persistence 等完整 transaction；
  其现有阶段、identity/revision 与 commit point 已清晰，继续切碎会降低可审计性。
- 未修改 threshold、timeout、task priority/stack/period、CAN protocol、Flash layout、NTC/SOC
  数学、W1C/FET/Balance/IWDG authority。
- 历史 phase/release deliverables、历史 review build scripts 和历史证据中的旧路径保持原貌；
  current architecture 文档、production 注释和当前 trust path 才做修正。
- 历史 `test_phase1_models.c` 不属于当前 ARMCC5/applicable regression chain，未为其重新公开
  已私有化的 `BMS_Data` backing store。
- 未知 untracked `firmware/App/bms_soc.c` 与 Keil `.vscode` 用户文件保持原样且未提交。

## 7. Architecture Preservation

`User/main -> APL -> FML -> DRV -> BSP` 依赖方向保持不变。FML/DRV 仍无 RTOS 依赖，
FML 无 BSP/STM32 peripheral 依赖；七个 task entry 全部留在 APL/Task。SYS_CTRL2 runtime
writer 仍为 FET Manager，scheduler-era CELLBAL writer 仍为 Balance，runtime SYS_STAT W1C
writer 仍为 Protect，IWDG start/feed 仍只在 State execution context 执行。

## 8. Readability Improvements

`APL_SystemInit()` 现在显式显示 board primitives、AFE transport、safety/control、persistence、
runtime feature 和 RTOS creation 阶段；失败顺序不隐藏。StateTask 以七个带语义阶段展示
health、recovery、state、HW qualification、FET、diagnostic/watchdog 和 bounded wait。
RTOS object cleanup 从压缩单行分支展开，CC 两阶段提交和 task 职责获得可学习注释。

## 9. Maintainability Improvements

新增 APL-private RTOS registry、直接 task dependency injection、私有数据 backing store、
共享 SYS_CTRL2 masks 与 runtime concurrency-guard token。FML 对同步机制只依赖窄 port；
当前模块清单、ownership matrix、runtime walkthrough 与单独的代码维护指南同步更新。

## 10. Public API Changes

- `APL_Rtos_CreateTasks(void)` 改为 `APL_Rtos_CreateTasks(BQ76940_t *afe_device)`，NULL
  fail-closed；
- 删除 `APL_SystemAfeDevice()`；
- public `apl_rtos.h` 不再声明 raw RTOS handles；
- public `bms_data.h` 不再暴露 `g_bms_data`；
- public `bms_protect.h` 不再暴露 `g_bms_fet_request`；
- FML runtime port 新增成对的 `BMS_Runtime_ConcurrencyGuardEnter/Exit`；
- mutable data accessor 仅在 Phase8 data/host test 宏下存在，production 无该符号。

## 11. APL Encapsulation Changes

raw handles 集中到 `apl_rtos_internal.h`，只有 APL IPC/IRQ/task glue 与明确 test image 可见。
ProtectTask 的 AFE device 由 composition root 在 `xTaskCreate` argument 中注入。APL public
header 同时移除不必要的 `bms_can.h` 传递依赖，调用者/测试改为声明自身真正依赖。

## 12. Safety-Critical Module Improvements

未改安全决策。Protect 的 CC queue commit-before-W1C 注释与 regression expectation 对齐；
XREADY+CC_READY 同 snapshot 明确以生命周期中断优先。FET/Recovery/AFE startup 使用同一
SYS_CTRL2 semantic mask。SOC 对非法 sample pointer/count 锁存 queue gap 后将 count 清零，
避免越界读取。Sample 配置 guard 只在实际并发运行时建立新 exclusion，且以 token 成对退出。

## 13. Chinese Comment Enhancement

增强集中于 authority 与 execution context、startup ordering、CC two-phase commit、queue
newest-wins、lock boundary、snapshot identity、watchdog gating、BSP/DRV electrical/register
语义和 failure evidence；未进行逐行翻译或 comment-density KPI。

## 14. Stale Comment / Documentation Corrections

KF-01 已将 PriorityGroup owner 从 `main` 改为 APL composition；KF-02 已写入架构阶段精确
HEAD `5d81c7f...`。SoftI2C、IWDG、EXTI、BQ control、FML runtime port 和 CAN 注释不再
泄漏旧 RTOS object/上层 owner。current architecture 文档已使用 BSP/DRV/FML/APL 与
`APL_Task*` 当前名称；trust source citation 改为 `firmware/DRV/BQ76940`。

## 15. main / Startup Flow

`main.c` 仍是 thin APL entry point：`APL_SystemInit()` 成功后 `APL_SystemStart()`，失败进入
`APL_SafeIdle()`。startup helper extraction 保持 Data/Sample fail-safe init、policy/clock
验证、board、AFE、domain owners、persistence、runtime 和 scheduler 的原始先后关系。

## 16. Task-by-Task Review

- Protect：AFE service、domain sample、APL queue、transport ack、后续 W1C 顺序明确；
- Sample：250 ms execution cadence，measurement transaction 仍在 FML；
- State：只编排各 FML authority 与 IWDG decision execution；
- SOC：有界 queue drain，gap 与 NULL/count 行为有回归测试；
- Balance：scheduler-era CELLBAL sole writer 不变；
- CAN Tx：periodic enqueue/target transmit 职责明确；
- CAN Rx：BSP IRQ handoff 后才 enable，domain decode 留在 FML。

## 17. BSP / DRV Review

BSP 仅描述 clock/pin/NVIC/FIFO/IWDG/Flash/UART primitive；DRV 仅描述 SoftI2C、BQ register、
CRC、measurement/control。未引入业务 policy 或 RTOS object identity。共享 register masks
替代四处 magic values，数值保持 0x40/0x02/0x01。

## 18. FML Module Review

Data backing store 私有化；Protect legacy request 私有化；Measurement concurrency guard 与
完整帧发布保持原 ordering；SOC NULL 防护修正；FET/Recovery/AFE 只做 semantic-mask 替换。
State、Health、Balance、CAN、Debug、Persistence、Policy、Fault、NTC/HW Recovery 的算法和
authority 经审计后无需结构性修改。

## 19. Build Results

ARMCC5 V5.06 update 7 production Clean/Rebuild：`0 Error(s), 0 Warning(s)`。最终资源与 HEX
见第 25 节。Phase8 test images 与 Phase9 test image 均无编译/链接 warning/error。

## 20. Regression Results

Phase4、Phase6、Phase7、Phase8 Data、Phase8 Sample、Phase8 AFE 的联合仿真和六镜像拆分
仿真全部 completed，全部 failures=0。起点的 21 个失败已通过当前架构语义修正与真实脚本
场景补全清零；ArchitectureRefactorMode 现在逐行拒绝任何缺失 PASS marker。

## 21. Targeted Race Results

Phase9 targeted races：`3/3` completed，0 failures。Protect revision、XREADY recovery
handoff 与 generation/identity race guard 保持通过。

## 22. Stress Results

`50,000` iterations，`600,000,000` simulated ms，0 failures。

## 23. Persistence Results

`512` persistence transactions，power-cut/A-B commit-last、storage codec 与 continuation
checks 全部 0 failures；Flash layout 与 linker boundary 未改变。

## 24. Trust / Architecture Gate Results

- `verify_architecture.py`: PASS（18 项）；
- `verify_phase9.py`: PHASE9 SIMULATION GATE PASS；
- Phase8 blocker-artifact trust suite: `49/49` OK；
- `git diff --check`: PASS；
- `HARDWARE_CLAIM=NONE`，未把 simulator evidence 表述为实物硬件验证。

## 25. Resource Delta

| Metric | Start | Final | Delta |
|---|---:|---:|---:|
| Code | 53,524 B | 53,720 B | +196 B (+0.37%) |
| RO | 1,092 B | 1,092 B | 0 B |
| RW | 384 B | 384 B | 0 B |
| ZI | 16,520 B | 16,520 B | 0 B |

Final HEX SHA-256:
`4343039e363f40f2a9600a7dd8b9b7445ca9bca5c69d82a3cc7ebdfb7768a8f8`。
Code 增量来自 startup/state 语义 helper、startup-aware concurrency guard 与额外 fail-closed
分支；RAM/const data 无增长。

## 26. Behavioral Diff Audit

对 `5d81c7f..bc55424` production diff 已逐项分类：

- A comment/documentation：中文 WHY/authority/race/commit 注释和 current docs；
- B naming/private structure：raw handle、data store、legacy request 私有化与 semantic masks；
- C API encapsulation：task dependency injection、删除 backchannel/mutable extern；
- D control-flow refactor：startup/State 语义 helper，原顺序保持；
- E test/gate support：exact markers、Git/Keil-aware architecture checks、trust path。

显式复核 timing、threshold、hardware ordering、failure handling、generation/revision、queue、
safety decision、W1C、persistence commit 与 IWDG：除两个授权遗留修正（SOC 非法输入防解引用、
Sample startup-aware guard）外无运行行为变化；两者均 fail-closed 且有回归证据。

## 27. Comment Audit Statistics

- production `.c/.h` files reviewed: 86
- files materially enhanced: 31
- files with stale-comment correction only: 10
- files intentionally unchanged because existing comments were sufficient: 45

未计算或声明 comment coverage percentage。

## 28. Protected Files Confirmation

`docs/reference/**`、`docs/FreeRTOS/**`、`deliverables/phase1..9/**`、
`deliverables/release/**`、`.project-memory/**`、`.agents/**` 均无 diff。未修改/提交 Keil
`.vscode`。验证生成的 Build/map/log 变更在提交前已恢复。未知 untracked 用户文件未覆盖、
未删除、未暂存。

## 29. Known Limitations

- 本报告是 ARMCC5 + Keil Simulator + offline trust/static 证据，不是实物硬件验证；
- Phase8 使用任务书指定的 ArchitectureRefactorMode/applicable images；旧 Phase8 blocker
  hard gate 仍受独立外部 artifact/approval contract 管理；
- final report commit 无法在自身内容中自引用其 Git SHA，精确最终 pushed tip 由最终交付
  消息记录；
- 历史 phase fixture/evidence 不为当前架构追溯性而重写。

## 30. Push Result

第一次普通 push 已成功创建并跟踪：
`origin/codex/refactor-readability-maintainability`，远端 tip 为已验证实现 HEAD
`bc55424962ea86840a1c0febf861bd35e5b970b4`。本报告 commit 随后以同一分支普通 push，
禁止 force push；最终远端 SHA 与结果见最终交付消息。

## 31. Final Verdict

**PASS**

依据：任务范围内 findings 已处理，冻结安全/架构契约保持，全量适用验证为绿色，无受保护
文件或未知用户工作污染，已验证实现已成功普通 push。
