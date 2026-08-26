# BMS V1 Phase 3 报告

日期：2026-08-14  
阶段：BQ7694003 寄存器传输 / CRC framing / 校准 / 基础换算  
判定：`PHASE 3: COMPLETE`

## 证据边界

本报告区分三类阶段证据：ARMCC5 生产 target Rebuild、ARMCC5/Keil Simulator 对实际生产 BQ transport C 的 mock 执行、Python 独立 oracle 与静态审查；结论严格绑定对应执行项。

`docs/` 仅作为只读输入；本阶段没有修改 TI/ST/SPL/CMSIS 原文件，也没有在 `docs/` 中生成输出。

## 1. 输入的 Phase 2 Gate

Phase 3 只在 Phase 2 Hard Gate 与项目日志检查点完成后开始。精确输入为：

| 输入 | SHA-256 / 状态 |
|---|---|
| `deliverables/phase2/BMS_V1_Phase2_Report.md` | `4adbed1e30a6b60f8e0fddad1094d61f5de0bc0a48bf075ac40a585780581a34` |
| Phase 2 项目日志检查点 | Phase 3 开始前的 log revision `8bd13f7e00003fbe88c7741cd2d36de0351f5dbbb19e6166ddfd2d3904747dc6`，strict check PASS |
| Phase 2 production driver/config 精确 hashes | 由 `verify_phase3.py` 逐文件复核，PASS |
| Phase 1 App 公共模型精确 hashes | 由 `verify_phase3.py` 逐文件复核，PASS |

Phase 2 的 MCU、时钟、TIM3、GPIO、software-I2C、CRC helper 与 ARMCC5 基线均未漂移。

## 2. 新增 / 修改文件

新增生产文件：

- `firmware/Driver/bq76940_regs.h`
- `firmware/Driver/bq76940.h`
- `firmware/Driver/bq76940.c`

新增测试与门禁文件：

- `firmware/Tests/test_phase3.h`
- `firmware/Tests/test_phase3_main.c`
- `firmware/Tests/test_phase3_transport.c`
- `firmware/Tests/test_phase3_decode.c`
- `firmware/Tests/phase3_tests.sct`
- `firmware/Tests/phase3_simulator.ini`
- `firmware/Tests/verify_phase3.py`
- `firmware/Tests/Build/Phase3/*`（ARMCC5 objects、AXF、map、Simulator/verify logs）

修改：

- `firmware/User/main.c`：仅初始化 BQ transport context，不发起任何 BQ transaction。
- `firmware/Project/Keil/BMS_V1.uvprojx`：把 `bq76940.c` 加入 ARMCC5 target。
- `firmware/Project/Keil/BMS_V1.uvoptx`：Simulator 自动化入口切换到 Phase 3 测试脚本。

## 3. BQ transport architecture

```text
main (safe initialization only)
  └─ BQ76940_Init

BQ public transport
  ├─ ReadByte / WriteByte
  ├─ ReadBlock / WriteBlock
  ├─ ReadAdjacentU16
  ├─ ReadCalibration / DecodeCalibration
  └─ DecodeRaw14 / DecodeSigned16 / ConvertCellRawToMv
       ├─ crc8_bq76940 (Phase 2, pure C)
       └─ SoftI2C deferred-response API (Phase 2)
```

生产 BQ 层不直接访问 GPIO/TIM/SPL；所有总线操作经过 `SoftI2C_t`。没有 public unchecked RMW API，避免未来绕过 `SYS_CTRL2` 的 CHG/DSG single-writer 规则。

## 4. Device address semantics

依据 TI BQ769x0 Datasheet SLUSBK2I Rev.I 物理页 3 Table 5，以及 EVM Guide SLVU925C 物理页 11：

- BQ7694003 为 CRC-enabled、3.3 V REGOUT 变体；
- 7-bit address 为 `0x08`；
- wire write byte 为 `(0x08 << 1) | 0 = 0x10`；
- wire read byte 为 `(0x08 << 1) | 1 = 0x11`。

