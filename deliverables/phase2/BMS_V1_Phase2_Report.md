# BMS V1 Phase 2 报告

日期：2026-08-14  
阶段：MCU BSP / TIM3 / Software I2C / BQ76940 CRC  
判定：`PHASE 2: COMPLETE`

## 证据边界与输入

本阶段按以下输入执行，冲突以勘误表和 Software Gate 为准：

| 输入 | SHA-256 |
|---|---|
| `docs/spec/BMS_V1_统一项目方案_软件设计规格.md` | `7e71125d6acdad5ba3d8203a5bd9cff51168051c895cfd31b76f663266576472` |
| `deliverables/review/BMS_V1_规格勘误表.md` | `65ec79225425fd23cd74b7e988276b76d2d876d99101f5606295aca609d85c1e` |
| `deliverables/review/BMS_V1_Software_Implementation_Gate.md` | `635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867` |
| `deliverables/phase1/BMS_V1_Phase1_Report.md` | `c444a4fe0db8401ba22b9515aef22d1c1e17f5057a872140698bc269859ea0b9` |
| 本轮执行附件 | `44efc7c11c9205b9ea704eb5ef366634243e36c997a90c7162d8864ba0427db3` |

芯片事实复核使用仓库内 ST `DS5319 Rev.20`、`RM0008 Rev.21`、`ES096 Rev.15`，以及 TI `BQ769x0 Datasheet SLUSBK2I Rev.I`。`docs/` 下的官方资料、SPL 与 CMSIS 副本均保持只读。

测试证据分为三类：ARMCC5 生产目标 Rebuild、Keil Cortex-M3 Simulator 对实际生产 C 模块的执行、Python 独立 oracle/静态检查。没有目标板、BQ7694003、示波器或逻辑分析仪证据，因此所有电气和时序实测结论均为 `HARDWARE VALIDATION REQUIRED`。

## 1. 创建 / 修改文件

新增：

- `firmware/Driver/bsp_clock.h/.c`
- `firmware/Driver/bsp_gpio.h/.c`
- `firmware/Driver/bsp_timer.h/.c`
- `firmware/Driver/soft_i2c.h/.c`
- `firmware/Driver/crc8_bq76940.h/.c`
- `firmware/Tests/test_phase2.h`
- `firmware/Tests/test_phase2_main.c`
- `firmware/Tests/test_phase2_crc.c`
- `firmware/Tests/test_phase2_soft_i2c.c`
- `firmware/Tests/phase2_tests.sct`
- `firmware/Tests/phase2_simulator.ini`
- `firmware/Tests/verify_phase2.py`
- `firmware/Tests/Build/Phase2/*`（ARMCC5 test objects、AXF、map、日志）

修改：

- `firmware/Config/bms_config.h`
- `firmware/User/main.c`
- `firmware/Project/Keil/BMS_V1.uvprojx`
- `firmware/Project/Keil/BMS_V1.uvoptx`（自动化 Simulator 初始化文件选择）

Phase 1 的六个 `firmware/App` 公共模型文件逐文件 SHA-256 均与 Phase 1 基线一致。

## 2. BSP 架构

依赖方向为：

```text
main
 ├─ bsp_clock  ── STM32 RCC/SPL
 ├─ bsp_gpio   ── STM32 GPIO/SPL
 ├─ bsp_timer  ── STM32 TIM/RCC SPL
 └─ soft_i2c   ── injected line/time callbacks
       └─ production callbacks: bsp_gpio + bsp_timer

crc8_bq76940   ── pure C, no GPIO/SPL dependency
```

正式 target 仅新增 `stm32f10x_rcc.c`、`stm32f10x_gpio.c`、`stm32f10x_tim.c`。未加入 USART、`stm32f10x_i2c.c` 或整套 SPL；仍只选择 `startup_stm32f10x_md.s`。

## 3. Clock verify

`BSP_Clock_Verify()` 读取真实 RCC 状态，并同时检查：

