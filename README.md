# BMS V1

BMS V1 是一个面向嵌入式软件工程学习的 13S NMC 电池管理系统项目。目标平台为 STM32F103C8T6 + TI BQ7694003，使用 FreeRTOS V11.1.0、Keil MDK5 / ARMCC5 5.06u7。项目覆盖 software I2C、AFE startup/measurement、保护与状态机、FET arbitration、XREADY recovery、health/IWDG、SOC、均衡、CAN、Flash A/B persistence 与 UART bring-up telemetry。

当前状态：**Software / Simulation Release Candidate；Engineering Closure M3**。这不是量产、硬件认证或实车验证结论。真实 NTC/Rsense/测量精度、ALERT/I2C 波形、OV/UV/OCD/SCD、MOS conduction、CAN physical bus、IWDG timing、Flash brownout/endurance、thermal/EMI/ESD 均需 REAL_HW validation。

## Hardware / software target

| Item | Target |
|---|---|
| MCU / clock | STM32F103C8T6 / 72 MHz |
| AFE / topology | BQ7694003 / 13S |
| Simulation battery | NMC, 20 Ah, Rsense 4 mΩ；positive current=charge |
| RTOS | FreeRTOS V11.1.0, heap_4, seven tasks |
| Toolchain | Keil MDK5, ARMCC5 5.06 update 7 build 960 |
| CAN | 500 kbit/s, PA11/PA12, TX `0x180..0x185`, service RX `0x280` |
| Debug UART | USART1 PA9/PA10, 115200 8N1, read-only `BMS1` line |
| Flash | 61 KiB app IROM；A/B pages `0x0800F800` / `0x0800FC00` |

## Architecture in one paragraph

ProtectTask owns HW/AFE faults and runtime XREADY W1C；StateTask owns SW protection、DATA_STALE 与 RTOS_HEALTH；Recovery Coordinator owns technical readiness；FET Manager consumes their authoritative snapshots plus State operational intent and is the only scheduler-era SYS_CTRL2 CHG/DSG writer。Measurement、State decision、recovery、calibration 与 hardware recovery request 都通过 `sample_sequence`、`afe_generation`、generation/revision identity 拒绝旧证据。BalanceTask 是 scheduler-era CELLBAL sole writer，StateTask 是 IWDG sole feeder，`BMS_Data` aggregate 只用于诊断。

## Repository map

```text
firmware/App/                 application owners, tasks, managers, services
firmware/Driver/              BSP, software I2C, BQ transport/measurement/control
firmware/Config/              compile-time config, immutable policy, memory map
firmware/User/main.c          startup and integration entry
firmware/Project/Keil/        ARMCC5 target and build evidence
firmware/Tests/               production-C test images, Simulator runners, verifiers
docs/architecture/            architecture, ownership, runtime walkthrough
docs/bringup/                 hardware bring-up and symptom-driven debugging
docs/config/                  SIM -> REAL configuration guide
docs/validation/              software/hardware evidence matrices
docs/learning/                engineering stories and interview review
deliverables/simulation/      historical/current simulation reports
deliverables/release/         M3 software release baseline
tools/phase8/                 approved-artifact trust-chain tooling/tests
```

`docs/reference/` 与 `docs/FreeRTOS/` 是项目输入/reference copies，不是 M3 生成报告目录。当前 authoritative simulation report 是 `deliverables/simulation/BMS_V1_Simulation_RC_Report.md`；较早 Integration Report 是历史 milestone。

## Run the simulation/software regression

环境：Windows PowerShell、Python 3、ARMCC5 5.06u7（默认 `D:\Keil_v5\ARM\Version5.06\bin`）、Keil uVision（默认 `D:\Keil_v5\UV4\UV4.exe`）。

Phase 9 + continuation + 50,000-iteration stress：

```powershell
& firmware\Tests\build_phase9.ps1
```

Phase 4/6/7/8 test images与production Clean/Rebuild，再分别执行六个Simulator images：

```powershell
& firmware\Tests\build_phase8.ps1 -SkipSimulator
& firmware\Tests\run_phase8_split_simulators.ps1
```

`-SkipSimulator` 是构建阶段诊断开关，因此脚本会明确返回 `PHASE8 HARD GATE NOT_EXECUTED`；software RC 的 lower-phase runtime evidence来自随后执行的split runner。这样避免本机 uVision 对同一进程连续多次 `LOAD` 的已知挂起，不减少任何一个image。Frozen `verify_phase8.py` 仍是real-hardware artifact gate，当前因缺少approved NTC/AFE artifacts保持 BLOCKED，不应改成接受SIM参数。

Trust-chain与current simulation verifier：

```powershell
python tools\phase8\test_validate_blocker_artifact.py
python firmware\Tests\verify_phase9.py
```

输出证据位于 `firmware/Tests/Build/Phase8/`, `firmware/Tests/Build/Phase9/` 与 `firmware/Project/Keil/Build/`。

## ARMCC5 Clean/Rebuild

脚本化入口是上面的 `build_phase8.ps1` production stage；也可在 uVision 打开 `firmware/Project/Keil/BMS_V1.uvprojx`，选择 target `BMS_V1` 后执行 Clean/Rebuild。发布目标是 0 errors / 0 warnings，并记录 Code/RO/RW/ZI 与 map/callgraph；不能只运行 Build 而沿用旧object。

## Documentation start points

- [Architecture Overview](docs/architecture/BMS_V1_Architecture_Overview.md)
- [Module Inventory](docs/architecture/BMS_V1_Module_Inventory.md)
- [Task Ownership Matrix](docs/architecture/BMS_V1_Task_Ownership_Matrix.md)
- [Runtime Walkthrough](docs/architecture/BMS_V1_Runtime_Walkthrough.md)
- [Hardware Bring-up Guide](docs/bringup/BMS_V1_Hardware_Bringup_Guide.md)
- [Debugging Guide](docs/bringup/BMS_V1_Debugging_Guide.md)
- [Configuration Guide](docs/config/BMS_V1_Configuration_Guide.md)
- [Software Validation Matrix](docs/validation/BMS_V1_Software_Validation_Matrix.md)
- [Hardware Validation Matrix](docs/validation/BMS_V1_Hardware_Validation_Matrix.md)
- [Engineering Development Story](docs/learning/BMS_V1_Engineering_Development_Story.md)
- [Interview Review Guide](docs/learning/BMS_V1_Interview_Review_Guide.md)
- [Software Release Baseline](deliverables/release/BMS_V1_Software_Release_Baseline.md)

## Current evidence and next step

当前 RC baseline包含 32/32 deterministic scenarios、3/3 targeted races、50,000 randomized iterations（600,000,000 ms equivalent simulated state time）、512 persistence transactions、381 injected power cuts、0 failures，以及 ARMCC5 target 0/0 build。M3新增的read-only UART telemetry同样进入target build与static verifier。

下一步不是继续添加产品功能，而是按 Hardware Bring-up Guide 从 Stage 0 power safety、MCU、UART、I2C waveform、AFE communication开始逐项填充hardware validation matrix。独立项目级review应另行执行；本README不声称 independent gate PASS。
