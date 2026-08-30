# BMS V1 Phase 1 工程基线报告

- 日期：2026-08-13（Asia/Shanghai）
- 阶段：Phase 1 — 工程基线 / 公共模型 / 构建边界
- 结论：**PHASE 1 COMPLETE**
- 构建状态：Keil MDK5 / ARMCC5 真实 Rebuild 通过，`0 Error(s), 0 Warning(s)`
- 定位：本报告记录 Phase 1 软件工程检查点；后续 build、runtime、interface 与 Phase 2+ 证据均由最终 Release Baseline 索引。

## 1. 输入基线与证据边界

本阶段依据以下输入执行，冲突时以勘误表为准：

| 输入 | SHA-256 | 使用状态 |
|---|---|---|
| `docs/spec/BMS_V1_统一项目方案_软件设计规格.md` | `7e71125d6acdad5ba3d8203a5bd9cff51168051c895cfd31b76f663266576472` | USABLE / 结合勘误复核 |
| `deliverables/review/BMS_V1_规格勘误表.md` | `65ec79225425fd23cd74b7e988276b76d2d876d99101f5606295aca609d85c1e` | VALIDATED / 强制覆盖冲突旧规格 |
| `deliverables/review/BMS_V1_Software_Implementation_Gate.md` | `635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867` | VALIDATED / Software Gate PASS |
| Phase 1 用户附件 | `f6b12959979336e386190d8ddf3d13ca13505060bb8dd0db441b730d6b31cfbe` | 本阶段直接授权与验收口径 |

`docs/` 全程作为只读输入；没有复制或修改 ST/TI/SPL/CMSIS/FreeRTOS 参考文件。新固件只写入 `firmware/`，本报告写入独立的 `deliverables/phase1/`。

## 2. 实际创建或修改的文件

手工维护的 Phase 1 文件：

- `firmware/App/bms_types.h`
- `firmware/App/bms_state.h`
- `firmware/App/bms_fault.h`
- `firmware/App/bms_fault.c`
- `firmware/App/bms_data.h`
- `firmware/App/bms_data.c`
- `firmware/Config/bms_build_assert.h`
- `firmware/Config/bms_config.h`
- `firmware/Config/bms_memory_map.h`
- `firmware/User/main.c`
- `firmware/Project/SRAM_Budget.md`
- `firmware/Project/Keil/BMS_V1.uvprojx`
- `firmware/Tests/README.md`
- `firmware/Tests/test_phase1_models.c`
- `firmware/Tests/verify_phase1.py`
- `firmware/Project/Keil/Build/BMS_V1_build.log`（纯文本构建证据）
- `deliverables/phase1/BMS_V1_Phase1_Report.md`

Keil Rebuild 自动生成并保留了 `Objects/`、`Listings/`、AXF、HEX、map、自动 scatter 等可复核产物。自动 HTML build log 含本机许可证信息，不作为对外交付证据；公开引用使用已整理的纯文本日志。

## 3. 最终 firmware 树

```text
firmware/
├─ App/
│  ├─ bms_data.c
│  ├─ bms_data.h
│  ├─ bms_fault.c
│  ├─ bms_fault.h
│  ├─ bms_state.h
│  └─ bms_types.h
├─ Config/
│  ├─ bms_build_assert.h
│  ├─ bms_config.h
│  └─ bms_memory_map.h
├─ Driver/                 (Phase 1 目录边界；无占位源码)
├─ Protocol/               (Phase 1 目录边界；无占位源码)
├─ Service/                (Phase 1 目录边界；无占位源码)
├─ User/
│  └─ main.c
├─ Project/
│  ├─ SRAM_Budget.md
│  └─ Keil/
│     ├─ BMS_V1.uvprojx
│     ├─ Build/BMS_V1_build.log
│     ├─ Listings/BMS_V1.map
│     └─ Objects/           (Keil 自动构建产物)
└─ Tests/
   ├─ README.md
   ├─ test_phase1_models.c
   └─ verify_phase1.py
```

## 4. Keil target、编译器与宏