编译期断言锁定 7-bit 与 wire byte 的关系。`0x08/0x10/0x11` 与同数值寄存器地址语义不同，不混用。

## 5. Register source

`bq76940_regs.h` 的地址来自 TI 英文 Datasheet Rev.I 物理页 29–30 Register Maps：

- `0x00..0x0B`：SYS_STAT、CELLBAL1..3、SYS_CTRL1..2、PROTECT1..3、OV_TRIP、UV_TRIP、CC_CFG；
- `0x0C..0x29`：VC1_HI/LO 至 VC15_HI/LO；
- `0x2A/0x2B`：BAT；`0x2C..0x31`：TS1..3；`0x32/0x33`：CC；
- `0x50/0x51/0x59`：ADCGAIN1、ADCOFFSET、ADCGAIN2。

`CC_CFG=0x0B`、要求值 `0x19` 只定义为常量并由 mock write vector 验证；默认 `main` 不写它。没有编造 DEVICE_ID、未定义间隙或 reserved-bit policy。

## 6. Write framing

依据 Datasheet Rev.I 物理页 25–26 §8.3.1.4：

```text
single: S, W10(A), Wreg(A), Wdata0(A), Wcrc0(A), P
block : S, W10(A), Wreg(A), [WdataN(A), WcrcN(A)]..., P
```

首字节 CRC 域为 `[0x10, register, data0]`；后续每个 data 的 CRC 域只有 `[dataN]`，每个 CRC 域重新以 init 0 开始。address、register、每个 data、每个 CRC 都检查从机 ACK。任意失败均尝试 STOP，绝不报告 partial write success。

## 7. Read framing

```text
S, W10(A), Wreg(A), RS, W11(A),
Rdata0(master ACK), Rcrc0(master response), ..., P
```

pointer 阶段不发送 CRC。首个 read CRC 域为 `[0x11, data0]`，不包含 register；后续 CRC 域分别为 `[dataN]`。

## 8. Block framing

`ReadBlock`/`WriteBlock` 最大长度固定为 32 bytes，并拒绝零长度、超长和 `start_register + length - 1` 超过 `0xFF` 的自增回绕。

三字节写 exact trace 验证 CRC `86/C7/00`；三字节读 exact trace 验证 CRC `3C/8C/A5`。这两个测试的 expected bytes 是固定 golden constants，没有从被测 CRC C 实现动态生成。

## 9. ACK / NACK 规则

- read data 必须 ACK，才能取得随后的 CRC；
- 非末、正确 CRC 必须 ACK，以继续下一个 data；
- 最后一个正确 CRC 必须 NACK，再 STOP；
- CRC mismatch 必须 NACK 当前 CRC、立即尝试 STOP，并返回 `CRC_MISMATCH`；
- write CRC 被 BQ NACK 映射为 `CRC_REJECTED`；
- `I2C_NACK`、`I2C_TIMEOUT`、`I2C_ERROR` 三类保持可区分状态；同类底层原因在该公共层合并。

Phase 2 的 `ReadByteBegin()` 与 `SendReadResponse()` 分离接口使“读完 CRC 后再决定第九时钟 ACK/NACK”可实现。

## 10. CRC 处理

算法为 MSB-first、poly `0x07`、init `0x00`、non-reflected、无 final XOR。独立 Python bitwise oracle 与实际 C 都覆盖：

| 输入 | CRC |
|---|---:|
| ASCII `123456789` | `F4` |
| `10 0B 19` | `7A` |
| `10 04 18` | `BE` |
| `10 04 10` | `86` |
| `40` / `00` | `C7` / `00` |
| `11 12` / `34` / `56` | `3C` / `8C` / `A5` |
| `11 00` / `11 FF` | `42` / `B1` |
| `FF` | `F3` |
| `00 FF AA 55`（continuous sanity） | `1D` |

## 11. Atomic read

`ReadBlock` 先写入固定 32-byte staging buffer。只有全部 data/CRC、最终 ACK/NACK 和 STOP 成功后才 `memcpy` 到 caller buffer；任一失败都保持 caller buffer byte-for-byte 不变。

