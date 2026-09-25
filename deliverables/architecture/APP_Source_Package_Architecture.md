# APP 独立源码包架构

状态：已实施。本文记录当前分层与交付边界；历史阶段资料保留在 `docs/` 和 `deliverables/`。

## 目录与职责

```text
APP/
  apl/       系统组装、七任务、应用协议交接和 IRQ 入口
  fml/       BMS 采样、保护、状态、恢复、SOC、均衡、协议和持久化
  bsp/       STM32 板级原语、软件 I²C 和 BQ76940 驱动
  os/        OS_* 门面、项目 FreeRTOSConfig.h 和 FreeRTOS 内核
  RTD/       原样复制的 ST 标准外设库与 CMSIS 源码
  bms_main/  仅 main.c
  keil/      uVision 工程文件及其生成输出目录
tests/       独立于交付源码包的构建脚本、仿真镜像和验证器
```

复制 `APP/` 即获得生产工程全部源文件、头文件和 Keil 工程。工程的源文件及 include 路径只落在 `APP/` 内；外部仅需要 Keil/ARMCC5 工具链。`docs/` 中的第三方代码是只读参考副本，不参与编译。项目说明和测试报告留在 `APP/` 外。

## 依赖方向

| 层 | 可以使用 | 不承担的职责 |
|---|---|---|
| `bms_main` | APL 入口 | 领域逻辑、硬件寄存器和任务细节 |
| `apl` | FML、BSP、OS 的公开接口 | FML 状态所有权、原生 FreeRTOS 调用 |
| `fml` | BQ/软件 I²C 的 BSP 接口、`OS_*` 同步原语 | MCU 寄存器、中断向量和任务创建 |
| `bsp` | RTD 中的 ST/CMSIS 原语 | APL/FML 业务规则 |
| `os` | FreeRTOS 内核 | APL/FML/BSP 头文件和业务规则 |
| `RTD` | 第三方自身头文件 | 项目自有改动 |

OS 是可直接调用的项目层。APL 和需要并发保护的 FML 模块调用 `OS_*`；原生 FreeRTOS 类型、对象创建及调度 API 收在 `APP/os/`。中断向量名、`main` 和 FreeRTOS 固定 hook 名由工具链约定，保留原名。

## 配置、命名与注释

- FML 策略及领域编译参数分别位于 `fml_policy.*`、`fml_config.h`；板级引脚、时钟和存储布局位于 BSP；`FreeRTOSConfig.h` 位于 OS。没有顶层 `config/`。
- 项目自有函数以 `APL_`、`FML_`、`BSP_` 或 `OS_` 表示所属层。领域类型仍使用已有 `BMS_`、`BQ76940_` 和 `SoftI2C_` 名称，避免改变数据模型含义。
- 项目自有函数定义前说明作用；全局变量和结构体成员在定义处说明用途。第三方源码及其版权声明原样保留。
- `APP/keil/Build/`、`Objects/`、`Listings/` 和 `tests/Build/` 是生成输出，由 Git 忽略。交付源码使用干净检出的 `APP/`。

## 迁移与核验

原 `firmware/DRV/` 按设备职责并入 BSP，FML 的逐模块目录展开为一层；原 `firmware/Tests/` 移到根目录 `tests/`。ST/CMSIS 复制到 `APP/RTD/ST/`，FreeRTOS 内核复制到 `APP/os/FreeRTOS/`；现有生产工程没有其它第三方源码。

从仓库外临时目录复制整个 `APP/` 后，Keil `BMS_V1` target 构建为 **0 错误、0 警告**，Code 54,232 B、RO 1,092 B、RW 384 B、ZI 16,520 B。`tests/verify_architecture.py` 检查路径闭包、分层、OS 门面和第三方文件未改动。Phase 9 仿真覆盖 24 个核心场景、8 个延续场景、3 个定向竞争场景和 50,000 次压力迭代；历史 Phase 8 的六个镜像也编译并执行通过。旧 Phase 8 硬门禁包含当时的目录与未集成状态断言，保留作历史资料，不作为当前架构验收入口。
