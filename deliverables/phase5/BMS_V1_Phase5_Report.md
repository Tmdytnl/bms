# BMS V1 Phase 5 报告

日期：2026-08-15
阶段：BQ Protection Configuration / FET Arbitration / Internal Balancing Foundation
判定：`PHASE 5: COMPLETE` / `CANDIDATE FOR CODEX REVIEW`

## 1. Git baseline

| 项 | 值 |
|---|---|
| Parent branch | `dsh/phase4` |
| Parent commit | `5374be7`（phase4: restore immutable phase3 verifier） |
| 当前 branch | `dsh/phase5` |
| main | `83bd3be`（UNCHANGED） |
| phase3-validated | `83bd3be9e5c2ee20e8c9ef7b1ac4bf88847c8d1a`（UNCHANGED） |
| 远程 | 无 push；未创建 phase5-validated tag |

## 2. Preflight

- `dsh/phase4` 状态 clean，HEAD `5374be7`；
- `git switch -c dsh/phase5 dsh/phase4` 成功；
- Phase 4 candidate 作为本阶段输入基线，未返回 dsh/phase4 修改任何内容。

## 3. 证据边界

ARMCC5 生产 Clean+Rebuild、ARMCC5/Keil Simulator 对实际生产 control C 的执行、Python 独立 oracle 与静态审查。无目标板/BQ/示波器/负载证据；所有电气、波形、极性、热行为结论为 `HARDWARE VALIDATION REQUIRED / DEFERRED`（见 §22）。

## 4. 输入基线

| 输入 | 状态 |
|---|---|
| Phase 1 Config/State/Fault | VALIDATED，哈希复核 PASS |
| Phase 2 BSP/SoftI2C/CRC | VALIDATED，哈希复核 PASS |
| Phase 3 BQ transport/calibration | VALIDATED，哈希复核 PASS |
| Phase 4 measurement | CANDIDATE，哈希复核 PASS |

## 5. 新增 / 修改 production files

新增：

- `firmware/Driver/bq76940_control.h`（SHA-256 `08c1cfdf5f22490005d8defc9e0512cd04393f15afae056c1ece16e9448260ca`）
- `firmware/Driver/bq76940_control.c`（SHA-256 `9cee4902205c1c967fddef5188f79f3c842ef59d74847fa3f5216bcaba8f8dfa`）

修改：

- `firmware/Project/Keil/BMS_V1.uvprojx`：`Driver_Phase3` group 增量加入 `bq76940_control.c`（唯一 target 增量；新 SHA-256 `c13d76bb7c3c47f58e030fab199a18ecc2387d8849dac640df6c745a3173f174`）
- `firmware/Project/Keil/BMS_V1.uvoptx`：Simulator 入口切到 `phase5_simulator.ini`
- `firmware/Project/Keil/Listings/BMS_V1.map`、`Objects/BMS_V1.axf`、`Build/BMS_V1_Phase5_build.log`：Phase 5 重建产物

未修改：Phase 1-4 全部 production/测试文件、历史 verifier（verify_phase1/2/3 保持不可变快照）、`main.c`。

## 6. 新增测试文件

- `firmware/Tests/test_phase5.h`
- `firmware/Tests/test_phase5_main.c`
- `firmware/Tests/test_phase5_trip.c`
- `firmware/Tests/test_phase5_ocdscd.c`
- `firmware/Tests/test_phase5_fet.c`
- `firmware/Tests/test_phase5_cellbal.c`
- `firmware/Tests/verify_phase5.py`
- `firmware/Tests/phase5_tests.sct`
- `firmware/Tests/phase5_simulator.ini`
- `firmware/Tests/Build/Phase5/*`（objects、AXF、map、logs）

## 7. 架构