`ReadAdjacentU16` 只调用一次长度 2 的 block transaction。测试比较完整 exact trace，证明只有一组 START/RESTART/STOP；禁止 `HI STOP LO`。该原子性仅防止相邻寄存器撕裂，不代表多个独立 transaction 属于同一采样帧。

## 12. Calibration 公式

依据 Datasheet Rev.I 物理页 36–37 Tables 8-21/8-22：

```text
trim = ((ADCGAIN1 & 0x0C) << 1) | ((ADCGAIN2 & 0xE0) >> 5)
gain_uV_per_LSB = 365 + trim       // 365..396
offset_mV = signed two's-complement ADCOFFSET
```

offset 使用显式 `raw - 256` 解码负值，不依赖 `uint8_t` 转 `int8_t` 的实现行为。测试覆盖 trim 0/31 与多个中间值，并覆盖 offset `00/01/7F/80/81/FF -> 0/1/127/-128/-127/-1`。

## 13. Calibration struct

`BQ76940_Calibration_t` 含 `gain_uv_per_lsb`、`offset_mv`、`valid`。`ReadCalibration` 依次读取 ADCGAIN1、ADCOFFSET、ADCGAIN2 到局部变量，三次 transport/CRC 和 decode 全部成功后才一次提交；任一失败不修改 caller 原有 calibration。

该结构只属于内存 API，不作为 wire/persistent raw layout。

## 14. Basic conversion

14-bit cell raw 解码为：

```text
raw14 = ((HI & 0x3F) << 8) | LO
cell_uV = raw14 * gain_uV_per_LSB + offset_mV * 1000
cell_mV = (cell_uV + 500) / 1000       // 非负 nearest，half-up
```

计算使用有符号 64-bit 中间值，不使用 float。负电压、raw 超过 `0x3FFF`、校准无效或输出超范围均返回明确错误且不修改输出。

测试包括 `0x1800/380/+30 -> 2365 mV`、`0x1F10/380/+30 -> 3052 mV`、`0x1000/365/-128 -> 1367 mV`、`0x3FFF/396/+127 -> 6615 mV`、`0x0019/380/0 -> 10 mV`。TI Datasheet 物理页 35 的一处 prose 把 upper bits 写成 D11:8，但同页表格和物理页 29 register map 明确为 D13:D8；实现按权威位表使用 `0x3F`。

BAT 仍按 16-bit unsigned、CC 仅提供 16-bit two's-complement decode helper；本阶段没有把 CC 换算为 mA 或进行 SOC 积分。

## 15. Tests

### 15.1 Actual-C ARMCC5 Simulator

测试用 ARMCC5 5.06u7 build 960 编译并链接实际：

- `firmware/Driver/bq76940.c`
- `firmware/Driver/crc8_bq76940.c`
- `test_phase3_transport.c` 提供 trace-recording mock SoftI2C backend
- `test_phase3_decode.c`

再由 Keil Cortex-M3 Simulator 执行：

```text
PHASE3_TEST_COMPLETED=1
PHASE3_TEST_FAILURES=0
```

`phase3_tests.map` 同时出现 production `bq76940.o`/`crc8_bq76940.o`、mock SoftI2C symbols 和全部被测 BQ API。这是软件模拟执行，不是硬件 I2C/BQ 验证。

### 15.2 Static / independent oracle

命令：

```powershell
python firmware/Tests/verify_phase3.py
```

结果：exit 0，`PHASE3_STATIC_AND_EXECUTION_CHECKS: PASS`。脚本不调用 GCC；它检查 Phase 1/2 精确 hashes、ARMCC5 target、官方寄存器常量、framing/atomicity、独立 CRC/校准/换算 oracle、Simulator/map/AXF 新鲜度、production Rebuild 和 Phase 4 禁区。

## 16. Negative tests

实际 C mock 执行覆盖：