- `HSEON=1`、`HSERDY=1`；
- `PLLON=1`、`PLLRDY=1`；
- `SW=PLL` 且 `SWS=PLL`；
- `PLLSRC=HSE`、`PLLXTPRE=/1`、`PLLMUL=x9`；
- `HPRE=/1`、`PPRE1=/2`、`PPRE2=/1`；
- SPL 读回计算为 SYSCLK/HCLK/PCLK1/PCLK2 = 72/72/36/72 MHz。

失败会在 GPIO/TIM3/I2C 初始化之前进入 safe idle。`RCC_GetClocksFreq()` 是寄存器与编译期 `HSE_VALUE` 的计算结果，不是频率实测。

残余边界：只读 vendor `system_stm32f10x.c` 在 HSE 失败时仍为空分支，并且 PLLRDY/SWS 的 pre-main 等待没有本阶段新增的超时。`BSP_Clock_Verify()` 只能处理已经到达 `main()` 的错误树；本阶段没有修改/复制 vendor 文件，因此不能声称 H-12 的 pre-main bounded startup 已完全闭环。

## 4. TIM3 计算

```text
PCLK1       = 36 MHz
PPRE1       = /2
TIM3 input  = 2 × PCLK1 = 72 MHz
PSC         = 71
CK_CNT      = 72 MHz / (71 + 1) = 1 MHz
ARR         = 0xFFFF
1 count     = 1 µs
wrap        = 65.536 ms
```

初始化使用 SPL、显式 update event、counter=0、清 UIF 后启动；不使用 SysTick。短时差使用显式 `uint16_t` 模减，`0xFFFE -> 0x0003` 得 5 µs。长延时按最大 32767 µs 分块；初始化状态和 CPU spin guard 可在 TIM3 停钟时有界失败，而不是永久等待。

## 5. Software I2C line model

PB8=SCL、PB9=SDA，均配置为 `GPIO_Mode_Out_OD`。切换为输出前先写 BSRR 释放两线：

- HIGH：写 1，表示释放开漏 NMOS，不表示主动驱高；
- LOW：写 0，表示主动下拉；
- line read：始终读 GPIO IDR 的物理电平，而不是 ODR shadow。

外部上拉、电压、上升时间和总线电容属于 Hardware Validation Gate。

## 6. I2C API

公共 API 覆盖 `Init / WaitBusIdle / Start / RepeatedStart / Stop / WriteAddress / WriteByte / ReadByteBegin / SendReadResponse / ReadByte / RecoverBus`。

关键冻结接口是两步读：`ReadByteBegin()` 只采 8 位，`SendReadResponse()` 在调用者检查数据或 CRC 后再发第九时钟 ACK/NACK。这允许后续 transport 对非末有效 CRC 发 ACK、对最终 CRC 或错误 CRC 当场发 NACK；没有把 ACK/NACK 语义塞进易反转的 bool。

状态区分 invalid argument、not initialized、state error、SCL/SDA stuck、timeout、address NACK、data NACK、recovery failed。

## 7. Timeout

每次 SCL release 都读回物理 SCL，并以 TIM3 16-bit 时间差和独立迭代 guard 双重限制；bus-free 等待同样有界。`delay_us` backend 返回 bool，TIM3 未初始化或停钟会映射为 `SOFT_I2C_STATUS_TIMEOUT`。

Stop/Recover 的所有错误路径均进入 cleanup：无条件释放 SCL/SDA、清 `started` 与 `read_response_pending`，再返回原始错误。测试覆盖 delay backend failure 后不遗留主机主动拉低。

## 8. Recovery

恢复流程：释放 SDA/SCL、确认 SCL 可达高、最多 9 个 SCL pulse、生成 STOP、最终确认两线均高。所有 SCL release 与 delay 均受限。

该流程只是 generic I2C bus-clear strategy，不是 TI/BQ76940 器件级恢复保证。AFE 未供电、SHIP/POR 状态、外部上拉/RC、硬短路或板级耦合仍可能失败，必须实板验证。

## 9. CRC 算法

`crc8_bq76940` 为独立纯 C 模块：MSB-first、polynomial `0x07`、initial value `0x00`、non-reflected、无 final XOR。每个 TI 定义的 CRC field 都重新从 0 开始。