```text
bq76940_control (pure register encoding + arbitration; NO I2C, NO RTOS)
  ├─ OV_TRIP / UV_TRIP encode/decode      (TI eq. full=(V-offset)*1000/gain)
  ├─ OCD/SCD threshold+delay selection    (TI Tables 8-9..8-11, "not below")
  ├─ PROTECT1/2/3 composition             (bit fields)
  ├─ FET arbitration                      (errata H-04 single-writer)
  └─ CELLBAL composition/decode           (spec §5.1 explicit mapping)
```

本模块**不执行 I2C 事务**（`bq76940_control.c` 不含任何 Read/WriteBlock 调用）、不评估保护策略、不持有状态机——纯编码/组合/仲裁原语，供 Phase 6+ 的 RTOS 任务与 Phase 7 的 ProtectTask 调用。

## 8. OV / UV trip encoding（TI 8.3.1.2.1）

官方公式：

```text
full_code = (target_mv - calibration.offset_mv) * 1000 / gain_uv
trip      = (full_code >> 4) & 0xFF
```

- OV：14-bit full code 的 bit13:12 必须为 `10`（MSB 窗口检查），寄存器只存中间 8 位；
- UV：bit13:12 必须为 `01`；
- 4 个 LSB（OV=`1000`、UV=`0000`）是**解码时重建**用的固定 preset，不是编码约束（TI 原表如此，编码仅截取中间 8 位）；
- 64-bit 中间量，无 float；decode 反向重建 full code 并反算 mV。

**TI 官方示例验证**：OV 4.30V/GAIN=382/OFFSET=0 → full=11256 → trip=`0xBF` ✓；UV 2.50V → `0x99` ✓（均与 datasheet 一致）。

Golden：`4250mV→0xB7（decode 4251）`、`2800mV→0xCA（decode 2799）`、带 OFFSET=+30：`4250→0xB2`、`2800→0xC5`。MSB 窗口外（如 OV 1.0V、UV 4.5V）→ `RANGE_ERROR`。

## 9. OCD / SCD encoding（TI Tables 8-9..8-11）

选择策略："not below"（选择阈值 ≥ 请求值的**最小合法档位**），超出表上限 → `RANGE_ERROR`。

| 表 | RSNS=1 (mV) | RSNS=0 (mV) |
|---|---|---|
| OCD threshold (0x0..0xF) | 17..100 | 8..50 |
| SCD threshold (0x0..0x7) | 6..178 | 22..100 |
| OCD delay (0x0..0x7) | 8..1280 ms | — |
| SCD delay (0x0..0x3) | 70..400 µs | — |
| OV delay (0x0..0x3) | 1/2/4/8 s | — |
| UV delay (0x0..0x3) | 1/4/8/16 s | — |

Golden 覆盖：OCD 56mV→code7、17mV→code0、100mV→code15、101mV→RANGE；OCD delay 80ms→3、100ms→4、2000ms→RANGE；SCD 111mV→4、178mV→7、179mV→RANGE；SCD delay 100µs→1、401µs→RANGE；OV delay 2s→1、9s→RANGE；UV delay 4s→1、17s→RANGE。

## 10. PROTECT register composition

```text
PROTECT1 = RSNS(bit7) | SCD_D1:0(bit4-3) | SCD_T2:0(bit2-0)
PROTECT2 = OCD_D2:0(bit6-4) | OCD_T3:0(bit3-0)
PROTECT3 = UV_D1:0(bit7-6) | OV_D1:0(bit5-4)   [bits3-0 = TI 保留, 保持 0]
```

**发现 datasheet prose 笔误**：TI 应用示例称 "OCD 320ms(0x5) + 14.4A(code 0x0A) → PROTECT2=0x5B"，但按位定义 `(0x5<<4)|0x0A = 0x5A`（0x5B 会嵌入阈值 code 0xB=78mV，与 14.4A 的 0x0A=67mV 矛盾）。实现按**寄存器位定义**取 `0x5A`，报告记录该勘误（不修改 datasheet，仅记录）。

## 11. FET arbitration（errata H-04）

