# BMS 四层架构解耦重构实施报告

## 1. Executive Summary

已从精确 baseline `f3eb9b03a224f1ea2ece895dfc621c060df4986f` 完成 BSP / DRV / FML / APL 四层重构。物理目录、应用编排、七任务入口、RTOS/IPC/IRQ、CAN/UART/Flash 适配及依赖硬门均已落地；冻结安全与行为契约未改变。所有可用 ARMCC5、Keil Simulator、并发、压力、持久化和静态门验证通过。

## 2. Branch / Starting HEAD / Final HEAD

- Branch：`codex/refactor-bsp-drv-fml-apl`
- Starting HEAD：`f3eb9b03a224f1ea2ece895dfc621c060df4986f`
- Final implementation/test HEAD（文档提交前）：`6ccb6df`
- Final pushed HEAD：`5d81c7f50479219e0e71bf98bb0fe2a0a8ee8c3f`

## 3. Commits

1. `8cbe1ba refactor: establish BSP DRV FML APL layout`
2. `daa692e refactor: extract application orchestration and RTOS ownership`
3. `7180b20 refactor: enforce BSP and DRV dependency boundaries`
4. `6ccb6df test: adapt regressions and add architecture dependency gate`
5. 最终文档提交：包含架构文档与本实施报告。

## 4. Final Directory Architecture

```text
firmware/
  BSP/                 STM32 board primitives
  DRV/SoftI2C/         software-I2C transport
  DRV/BQ76940/         BQ76940 transport/measurement/control
  FML/                 Afe, Core, Data, Measurement, Protect, State,
                       Fet, Recovery, Health, Soc, Balance,
                       Communication, Storage
  APL/                 system, RTOS, ports, CAN/UART adapters, IRQ
  APL/Task/            seven production task entries
  User/main.c          thin entry
```

旧 `firmware/App/`、`firmware/Driver/` 不再包含生产源副本。

## 5. Current-to-New File Mapping

- `Driver/bsp_*` → `BSP/`
- `Driver/soft_i2c.*` → `DRV/SoftI2C/`
- BQ76940/CRC driver → `DRV/BQ76940/`
- 原 `App/bms_*` → 按功能拆入 `FML/<Function>/`
- `app_rtos.*` → `APL/apl_rtos.*`
- task entry → `APL/Task/apl_task_*.c`
- EXTI/CAN RTOS-aware ISR → `APL/apl_irq.c`
- startup wiring → `APL/apl_system.c`

完整职责和映射见 `docs/architecture/BMS_Layered_Architecture_Refactor.md`。

## 6. Dependency Rules Implemented

- FML 无 RTOS、BSP 和直接 STM32 peripheral dependency。
- DRV 无 RTOS 和 APL/FML business-header dependency。
- BSP 无 APL/FML business-header dependency。
- APL 独占 OS/timing/IPC/IRQ/hardware scheduling，并只调用 FML 的领域 API。
- `verify_architecture.py` 对依赖方向、任务位置、IRQ 位置、旧目录退休和 sole-writer 集合执行硬门。

## 7. RTOS Dependencies Removed From FML

FML 中的 tick 读取改为显式 `now_ms`；task entry、period、delay、notification、heartbeat call site 与 queue ownership 均移到 APL。I2C mutex、data mutex 与 scheduler exclusion 通过六个窄 `BMS_Runtime_*` port 接口实现，生产绑定在 `apl_fml_port.c`，测试绑定在 `test_fml_runtime_port.c`。

CC sample 已成为 plain `BMS_CcSample_t`。FML Protect 导出两阶段 sample；APL Protect task 完成 newest-wins FreeRTOS queue transport；FML SOC 接收 plain sample array。

## 8. main.c / APL Composition Changes

`main.c` 只调用 `APL_SystemInit()`、`APL_SystemStart()` 与 `APL_SafeIdle()`。原 policy/clock/BSP/SoftI2C/BQ/AFE startup/calibration/module/persistence/CAN/RTOS wiring 全部进入 `apl_system.c`，任一 mandatory startup failure 仍 fail-safe，禁止半初始化 scheduler。

## 9. Task-by-Task Changes

- Protect：APL 承担 EXTI enable、semaphore、10 ms retry、CC transport、heartbeat；FML 保留 bounded drain/W1C/fault authority。
- Sample：APL 固定 250 ms；FML 只执行一次完整采样。
- State：APL bounded wait/urgent wake/IWDG；FML 保留 Health/Recovery/State/HW recovery/FET 业务 authority。
- SOC：APL 固定 1 s、最多消费 8 条 queue sample 与 overflow bit；FML 执行整数积分和端点校正。
- Balance：APL 固定 1 s；FML 保留 CELLBAL sole-writer transaction。
- CAN Tx：APL 10 ms hardware/UART service 与 100 ms周期发布；FML 只产生协议帧/诊断格式。
- CAN Rx：APL 100 ms queue wait；FML 只解码 identity-bound service request。