- 未初始化、NULL、zero length、length 33、register auto-increment overflow；
- write address/register/data/CRC NACK，含 mid-block data 与 CRC NACK；
- repeated-start、read address、read byte、master response、START/STOP failure；
- timeout/state error 状态传播；
- 中途 CRC mismatch 立即 NACK+STOP，不继续读取；
- CRC mismatch 后即使 NACK response 或 cleanup STOP 报错，仍保持显式 `CRC_MISMATCH` contract，并确认 STOP 被尝试；
- 任意 block/read/calibration 失败时 caller output 原子不提交；
- raw `0x4000`、负换算结果、invalid calibration 保持输出 sentinel 不变。

## 17. ARMCC5 build

最终命令使用 Clean + Rebuild：

```powershell
& 'D:\Keil_v5\UV4\UV4.exe' -cr `
  'D:\AI\Codex\Bms_shop\firmware\Project\Keil\BMS_V1.uvprojx' `
  -t 'BMS_V1' -j0 `
  -o 'D:\AI\Codex\Bms_shop\firmware\Project\Keil\Build\BMS_V1_Phase3_build.log'
```

结果：ARM Compiler 5.06 update 7 build 960，`Rebuild target 'BMS_V1'`，15 个 source/assembly units 全量重编译，`0 Error(s), 0 Warning(s)`。

Target 仍为 STM32F103C8、ARM-ADS、uAC6=0、`STM32F10X_MD;USE_STDPERIPH_DRIVER`、唯一 MD startup、IROM `0x08000000+0xF400`、IRAM `0x20000000+0x5000`。没有添加 `stm32f10x_i2c.c` 或整套 SPL。

## 18. Code / RO / RW / ZI

```text
Code=3164
RO-data=268
RW-data=32
ZI-data=1896
Total RO=3432 B
Total RW=1928 B
Total ROM=3464 B
```

production target 编译整个 `bq76940.c`；由于默认 `main` 不发 transaction，split sections 会从最终 firmware image 移除未引用 transport 函数。实际 transport 全路径由独立 test AXF 链接并执行，因此不能仅从 production ROM 增量推断测试覆盖。

关键证据 SHA-256：

| Artifact | SHA-256 |
|---|---|
| `firmware/Driver/bq76940_regs.h` | `0ef807d409fa68d97e713900e870ec429515a7a8bdfae3fc17747774855f602e` |
| `firmware/Driver/bq76940.h` | `af8aa66fd90a0cf29d1570879e7f4d8a18d369e0a9117343611f2bc8d4efbecb` |
| `firmware/Driver/bq76940.c` | `d6ee9ebbf395bb3b68b9d4d90b600d2c0505c8969237eb0b4205c35191bb0a69` |
| `firmware/Project/Keil/BMS_V1.uvprojx` | `7155ec5f13c67659613c5b850b3b5f1b4c8ee08068fb06eb5091edb029500294` |
| `firmware/Project/Keil/Build/BMS_V1_Phase3_build.log` | `f2f523dbd30c7722cc640c0d127dba5382608737e041865748e4b2b37eb6e22c` |
| `firmware/Project/Keil/Listings/BMS_V1.map` | `828a29ad60ca67d9aa14f43edddcc00a41810d073fc803f3376bb8f874dfd222` |
| `firmware/Project/Keil/Objects/BMS_V1.axf` | `ac7d7eb26d14edd54d9c191986e5ed30efd8848a585557dcb3e51276e236277e` |
| `firmware/Tests/Build/Phase3/phase3_tests.axf` | `54ab5df4708d4d5b3120dcee3ce95cbe9ef75ebadfe704005e63135f0288a10e` |
| `firmware/Tests/Build/Phase3/phase3_tests.map` | `24a5b0c554cfc9eedd55a8673b51ae687f07cc86dea7f447434d7cffaac4f533` |
| `firmware/Tests/Build/Phase3/phase3_simulator.log` | `95e6a301807a2e0b0f7bc4ebb101b9ab04bd8eb447b924c833f5f2e65e9e664c` |
| `firmware/Tests/Build/Phase3/verify_phase3.log` | `43cec17a018d6203b009543d2636e70fc1cb6cf9406ee1463d3f51b81f6192fa` |

## 19. Phase 1 / 2 / 3 增量比较

| 指标 | Phase 1 | Phase 2 | Phase 3 | P1→P2 | P2→P3 | P1→P3 |
|---|---:|---:|---:|---:|---:|---:|
| Code | 812 | 3084 | 3164 | +2272 | +80 | +2352 |
| RO-data | 252 | 268 | 268 | +16 | 0 | +16 |
| RW-data | 0 | 24 | 32 | +24 | +8 | +32 |
| ZI-data | 1856 | 1896 | 1896 | +40 | 0 | +40 |
| Total ROM | 1064 | 3376 | 3464 | +2312 | +88 | +2400 |
| RW+ZI | 1856 | 1920 | 1928 | +64 | +8 | +72 |

当前 1928 B RAM 仍不包含 FreeRTOS heap、七任务栈或队列；8 KiB heap 仍只是后续初始目标，不能据此宣称最终 SRAM 安全。

## 20. 阶段接口观测清单（历史）

以下条目是该阶段测试方法之外的物理接口观测维度，不作为当前项目状态。

- BQ7694003 BOM/suffix 与 3.3 V REGOUT 实物确认；
- TS1 wake、SHIP/POR、boot 后约 1 ms I2C / 约 10 ms startup 的板级时序；
- PB8/PB9 上拉、电平、RC、rise/fall time 和 nominal 100 kHz waveform；
- 真实 address/CRC/repeated-start/ACK/NACK；
- 9-clock+STOP generic bus clear 对目标板故障的有效性；
- calibration register 实读与 gain/offset 合理性；
- 真实 cell/BAT/CC/TS 数据、约 800 ms 初始 cell-valid 等待；
- brownout、AFE unpowered、SCL/SDA stuck、线束噪声和温度边界。

本报告不声称 probe 可证明芯片身份，也不声称任何硬件 transaction、波形或恢复已验证。

## 21. Phase 4 input conditions

Phase 4 只能消费本报告和项目日志中记录的精确 `VALIDATED` Phase 3 revision，并至少保持：

- Phase 3 build/test/static artifacts hashes 可复核；
- BQ transport 的 address、CRC、ACK/NACK、atomic commit contract 不漂移；
- 13S VC mapping 必须重新对 TI configuration table 与统一规格核对；
- 相邻 HI/LO 必须经单一 block transaction；
- calibration 必须先有效，再进行 measurement conversion；
- 真实 BQ/板级结果继续进入 Hardware Validation Gate，不用软件 mock 冒充。

进入 Phase 4 仍需用户明确命令；本轮不自行开始。

## 22. 本阶段明确未实现内容

未实现：13S sampling/VC mapping、pack measurement、ALERT/EXTI、SYS_STAT servicing、Protect/State/SOC/Balance tasks、CHG/DSG/FET control、protection configuration、CC→mA、coulomb/SOC integration、CAN、Flash A/B/SOC log、FreeRTOS objects/scheduler、IWDG、BQ default write/probe/wake sequence、硬件 validation。

没有 HAL、Cube、hardware-I2C、ArmClang、GCC firmware build 或 CMSIS-RTOS。

## Phase 3 Hard Gate

| Gate | Result |
|---|---|
| Phase 2 exact validated input | PASS |
| TI address/register/framing/calibration facts | PASS |
| single/block read/write exact traces | PASS |
| ACK/NACK / CRC mismatch / negative paths | PASS |
| atomic output and adjacent-pair transaction | PASS |
| calibration/decode/conversion actual-C tests | PASS |
| ARMCC5 Simulator actual-C execution | PASS |
| independent static/oracle gate | PASS |
| ARMCC5 production Rebuild 0/0 | PASS |
| no Phase 4 implementation | PASS |
| target-board/BQ/electrical validation | DEFERRED |

`PHASE 3: COMPLETE`

`READY FOR PHASE 4`

本判定仅表示软件 Phase 3 门禁通过；本轮到此停止，未进入 Phase 4。