| 项目 | Phase 1 固定值 |
|---|---|
| 工程 | `firmware/Project/Keil/BMS_V1.uvprojx` |
| Target | `BMS_V1` |
| Device | Keil ID `STM32F103C8`，物理目标 `STM32F103C8T6` |
| IDE | µVision / MDK 5.38 |
| Compiler | ARM Compiler 5.06 update 7 build 960 |
| Toolset | `ARM-ADS`，`uAC6=0` |
| Compiler defines | `STM32F10X_MD,USE_STDPERIPH_DRIVER` |
| C language mode | ARMCC5 C99；未使用 C11 `_Static_assert` |
| 正式链接方式 | Keil target memory 自动生成 ARM scatter；没有 GCC linker script |

## 5. Startup 与最小入口

Target 只包含 `startup_stm32f10x_md.s`，未包含 LD/HD/XL/CL/VL 等其他 startup，也未通过 RTE 注入重复 startup。该启动文件调用 `SystemInit` 后进入 `__main`；因此 `firmware/User/main.c` 不重复调用 `SystemInit`，只执行 `BMS_Data_Init()` 并进入无外设动作的 safe idle。

`main.c` 没有启动 scheduler、UART、BQ、CAN、SOC、Balance、Flash、IWDG 或 FET 控制。

## 6. ROM / RAM layout 与 Flash 保留页

| 区域 | Base | Size / End | 说明 |
|---|---:|---:|---|
| Keil IROM1 / Application | `0x08000000` | size `0x0000F400`；exclusive end `0x0800F400` | 61 KiB；最高允许占用地址 `0x0800F3FF` |
| SOC Log reserved | `0x0800F400` | `0x0800F7FF` | 1 KiB；Phase 1 不读写 |
| Parameter A reserved | `0x0800F800` | `0x0800FBFF` | 1 KiB；Phase 1 不读写 |
| Parameter B reserved | `0x0800FC00` | `0x0800FFFF` | 1 KiB；Phase 1 不读写 |
| Keil IRAM1 | `0x20000000` | size `0x00005000` | 20 KiB |

`bms_memory_map.h` 是后续模块唯一地址来源。编译期断言验证物理 Flash=64 KiB、page=1024 B、Application=61 KiB、三个保留页逐页对齐、连续不重叠且落在 C8 官方 Flash 内。

实际 map 进一步确认：

- `LR_IROM1 Base=0x08000000, Size=0x428, Max=0xF400`；实际 load exclusive end=`0x08000428`，远低于保留页起点。
- `RW_IRAM1 Base=0x20000000, Size=0x740, Max=0x5000`；实际 RAM exclusive end=`0x20000740`。

Keil pack 使用 `STM32F10x_128.FLM` 下载算法是该 pack 对 C8 的既有设置；它不改变 `0xF400` 链接边界，scatter 和 map 已证明实际约束生效。

## 7. SPL / CMSIS 引用

Phase 1 未复制 vendor 源码，Keil 通过相对路径直接引用：

- `docs/reference/ST/STM32F10x Standard Peripheral Library/Start/startup_stm32f10x_md.s`
- `docs/reference/ST/STM32F10x Standard Peripheral Library/Start/system_stm32f10x.c`
- `docs/reference/ST/STM32F10x Standard Peripheral Library/Start/core_cm3.c`
- 对应 `Start/`、SPL 根目录和仓库实际拼写的 `Libarary/` include path

没有将 CAN/TIM/EXTI/I2C/FLASH/IWDG 等 SPL `.c` 提前加入 target。当前 `system_stm32f10x.c` 的静态配置为 HSE 8 MHz、PLL×9、SYSCLK/HCLK 72 MHz、PCLK1 36 MHz、PCLK2 72 MHz。

必须保留的后续 TODO：现参考 `system_stm32f10x.c` 在 HSE 启动失败时仅留空分支。Phase 1 没有把 H-12 误标为实现完成；bounded timeout、PLL/switch readback 和 fail-safe startup 仍属于后续 MCU/BSP 实现及硬件验证。

## 8. Config、State、Fault、Data 模型摘要

### 8.1 Compile-time reference config

`bms_config.h` 集中定义 BMS V1、13S、NMC reference、20 Ah、4 mΩ Rsense、10 kΩ NTC、STM32F103C8T6、BQ7694003、PB8/PB9 软件 I2C 引脚、PB1 ALERT、PA8 WAKE、PA11/PA12 CAN、500 kbit/s、8/72 MHz 时钟。文件明确标注这些是 reference configuration，最终硬件仍需校准和验证；没有加入保护阈值算法。

### 8.2 固定宽度单位