- `SysCtrl2WithFets`：从当前 SYS_CTRL2 值 + 期望 CHG/DSG 合成新字节，**保留 DELAY_DIS(bit7)、CC_EN(bit6)、保留位(bit5-2)**，仅写 bit0/bit1；
- `ObserveFets`：从寄存器读提取实际 CHG/DSG；
- `ApplyInhibits`：fault inhibit 强制 FET OFF（覆盖 desire）；
- 所有模块只提交 request/inhibit，禁止直接写 FET bit（single-writer 规则的编码基础）。

Golden：`0x40+CC_EN + CHG/DSG on → 0x43`、`all off → 0x40`、`0xFF 只清 CHG → 0xFE`、`0xC0 + CHG on → 0xC1`。

## 12. Internal balancing（spec §5.1 / TI Tables 8-4..8-6）

显式 logical-cell → CELLBAL bit 映射（13 项，CB9/CB14 永不使用）：

```text
Cell1..8 -> CB1..CB8, Cell9..12 -> CB10..CB13, Cell13 -> CB15
```

- `ComposeCellBal`：bitmap（bit n = Cell n+1）→ 三个寄存器字节；**V1 策略：最多 1 bit**（多 bit / 越界 → false）；
- `DecodeCellBal`：逆映射，非法物理位（CB9/CB14/保留）永不产生逻辑 cell；
- round-trip 13 项全验证；CB9 单独置位、CB14 单独置位 → decode 为 0。

Golden：`Cell1→0x01,0x00,0x00`、`Cell6→0x00,0x01,0x00`、`Cell9→0x00,0x10,0x00`、`Cell13→0x00,0x00,0x10`。

## 13. Output transactional contract

所有 API 均为纯函数：失败（NULL/越界/无效校准/窗口外）返回明确 `BQ76940_Status_t` 且**不修改输出**；成功才写输出。错误模型完全复用 Phase 3 `BQ76940_Status_t`，无第二套系统。

## 14. Software tests

实际生产 `bq76940_control.c` 由 ARMCC5 编译链接，Keil Cortex-M3 Simulator 执行：

```text
PHASE5_TEST_COMPLETED=1
PHASE5_TEST_FAILURES=0
P5_TRIP_FAILURES=0  P5_OCDSCD_FAILURES=0  P5_FET_FAILURES=0  P5_CELLBAL_FAILURES=0
```

覆盖：

- A. OV/UV trip：TI 官方示例（0xBF/0x99）+ 参考默认 + OFFSET 变体 + decode round-trip + MSB 窗口越界 + NULL + 无效校准；
- B. OCD/SCD/delay：全部选择表 golden + 超限 RANGE + NULL；
- C. PROTECT 组合：位域精确（含 0x5A vs 0x5B 勘误断言）；
- D. FET：CC_EN/DELAY_DIS 保留、单 FET 操作、inhibit 覆盖 desire、NULL 安全；
- E. CELLBAL：13 项映射 + round-trip + 多 bit 拒绝 + 越界 + CB9/CB14 永不暴露 + 非法位不 decode。

## 15. ARMCC5 Simulator 结果

`phase5_simulator.log`（SHA-256 `887a002c562df7c5c4aab182ab61672e5671ec0fc9b13a6e868c6b135cc93bdf`）：

```text
PHASE5_TEST_COMPLETED=1
PHASE5_TEST_FAILURES=0
```

`phase5_tests.map` 同时出现 production `bq76940_control.o` 与全部测试对象。软件模拟执行，非硬件验证。

## 16. Python oracle 结果

`verify_phase5.py`（SHA-256 `96f31a248d2c787cb3315261061124ec7c35639ba23db4b202f6938f7630bfdf`）exit 0，7 组 PASS：

```text
PHASE5_STATIC_AND_EXECUTION_CHECKS: PASS
```

log：`verify_phase5.log`（SHA-256 `461861d40dffb047178452aa4802062badc11acf7be099b272c44f40093ab0ee`）。