优先级仍为 `5/4/3/3/2/2/2`。

## 10. Safety Ownership Preservation

- FET Manager 仍是 scheduler-era SYS_CTRL2/CHG/DSG sole writer。
- Balance 仍是 scheduler-era CELLBAL sole writer。
- Protect 仍是 runtime SYS_STAT/XREADY W1C owner。
- Recovery 仍通过 generation/revision request/ack 授权，不直接 W1C XREADY。
- State classification 不等于 FET permission；directional inhibit 保持分离。
- measurement identity、calibration provenance、HW recovery handshake 均保持 generation/sequence/revision 绑定。
- IWDG 仅在 APL State execution context start/feed。
- `BMS_Data` 仍为诊断/shared projection，不进入 FET authority 链。

## 11. CAN / UART / Persistence Boundary Changes

- CAN：FML 负责 frame encode/decode 与 service request；APL 负责 queue、mailbox retry、bus-off recovery 和 RX ISR；BSP 负责 bxCAN peripheral。
- UART：FML Debug 只生成/维护只读 telemetry buffer；APL 每 10 ms 最多非阻塞发送 8 B；无 command parser。
- Persistence：FML 保留 CRC、A/B select、commit-last、throttle；APL 提供 storage ops；BSP 只允许两个 reserved page 的 read/erase/halfword program。

## 12. Build Results

- Toolchain：ARM Compiler 5.06 update 7 (build 960)。
- Production Clean/Rebuild：`0 Error(s), 0 Warning(s)`。
- Final resources：Code `53524` B，RO `1092` B，RW `384` B，ZI `16520` B。
- Sample task static max depth：`480` B，Unknown=`0`。
- Final HEX SHA-256：`d7953e03fba481d62d5c635d48f9bab08dd33673bf3e8e281803f00d38508312`。

## 13. Regression Results

- Phase 8 六个 ARMCC5 test image 全部 build pass：Phase 4、6、7、8 Data、8 Sample、8 AFE。
- Phase 8 split Keil Simulator：全部 failure counter 为 0，`PHASE8_SPLIT_SIMULATOR_REGRESSION_PASS`。
- Phase 9：24 个核心场景、3 个 targeted races、8 个 continuation 场景全部通过。

## 14. Stress / Persistence / Trust Results

- Stress：50,000 iterations，600,000,000 simulated ms，0 failures。
- Random fault events：6,601。
- Persistence transactions：512；power cuts：381；0 failures。
- Phase 9 trust/static verifier：PASS。
- Phase 8 legacy verifier不作为当前架构门运行，因为它的历史冻结断言要求 Phase 9 模块不存在；使用 `-ArchitectureRefactorMode` 运行全部实际回归、生产构建与新架构硬门。

## 15. Architecture Static-Gate Results

`python firmware/Tests/verify_architecture.py`：PASS。已证明 FML/DRV RTOS-free、BSP/DRV 无向上 business include、FML 无 MCU/BSP 访问、main 薄入口、七任务与两个 IRQ 位于 APL、旧混合目录退休，以及 SYS_CTRL2/CELLBAL/SYS_STAT writer 集合符合冻结 ownership。

## 16. Resource Delta

相对 baseline `f3eb9b0`：

| Resource | Baseline | Final | Delta |
|---|---:|---:|---:|
| Code | 53288 | 53524 | +236 B |
| RO | 1092 | 1092 | 0 B |
| RW | 364 | 384 | +20 B |
| ZI | 16508 | 16520 | +12 B |

未把 HEX 相同作为硬门；架构路径/group 变化后 final hash 如上。

## 17. Known Limitations / Unavailable Validation

- 本任务环境没有真实 BMS 硬件测试声明；所有结果标记 `HARDWARE_CLAIM=NONE`。
- 已完成 ARMCC5 target build 与 Keil Simulator validation；真实 AFE/CAN/Flash/UART 电气行为仍依赖既有硬件资格证据，本次未宣称新增硬件验证。

## 18. Protected Files Confirmation

未修改 `docs/reference/**`、`docs/FreeRTOS/**`、既有 `deliverables/phase1..9/**`、`deliverables/release/**`、`.project-memory/**`、`.agents/**` 或 `BMS_V1.uvoptx`。用户未跟踪的 `firmware/Project/Keil/.vscode/` 保持未修改、未加入 Git；未执行全局 untracked clean。

## 19. Push Result

目标为 `origin/codex/refactor-bsp-drv-fml-apl`，使用 `git push -u origin codex/refactor-bsp-drv-fml-apl`，不 force-push。最终实际远端结果记录在一次性交付消息中。

## 20. Final Verdict

PASS