公开 helper 区分 first write、subsequent byte、first read，避免把 register address 错误加入 read CRC。

## 10. CRC framing vectors

独立 Python bitwise oracle 与 ARMCC5 Simulator 中实际 C 模块均验证：

| 语义 | 输入 | CRC |
|---|---|---:|
| 标准 check | ASCII `123456789` | `F4` |
| CC_CFG single write | `10 0B 19` | `7A` |
| first write | `10 04 18` | `BE` |
| block first | `10 04 10` | `86` |
| subsequent | `40` | `C7` |
| subsequent | `00` | `00` |
| first read | `11 12` | `3C` |
| subsequent | `34` | `8C` |
| subsequent | `56` | `A5` |
| independent continuous sanity | `00 FF AA 55` | `1D` |

TI framing冻结为：first write=`[0x10, reg, data0]`，later write=`[dataN]`；first read=`[0x11, data0]`（不含 reg），later read=`[dataN]`。

## 11. Tests

### 11.1 ARMCC5 Simulator execution

`test_phase2_soft_i2c.c` 把实际 `firmware/Driver/soft_i2c.c` 链接到 mock line/time backend；`test_phase2_crc.c` 同样链接实际 CRC 模块。使用 ARMCC5 编译、ARM linker 生成 AXF，再由 Keil Cortex-M3 Simulator 执行，结果：

```text
PHASE2_TEST_COMPLETED=1
PHASE2_TEST_FAILURES=0
```

覆盖：参数/状态、START/RESTART/STOP、address/data ACK 与 NACK、caller-controlled read ACK/NACK、clock stretch、SCL stuck、SDA stuck、recovery success/failure、9-pulse 上限、delay timeout cleanup、16-bit wrap、全部 CRC vectors。日志不存在 debugger error。

证据：`firmware/Tests/Build/Phase2/phase2_simulator.log`。这是真实生产 C 的软件模拟执行，不是硬件 I2C、BQ 或波形验证。

### 11.2 Static/oracle gate

命令：

```powershell
& 'C:\Users\Tmdytnl\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' firmware/Tests/verify_phase2.py
```

结果：exit 0，`PHASE2_STATIC_AND_EXECUTION_CHECKS: PASS`。检查 target/toolchain/memory、Phase 1 App hashes、clock/TIM/GPIO/I2C 边界、独立 CRC oracle、Simulator 完成标志、Rebuild 证据、无 HAL/Cube/RTOS/硬件-I2C/Phase 3 文件。

## 12. Keil build

最终命令使用 Clean + Rebuild，防止旧 object 满足 Gate：

```powershell
& 'D:\Keil_v5\UV4\UV4.exe' -cr `
  'D:\AI\Codex\Bms_shop\firmware\Project\Keil\BMS_V1.uvprojx' `
  -t 'BMS_V1' -j0 `
  -o 'D:\AI\Codex\Bms_shop\firmware\Project\Keil\Build\BMS_V1_Phase2_build.log'
