# BMS V1

## 项目简介

BMS V1 是一个面向嵌入式工程学习与复盘的 13S NMC 电池管理系统。目标平台为 STM32F103C8T6 + TI BQ7694003，使用 FreeRTOS V11.1.0、Keil MDK5 / ARMCC5 5.06u7。当前版本已经完成系统功能、冻结安全架构、确定性与随机压力回归、目标编译和工程文档的整体闭环。

当前状态：**BMS V1 已完成整体工程闭环并建立正式 Release Baseline。**

## 系统配置

| 项目 | 配置 |
|---|---|
| MCU / 时钟 | STM32F103C8T6 / 72 MHz |
| AFE / 拓扑 | BQ7694003 / 13S |
| 电池模型 | NMC，20 Ah，Rsense 4 mΩ；正电流表示充电 |
| RTOS | FreeRTOS V11.1.0，heap_4，7 个任务 |
| 工具链 | Keil MDK5，ARMCC5 5.06 update 7 build 960 |
| CAN | 500 kbit/s，PA11/PA12，TX `0x180..0x185`，服务 RX `0x280` |
| 调试串口 | USART1 PA9/PA10，115200 8N1，只读 `BMS1` 快照 |
| Flash | 61 KiB 应用区；A/B 页 `0x0800F800` / `0x0800FC00` |

## 核心功能

- BQ7694003 上电、CRC 通信、寄存器配置、校准与 13S 完整采样；
- OV / UV / OC / 温度保护、硬件保护源聚合和方向性 CHG/DSG inhibit；
- XREADY 分阶段恢复、generation 绑定的校准交接和恢复后首帧验证；
- FET 请求、有效命令、寄存器回读与 transaction revision 闭环；
- 单调 heartbeat generation、任务健康监督与 StateTask 独占 IWDG 喂狗；
- 库仑计数 SOC、单节均衡、CAN 周期诊断与受限服务帧；
- Flash A/B 页、CRC32、newest-valid 选择与 commit-last 掉电保护；
- 1 秒 UART 诊断快照和每次最多 8 B 的非阻塞发送。

## 软件架构

ProtectTask 持有 HW/AFE fault 与运行期 XREADY W1C；StateTask 持有软件保护、DATA_STALE 与 RTOS_HEALTH；Recovery Coordinator 持有技术恢复过程；FET Manager 消费这些权威快照与 State 运行意图，是调度器启动后唯一写 SYS_CTRL2 CHG/DSG 的模块。Measurement、State decision、recovery、calibration 与 hardware recovery request 通过 `sample_sequence`、`afe_generation`、generation/revision identity 拒绝旧证据。BalanceTask 是调度器启动后唯一写 CELLBAL 的任务，`BMS_Data` 聚合只承担诊断职责。

## FreeRTOS 任务模型

| 任务 | 核心职责 | 周期 / 唤醒方式 |
|---|---|---|
| Sample | 形成并发布完整测量快照 | 250 ms |
| Protect | 处理 ALERT、SYS_STAT、CC 与硬件 fault | ALERT 通知 + 有界等待 |
| State | 状态分类、安全协调、FET service、健康监督和 IWDG | 最大有界等待 100 ms；安全事件可 urgent notification 提前唤醒 |
| SOC | 消费独立 CC 队列并更新 SOC | CC 队列 + 有界等待 |
| Balance | 仲裁并写入 CELLBAL | 1 s |
| CANRx | 消费服务帧但不绕过安全 ownership | RX 队列 + 有界等待 |
| CANTx | 10 ms 服务发送、100 ms 帧发布、1 s UART 快照 | 10 ms |

## 关键安全设计

- State 只描述运行状态，不直接等价于 FET 最终许可；
- CHG / DSG 由方向性 inhibit bitmap 分别仲裁，未知安全源默认双向禁止；
- Protect / State / Recovery 分别发布一致快照，FET Manager 不以诊断聚合代替安全权威；
- runtime XREADY 只有 ProtectTask 可以 W1C，恢复全过程保持 BOTH inhibit；
- `sample_sequence` 与 `afe_generation` 把安全决策绑定到同一测量生命周期；
- write → readback → revision confirm 防止寄存器写入期间的新安全输入被旧事务覆盖；
- StateTask 是唯一 IWDG 喂狗者，任何必需任务停止推进都会阻止继续喂狗；
- BalanceTask 是 CELLBAL 单写者，CAN 与 UART 没有修改安全策略或直接控制 MOS 的权限。

## 仓库结构

