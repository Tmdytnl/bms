# BMS V1 Release Baseline

## 基线标识

| 字段 | 内容 |
|---|---|
| Project | BMS V1 |
| Contract | `BMS-FINAL-POLISH-CONTRACT-V1` |
| Status | ACCEPTED |
| Engineering Closure | COMPLETE |
| Branch | `dsh/project-finalization` |
| Starting HEAD | `714c4d63f8833d50b380feefbb841d633b4a6261` |
| Date | 2026-08-26 (Asia/Shanghai) |
| Toolchain | Keil MDK5 / uVision 5.38；ARMCC5 5.06 update 7 build 960 |
| MCU / AFE | STM32F103C8T6 / BQ7694003 / 13S |
| Policy profile | `SIM-HW-POLICY-V1` / `SIM_POLICY_V1` |

本文件固定 BMS V1 的最终工程内容、冻结架构、可重复构建方法与验收证据。最终提交身份以 Git 分支 HEAD 为准，不在提交内容中写入自指哈希。

## 功能基线

- 时钟、GPIO、TIM3、软件 I2C、CAN、UART、Flash 与 IWDG BSP；
- BQ7694003 CRC 通信、启动、配置、校准、13S 采样、电流/包压/NTC 换算；
- 带 validity、freshness、`sample_sequence` 与 `afe_generation` 的一致测量快照；
- 七任务 FreeRTOS 架构、所有权明确的 mutex / queue / semaphore / event / notification；
- Protect / State / Recovery 权威安全快照与方向性 CHG/DSG inhibit；
- 分阶段 XREADY 恢复、generation 绑定校准和 source-specific hardware recovery handshake；
- FET Manager 单写 SYS_CTRL2、transaction revision 与 write/readback/confirm 闭环；
- heartbeat generation 健康模型和 StateTask 独占 IWDG 喂狗；
- SOC、CELLBAL 单写均衡、`0x180..0x185` 周期 CAN 与 `0x280` 服务帧；
- 34 B Flash A/B record、CRC32、wrap-safe newest-valid、commit-last 与 readback verify；
- 1 秒 UART 只读快照、每次最多 8 B 非阻塞排空，不影响安全控制调度。

## 冻结安全契约

- State classification 不等于 FET permission；
- ProtectTask 持有 HW/AFE fault 与运行期 XREADY W1C；
- StateTask 持有软件保护、DATA_STALE、RTOS_HEALTH，并独占 IWDG 喂狗；
- FET Manager 是调度器启动后 SYS_CTRL2 CHG/DSG 唯一写者；
- BalanceTask 是调度器启动后 CELLBAL 唯一写者；
- `BMS_Data` 只做诊断聚合，不是 FET 安全权威；
- recovery-in-progress 全程 BOTH inhibit；
- `sample_sequence`、`afe_generation`、generation/revision 与 compare-and-publish 拒绝跨生命周期旧证据；
- CAN、UART 与持久化接口不能绕过安全 ownership。

## 验收证据

| Evidence | Result |
|---|---:|
| Phase 9 core deterministic scenarios | 24 / 24 PASS |
| Continuation scenarios | 8 / 8 PASS |
| Total deterministic scenarios | 32 / 32 PASS |
| Targeted race tests | 3 / 3 PASS |
| Randomized stress | 50,000 iterations / 0 failures |
| Equivalent simulated state time | 600,000,000 ms |
| Injected events | 6,601 |
| Persistence transactions / power cuts | 512 / 381 |
| Phase 4/6/7 + Phase 8 data/sample/AFE suites | PASS / all failures 0 |
| Trust-chain tests | 49 / 49 PASS |
| ARMCC5 Clean/Rebuild | 0 errors / 0 warnings |
| Baseline / final HEX SHA256 | `0C8D17319AA29031C0CC6FEC3EDD03269D3DFDCA71DFF9A6A0FB293F8DCD375E` / identical |
| C/H comment-only token audit | 86 files / 0 executable-token differences |
| Python/PowerShell comment audit | 8 files / 0 non-comment differences |

Simulator 在本基线中是一种运行 production-C 的可重复测试工具。完整回归同时使用确定性场景、针对性竞态、随机压力、静态 verifier、ARMCC5 test images 与 production Clean/Rebuild，形成互相独立的证据链。

## 资源基线

| Item | ARMCC5 result |
|---|---:|
| Code | 53,288 B |
| RO data | 1,092 B |
| RW data | 364 B |
| ZI data | 16,508 B |
| RW + ZI | 16,872 B |
| Application Flash region | `[0x08000000, 0x0800F400)` = 62,464 B |
| SRAM limit | 20,480 B |

12 KiB `ucHeap`、1 KiB MSP 与 512 B C heap 已计入 linked RW+ZI；动态任务栈、TCB、队列、mutex、semaphore、event group 和 timer queue 消耗该预留 heap，不能重复加到 RW+ZI。链接布局保留 SOC 页 `0x0800F400`、参数 A 页 `0x0800F800` 与参数 B 页 `0x0800FC00`，应用镜像不与三页重叠。

## 工程文档与复现入口

- 项目入口与构建命令：`README.md`；
- 架构、所有权和运行流程：`docs/architecture/`；
- 配置、验证、调试和学习材料：`docs/config/`、`docs/validation/`、`docs/bringup/`、`docs/learning/`；
- production-C 回归入口：`firmware/Tests/build_phase8.ps1`、`build_phase9.ps1` 与 `run_phase8_split_simulators.ps1`；
- 构建与执行证据：`firmware/Project/Keil/Build/`、`firmware/Tests/Build/Phase8/`、`firmware/Tests/Build/Phase9/`。

## Release 结论

**BMS V1 RELEASE BASELINE**
**STATUS: ACCEPTED**

**Engineering Closure: COMPLETE**
**Release Baseline: ESTABLISHED**
**Open Blockers: NONE**