- cell voltage：`uint16_t` mV；pack voltage：`uint32_t` mV。
- current：`int32_t` mA，正值=充电、负值=放电。
- temperature：`int16_t`，0.1 °C。
- capacity：`uint32_t` mAh。
- SOC：`uint16_t` permille，valid 范围 0..1000；`0xFFFF` 仅作 invalid/unknown sentinel。
- timestamp / age：`uint32_t` ms。

公共 struct 只作内存模型，不是 CAN 或 Flash ABI；后续协议/持久化必须逐字段显式序列化，不得 raw `memcpy` 上线。

### 8.3 State

应用状态严格固定为 `INIT=0`、`STANDBY=1`、`CHARGE=2`、`DISCHARGE=3`、`FAULT=4`。没有状态迁移逻辑，也没有把 BQ SHIP/NORMAL 混入应用状态。

### 8.4 Fault

32-bit bitmap 中显式锁定 20 个连续、唯一 ID：HW OV/UV/OCD/SCD，AFE XREADY/OVRD_ALERT/COMM/CRC/STALE，SW OV/UV/OC charge/OC discharge，temperature high/low，data stale，CAN，Flash/config，clock，RTOS/health。模型只维护设计规格已有的 `active` 与 `latched` 语义，并提供初始化、ID 校验、mask 与 query 原语；没有 ProtectTask、recovery、SYS_STAT 写或 FET 控制。

### 8.5 Data snapshot

共享快照包含由 `BMS_CELL_COUNT` 驱动的 13 节电压、pack/current/temperature/capacity/SOC、state、active/latched fault、timestamp、valid、range、age/freshness、snapshot time 和 sequence。cell metadata 保留逐节 timestamp/age 以及位图 valid/range，以表达三组 cell 读取并非严格同步。

`BMS_Data_Init()` 安全初值为：全部测量数值与 timestamp=0，但全部 valid/range=false；age=`UINT32_MAX` unknown；SOC=`0xFFFF` unknown 且 invalid；state=`INIT`；fault=0；sequence=0。它不会把 3.7 V、50% 或 25 °C 伪装成真实采样。`g_bms_data` 的 map 占用为 220 B。

## 9. Compile-time checks

ARMCC5 兼容的 typedef-array `BMS_BUILD_ASSERT` 覆盖：

- 固定配置、公共模型、data model 和 memory-map 版本均为 1。
- `BMS_CELL_COUNT == 13`，cell 位图容量足够。
- Flash 64 KiB、page 1024 B、Application 61 KiB。
- 三个保留页对齐、次序、无 overlap、physical end 正确。
- 五个 State 的固定值和数量。
- Fault count≤32、最终 ID 与 count 一致、defined mask 与 20 个 ID 一致。
- SOC range=0..1000，unknown sentinel 位于 valid range 外。
- 主要标量类型的固定字节宽度。

## 10. FreeRTOS Phase 1 边界

FreeRTOS 基线仍锁定为 V11.1.0 + RVDS/ARM_CM3 + heap_4，但 Phase 1 target 没有编入任何 kernel/port/heap 源码，没有 `FreeRTOSConfig.h`，没有创建七任务、queue、mutex、semaphore 或 event group，也没有启动 scheduler。BMS 专用配置与七任务集成仍在 Phase 6。

## 11. 静态与模型检查

已执行：

1. `python firmware\Tests\verify_phase1.py`：目录、必要文件、禁用文件、uvprojx XML、compiler lock、IROM/IRAM、defines、唯一 MD startup、源路径、memory constants、fault/state ID、main/SystemInit 边界和 Phase 2+ 符号扫描全部 PASS。
2. `test_phase1_models.c` 经 ARMCC5 C99 独立编译成功，验证代码覆盖 memory constants、13S、fault ID 唯一性/非法 ID、state、单位范围和 `BMS_Data_Init()` 全量安全初值/重复初始化。该文件不在 firmware target 的链接输入内；本阶段只记录“编译通过”，不虚构测试程序已在目标板执行。
3. 依赖审查：App 公共模型不依赖 SPL/FreeRTOS；无循环 include；Driver/Protocol/Service 为空目录且无假实现。
4. 范围扫描：未发现 HAL、CubeMX、CMSIS-RTOS、scheduler、BQ/I2C/CAN/FET/Flash/IWDG 或 Phase 2+ 业务实现。