```text
APP/apl/                      启动编排、七任务、协议交接和 IRQ 入口
APP/fml/                      采样、保护、状态、恢复、SOC、均衡、协议与持久化
APP/bsp/                      STM32 板级原语、软件 I2C 与 BQ76940 驱动
APP/os/                       OS_* 接口、FreeRTOS 内核和 FreeRTOSConfig.h
APP/RTD/                      原样复制的 ST 标准外设库与 CMSIS 源码
APP/bms_main/main.c           唯一的 main 入口
APP/keil/                     ARMCC5 工程及其生成输出
tests/                        仿真镜像、构建脚本和架构验证
docs/                         架构、配置、验证、调试与学习输入文档
deliverables/architecture/    当前 APP 源码包架构决定
deliverables/simulation/      仿真里程碑与回归报告
deliverables/release/         正式 Release Baseline
deliverables/refactor/        发布基线后的分层与可维护性重构证据
tools/phase8/                 artifact trust-chain 工具与测试
```

`APP/` 可单独复制并在 Keil 中构建；工程源文件和头文件都在包内。`docs/reference/` 与 `docs/FreeRTOS/` 保留为只读参考副本，不参与生产编译。仿真报告中的 Simulator 代表一种可重复测试方法，不是项目完成度标签。

正式 Release Baseline 记录于 `dsh/project-finalization`；当前重构分支的目录、依赖关系与验证结论见 `deliverables/refactor/`。历史基线中的资源数字和 HEX 哈希属于当时的提交，不能直接当作当前分支的构建结果。

## 构建方法

环境：Windows PowerShell、Python 3、ARMCC5 5.06u7（默认 `D:\Keil_v5\ARM\Version5.06\bin`）、Keil uVision（默认 `D:\Keil_v5\UV4\UV4.exe`）。

```powershell
# 当前 Phase 9 core、continuation、race 与 50,000 次压力测试
& tests\build_phase9.ps1

# 当前 APP 包边界与分层检查
python tests\verify_architecture.py

# release trust-chain 工具验证
python tools\phase8\test_validate_blocker_artifact.py
```

当前回归证据由 `tests/Build/Phase9/` 和 `APP/keil/Build/` 生成，均不纳入 Git。`tests/build_phase8.ps1` 与旧 verifier 属于历史 Phase 8 门禁，其静态断言包含当时的目录布局和当时尚未接入的功能，不作为当前重构的验收入口。

也可在 uVision 打开 `APP/keil/BMS_V1.uvprojx`，选择 `BMS_V1` target 后执行 Clean/Rebuild。正式构建验收同时检查 0 errors / 0 warnings、Code/RO/RW/ZI、map/callgraph 与 HEX 一致性。

## 测试与验证

| 验证项 | Release 结果 |
|---|---:|
| Phase 9 core deterministic scenarios | 24 / 24 PASS |
| Continuation scenarios | 8 / 8 PASS |
| 总确定性场景 | 32 / 32 PASS |
| Targeted races | 3 / 3 PASS |
| Randomized stress | 50,000 iterations / 0 failures |
| Persistence | 512 transactions / 381 injected power cuts |
| ARMCC5 Clean/Rebuild | 0 errors / 0 warnings |

## 文档导航

- [APP 独立源码包架构](deliverables/architecture/APP_Source_Package_Architecture.md)
- [Architecture Overview](docs/architecture/BMS_V1_Architecture_Overview.md)
- [Module Inventory](docs/architecture/BMS_V1_Module_Inventory.md)
- [Task Ownership Matrix](docs/architecture/BMS_V1_Task_Ownership_Matrix.md)
- [Runtime Walkthrough](docs/architecture/BMS_V1_Runtime_Walkthrough.md)
- [Hardware Integration Guide](docs/bringup/BMS_V1_Hardware_Bringup_Guide.md)
- [Debugging Guide](docs/bringup/BMS_V1_Debugging_Guide.md)
- [Configuration Guide](docs/config/BMS_V1_Configuration_Guide.md)
- [Software Validation Matrix](docs/validation/BMS_V1_Software_Validation_Matrix.md)
- [Hardware Validation Matrix](docs/validation/BMS_V1_Hardware_Validation_Matrix.md)
- [Engineering Development Story](docs/learning/BMS_V1_Engineering_Development_Story.md)
- [Interview Review Guide](docs/learning/BMS_V1_Interview_Review_Guide.md)
- [Release Baseline](deliverables/release/BMS_V1_Release_Baseline.md)
- [Current Layered Architecture](docs/architecture/BMS_Layered_Architecture_Refactor.md)
- [Readability and Maintainability Refactor](deliverables/refactor/BMS_Readability_Maintainability_Refactor_Report.md)

## Release Baseline

**Engineering Closure: COMPLETE**
**Release Baseline: ESTABLISHED**

当前基线固定了完整功能集合、安全 ownership、构建方式、32 个确定性场景、3 个目标竞态、50,000 次随机压力测试和 ARMCC5 0/0 构建证据。版本结论见 [BMS V1 Release Baseline](deliverables/release/BMS_V1_Release_Baseline.md)。