## 17. ARMCC5 Clean Rebuild

```powershell
& 'D:\Keil_v5\UV4\UV4.exe' -cr '...\BMS_V1.uvprojx' -t 'BMS_V1' -j0 -o '...\BMS_V1_Phase5_build.log'
```

结果：ARM Compiler 5.06 update 7 build 960，17 个 units 全量重编译，`0 Error(s), 0 Warning(s)`。build log SHA-256 `039ac907af8a5ae7f5d00590c3746a946ab95f41d1c6a3d2241b984e0ca58352`。

## 18. Code / RO / RW / ZI

```text
Code=3164  RO-data=268  RW-data=32  ZI-data=1896
Total RO=3432 B  Total RW=1928 B  Total ROM=3464 B
```

与 Phase 4 相同原因：main 不调用 control API，split sections 移除未引用函数；control 全路径由 Phase 5 test AXF 验证。

## 19. Phase 1 → 5 size 增量

| 指标 | P1 | P2 | P3 | P4 | P5 | P4→P5 |
|---|---:|---:|---:|---:|---:|---:|
| Code | 812 | 3084 | 3164 | 3164 | 3164 | 0 |
| RO-data | 252 | 268 | 268 | 268 | 268 | 0 |
| RW-data | 0 | 24 | 32 | 32 | 32 | 0 |
| ZI-data | 1856 | 1896 | 1896 | 1896 | 1896 | 0 |

P4→P5 增量为 0（split-sections 未引用移除，同 Phase 4 说明）。

## 20. 历史 regression

| Verifier | 结果 | 说明 |
|---|---|---|
| verify_phase1/2/3 | FAIL（历史快照，预期） | 不可变验收快照，不要求在后阶段工程直接 PASS；不修改 |
| verify_phase4.py | **PASS**（8/8） | Phase 4 candidate 自身门禁保持 PASS |
| verify_phase5.py | **PASS**（7/7） | `check_regression` 覆盖 Phase 1 App 6 文件、Phase 2 源 11 文件、Phase 3 源/报告 4 文件、Phase 4 源 2 文件精确哈希 + Phase 5 target 增量正确性 |

## 21. Phase 6 明确未实现内容

未实现：FreeRTOSConfig、RTOS objects（mutex/queue/sem/event group）、七任务、ALERT/EXTI、SYS_STAT service loop、ProtectTask/SampleTask/StateTask、保护策略求值（OV/UV/OCD/SCD 判定）、SYS_CTRL2 实际 I2C 写入、CELLBAL 实际写入、CAN、Flash、IWDG。无 HAL/Cube/hardware-I2C/CMSIS-RTOS。

## 22. Hardware Validation TODO

统一状态：`HARDWARE VALIDATION REQUIRED / DEFERRED`：

- 真实 BQ7694003 与 OV/UV/OCD/SCD 实际跳变行为；
- 4 mΩ Rsense 实值对 OCD/SCD 电流阈值换算的准确性；
- CHG/DSG 驱动极性、默认态、I2C 失效后物理关断边界（H-13）；
- CELLBAL 内部均衡电流、约 70% duty、相邻限制与温升（E-09）；
- 保护 delay 的真实计时（OV/UV/OCD/SCD）；
- TS1 wake、brownout、EMC/ESD、热行为。

Simulator/mock 只证明软件编码与仲裁逻辑。

## 23. Git commit list

```text
686cb85 phase5: add BQ protection encoding and safe FET/balance primitives
643df99 phase5: add control source to ARMCC5 target and rebuild
5b10ee9 phase5: add protection, FET and balance simulator tests with oracle
110716c phase5: add Phase 5 report (protection/FET/balance foundation)
[HEAD]  phase5: sync uvoptx file list and record full Clean Rebuild evidence
```

全部在 `dsh/phase5`；`main`、`phase3-validated`、`dsh/phase4` 未改变；无 push；未创建 phase5-validated tag。（HEAD 提交的精确 hash 以 `git log dsh/phase4..HEAD` 为准。）