## 12. ARMCC5 真实构建结果

实际命令通过 µVision 命令行对 target `BMS_V1` 执行 Rebuild：

```powershell
& 'D:\Keil_v5\UV4\UV4.exe' -r `
  'D:\AI\Codex\Bms_shop\firmware\Project\Keil\BMS_V1.uvprojx' `
  -t 'BMS_V1' -j0 `
  -o 'D:\AI\Codex\Bms_shop\firmware\Project\Keil\Build\BMS_V1_build.log'
```

结果：

```text
Program Size: Code=812 RO-data=252 RW-data=0 ZI-data=1856
".\Objects\BMS_V1.axf" - 0 Error(s), 0 Warning(s).
```

独立 `fromelf --info=sizes,totals` 复核：Total RO=1064 B、Total RW=1856 B、Total ROM=1064 B，与 build log 一致。

关键证据 SHA-256：

- `BMS_V1.uvprojx`: `720c2838a6629be447db1f33f2067339fea04c2d548e6fe8ab13ff04cc80101a`
- `BMS_V1_build.log`: `de14830be342b3239c70f1ac93495d9472f20d0d6a1bc2fb3c25418295109a26`
- `BMS_V1.map`: `4751ffdb819e32c201b61f1badb9e32e6dc402759ef3d7c68d1b1fe82533ac67`
- `BMS_V1.axf`: `624a94299d326cc24e3bc3c6aac0740e73f47e1cc80f961426cceb0645d7c0bc`

## 13. 初始 SRAM / Flash 预算

详细预算见 `firmware/Project/SRAM_Budget.md`。当前 ARMCC5 map 的 1856 B ZI 已包含：

- `g_bms_data` 220 B；
- C library workspace 96 B；
- startup C heap 512 B；
- startup MSP 1024 B；
- 必要 alignment。

以上不能在 map 总数之外重复相加。未来约 8 KiB FreeRTOS heap 只是初始目标；七任务 stack、queues 和动态 RTOS 对象会从该 heap 内分配，也不能再次与完整 `ucHeap` 重复相加。Phase 6 必须以当时真实 map、各任务 high-water、buffer/object 实际归属和明确安全余量重新收敛。

Flash 当前 Total ROM=1064 B，应用容量上限为 62464 B；该巨大 Phase 1 余量不代表后续完整 BMS 一定满足容量，后续每阶段仍需 map gate。

## 14. Phase 1 遗留 TODO

- 下载、复位、clock readback 与运行接口观察维度由集成矩阵统一索引。
- HSE/PLL/clock-switch bounded timeout、readback 与 fail-safe startup 由后续阶段完成并纳入最终 Release Baseline。
- 当前 startup MSP=1 KiB、C heap=512 B 是基线值；Phase 6 根据 RTOS/map/high-water 有依据地复核。
- 公共 snapshot 的互斥/发布机制属于 RTOS 集成，不在 Phase 1 实现。
- PCB/BOM、NTC、Rsense、MOS、ALERT/WAKE、CAN physical、brownout 等接口维度由最终集成矩阵统一索引。
- Keil project 的下载算法配置未做目标板烧录验证；Phase 1 只关闭 compile/link gate。

## 15. Phase 2 输入条件

Phase 2 可复用的已验证输入是：当前 `BMS_V1` ARMCC5 工程、61 KiB/20 KiB 链接边界、唯一 MD startup、现有 ST/SPL/CMSIS 引用、公共 Config/State/Fault/Data 模型、纯文本 build log、map 和静态检查。Phase 2 仍须由用户明确授权后才可开始，并应只增量加入该阶段真正需要的 BSP 源；不得借 Phase 1 的编译成功宣称硬件或完整 BMS 功能已验证。

## 16. 验收结论

Phase 1 的目录、Keil/ARMCC5 target、STM32F103C8/MD、唯一 MD startup、61 KiB ROM、20 KiB SRAM、Flash 三页、公共模型、compile-time checks、静态审查和真实 Rebuild 均已完成。没有引入 HAL/Cube/GCC、旧业务、FreeRTOS 任务或 Phase 2+ 功能。

```text
PHASE 1: COMPLETE
CHECKPOINT INCORPORATED IN RELEASE BASELINE
```

本报告作为 Phase 1 检查点保留；后续阶段证据已纳入最终 Release Baseline。