```

结果：ARM Compiler 5.06 update 7 build 960，`Rebuild target 'BMS_V1'`，全部 14 个 source/assembly unit 重新翻译，`0 Error(s), 0 Warning(s)`。

Target 仍为 STM32F103C8、ARM-ADS、uAC6=0、`STM32F10X_MD;USE_STDPERIPH_DRIVER`、IROM `0x08000000+0xF400`、IRAM `0x20000000+0x5000`。

## 13. Program Size

```text
Code=3084
RO-data=268
RW-data=24
ZI-data=1896
```

`fromelf --info=totals`：Total RO=3352 B，Total RW=1920 B，Total ROM=3376 B。Map/scatter 继续证明正式 link region 为 61 KiB app Flash 与 20 KiB SRAM。

关键证据 SHA-256：

| Artifact | SHA-256 |
|---|---|
| `firmware/Project/Keil/BMS_V1.uvprojx` | `4851d8e4b56d7a2dab744a7508805d681c9ab0c60af35cf26b269a1a89325d99` |
| `firmware/Project/Keil/Build/BMS_V1_Phase2_build.log` | `36c0c46d2074592c7913f904efcd72193c3ab56e42612e50bfaa459875b08f79` |
| `firmware/Project/Keil/Listings/BMS_V1.map` | `d2eab79cd11602409d952252dcac7ade4447b088a7747bdb59702508ebfb2846` |
| `firmware/Project/Keil/Objects/BMS_V1.axf` | `d7b4e3cd1b1021366473155227accf849b6294598c2f612526f1e6fb6d9a89d4` |
| `firmware/Tests/Build/Phase2/phase2_tests.axf` | `799d09a798d5127b36e45e36b30e540cf3794c56da5b2ee2e8bd24e021973a4f` |
| `firmware/Tests/Build/Phase2/phase2_simulator.log` | `ef8e3c735f6bf3b7635d8234089aa9d33ae45bd614fc27cf7cc7485433186bb3` |
| `firmware/Tests/Build/Phase2/verify_phase2.log` | `3b78fdb340e5cadec61836bc6176ec7ad864cb9a36b3849dba3382648cccb364` |

## 14. RAM / ROM 增量

| 指标 | Phase 1 | Phase 2 | 增量 |
|---|---:|---:|---:|
| Code | 812 | 3084 | +2272 |
| RO-data | 252 | 268 | +16 |
| RW-data | 0 | 24 | +24 |
| ZI-data | 1856 | 1896 | +40 |
| Total ROM | 1064 | 3376 | +2312 |
| RW+ZI | 1856 | 1920 | +64 |

当前 1920 B RAM 已包含 startup MSP/C heap、C library workspace、Phase 1 snapshot 及 Phase 2 globals；还没有 FreeRTOS heap/任务/队列。8 KiB FreeRTOS heap 仍只是后续初始目标，不能与其中的动态 task stacks/RTOS objects 重复计数，也不能据当前 map 宣称最终 SRAM 已验证安全。

## 15. Hardware Validation TODO

统一状态：`HARDWARE VALIDATION REQUIRED` / `DEFERRED`。

- 8 MHz HSE 与 72/36/72 MHz 时钟树的实测；
- PB8/PB9 外部上拉、电平、RC、rise/fall time；
- software-I2C 100 kHz nominal 波形、setup/hold 与 clock stretching；
- BQ7694003 真实 ACK/NACK、CRC 和 repeated-start；
- SCL/SDA stuck、9-clock+STOP 的板级有效性；
- AFE unpowered、SHIP/POR、TS1 wake 与 brownout 场景；
- 线缆噪声、温度、电压及真实负载条件。

本报告不声称 `100 kHz waveform verified`，也不声称 `bus recovery verified on hardware`。

## 16. Phase 3 input

Phase 3 只允许消费本报告列出的精确 Phase 2 project/source/build/test revision。已冻结的输入是：

- 7-bit address `0x08`，wire write/read `0x10/0x11`；
- deferred read response API；
- per-byte BQ CRC helper；
- bounded Stop/Recover cleanup；
- ARMCC5 target 与 Phase 2 size baseline；
- 当前 Phase 2 Simulator/static PASS 证据。

Phase 3 仅可实现 BQ7694003 register transport、atomic adjacent read、calibration decode 与单 cell pure conversion；不得进入 13S sampling、ALERT/Protect/FET/balance/SOC/CAN/Flash/FreeRTOS/IWDG。

## Phase 2 Hard Gate

| Gate | Result |
|---|---|
| ARMCC5 Rebuild 0 Error / 0 Warning | PASS |
| CRC golden vectors | PASS |
| Software-I2C actual-C mock execution | PASS |
| timeout / stuck / bounded recovery / cleanup | PASS |
| physical line readback and TIM3 calculation | PASS (static/software scope) |
| no hardware-I2C implementation | PASS |
| no Phase 3+ behavior | PASS |
| board/BQ/waveform validation | DEFERRED — does not convert to software PASS |

`PHASE 2: COMPLETE`

本判定只授权随后开始 Phase 3；它不表示 Phase 3 已开始，也不表示硬件 Gate 已通过。