## 24. git diff --stat dsh/phase4..HEAD

```text
 deliverables/phase5/BMS_V1_Phase5_Report.md        | 290 ++++++++++++
 firmware/Driver/bq76940_control.c                  | 517 +++++++++++++++++++++
 firmware/Driver/bq76940_control.h                  | 231 +++++++++
 firmware/Project/Keil/BMS_V1.uvoptx                |  38 +-
 firmware/Project/Keil/BMS_V1.uvprojx               |   5 +
 firmware/Project/Keil/Build/BMS_V1_Phase5_build.log|  28 ++
 firmware/Project/Keil/Listings/BMS_V1.map          | 217 +++++----
 firmware/Tests/Build/Phase5/phase5_simulator.log   |  16 +
 firmware/Tests/Build/Phase5/phase5_tests.map       | 121 +++++
 firmware/Tests/Build/Phase5/verify_phase5.log      |   8 +
 firmware/Tests/phase5_simulator.ini                |  13 +
 firmware/Tests/phase5_tests.sct                    |  13 +
 firmware/Tests/test_phase5.h                       |  11 +
 firmware/Tests/test_phase5_cellbal.c               | 117 +++++
 firmware/Tests/test_phase5_fet.c                   |  84 ++++
 firmware/Tests/test_phase5_main.c                  |  28 ++
 firmware/Tests/test_phase5_ocdscd.c                | 161 +++++++
 firmware/Tests/test_phase5_trip.c                  | 108 +++++
 firmware/Tests/verify_phase5.py                    | 338 ++++++++++++++
 19 files changed, 2252 insertions(+), 92 deletions(-)
```

## 25. Codex takeover review 注意事项

1. **纯编码层，无 I2C**：control.c 不含任何总线事务，ProtectTask/任务将在 Phase 6/7 调用这些原语并执行实际读写；review 时可独立验证编码正确性。
2. **PROTECT2 勘误**：TI prose 的 0x5B 与位定义 0x5A 冲突，按位定义实现（§10）。
3. **OV/UV LSB preset 语义**：编码不要求 full code 的 4 LSB 等于 preset（寄存器只存中间 8 位），decode 才重建 preset。
4. **FET 合成只保留 CHG/DSG 位**：CC_EN/DELAY_DIS 永远保留，single-writer 基础。
5. **平衡策略**：ComposeCellBal 强制最多 1 bit（V1），多 bit 直接拒绝。
6. **参考阈值**（4.2/4.25/3.0V、14.4A 等）是 reference configuration（E-08），真实目标由 Phase 9 保护策略/参数提供。
7. Phase 5 未创建 phase5-validated tag，最终接受权留待 Codex review。

## Phase 5 Hard Gate

| Gate | Result |
|---|---|
| 当前分支 dsh/phase5 | PASS |
| main / phase3-validated / dsh/phase4 未改变 | PASS |
| Phase 4 candidate 输入哈希 | PASS |
| OV/UV trip 官方公式（含 TI 示例） | PASS |
| OCD/SCD/delay 选择表 | PASS |
| PROTECT1/2/3 位域组合 | PASS |
| FET single-writer 仲裁 | PASS |
| CELLBAL 显式映射（CB9/CB14 跳过） | PASS |
| 输出事务性契约 | PASS |
| 独立 Python oracle | PASS |
| ARMCC5 Simulator actual-C | PASS |
| Production Clean Rebuild 0/0 | PASS |
| 无 Phase 6 功能 | PASS |
| 无 HAL / 无 hardware I2C / 无 RTOS | PASS |
| Git commits 完成 | PASS |
| Phase 5 report 完成 | PASS |
| 真实 BQ/板级验证 | DEFERRED |

`PHASE 5: COMPLETE`

`STATUS: CANDIDATE FOR CODEX REVIEW`
