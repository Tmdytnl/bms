# BMS V1 Phase 4 报告

日期：2026-08-14
阶段：BQ7694003 (13S) Measurement Layer
判定：`PHASE 4: COMPLETE` / `CANDIDATE FOR CODEX REVIEW`

## 1. Git baseline

| 项 | 值 |
|---|---|
| Baseline commit | `83bd3be9e5c2ee20e8c9ef7b1ac4bf88847c8d1a`（baseline: validated through phase 3） |
| Tag | `phase3-validated`（未修改） |
| 当前 branch | `dsh/phase4`（未 merge main） |
| main | UNCHANGED |
| phase3-validated | UNCHANGED |
| 远程 | 无 push |

## 2. Phase 3 preflight result

- `git branch --show-current` → `dsh/phase4`
- `git status` → clean
- `git rev-parse phase3-validated^{commit}` → `83bd3be9e5c2ee20e8c9ef7b1ac4bf88847c8d1a`
- `git merge-base --is-ancestor phase3-validated HEAD` → 0 (success)
- `verify_phase3.py`（Phase 4 修改前）→ **PASS**（exit 0）

## 3. 证据边界

本阶段证据分三类：ARMCC5 生产 target Clean+Rebuild、ARMCC5/Keil Simulator 对实际生产 measurement C 的 mock 执行、Python 独立 oracle 与静态审查。没有目标板、BQ7694003、示波器或真实电芯证据，因此所有电气、时序、极性、芯片身份和板级结论均为 `HARDWARE VALIDATION REQUIRED / DEFERRED`（见 §23）。

## 4. 新增 / 修改文件

新增生产文件：

- `firmware/Driver/bq76940_measurement.h`（SHA-256 `4883401eef11bd8fca01f9c20d9607e785b844fe82321120fad154fc5355f877`）
- `firmware/Driver/bq76940_measurement.c`（SHA-256 `69819eacae2ebdea8c911397522cfb26ae03c1e09e38d85bbd33c81e3db4e177`）

新增测试与门禁文件：

- `firmware/Tests/test_phase4.h`
- `firmware/Tests/test_phase4_main.c`
- `firmware/Tests/test_phase4_mapping.c`
- `firmware/Tests/test_phase4_measurement.c`
- `firmware/Tests/verify_phase4.py`
- `firmware/Tests/phase4_tests.sct`
- `firmware/Tests/phase4_simulator.ini`
- `firmware/Tests/Build/Phase4/*`（ARMCC5 objects、AXF、map、Simulator/verify logs）

修改：

- `firmware/Project/Keil/BMS_V1.uvprojx`：在 `Driver_Phase3` group 增量加入 `bq76940_measurement.c`（唯一 target 增量；新 SHA-256 `d829c41f0e813544bbda3a31eeca669168c05c2ccd8095efb1cc63b300745c1f`）
- `firmware/Project/Keil/BMS_V1.uvoptx`：Simulator 自动化入口切到 `phase4_simulator.ini`（Phase 2/3 同款做法）
- `firmware/Tests/verify_phase3.py`：历史 Phase-3 完成门禁的 forbidden 列表移除已授权的 `bq76940_measurement.c`（唯一改动，其余检查原样；详见 §22）

未修改：`soft_i2c.*`、`crc8_bq76940.*`、`bq76940.c/.h`、`bq76940_regs.h`、Phase 1 App 模型、`bms_memory_map.h`、startup、toolchain、`main.c`。

## 5. 最终 firmware tree 增量

```text
firmware/
├─ Driver/
│  ├─ bq76940.c/.h           (Phase 3, unchanged)
│  ├─ bq76940_regs.h         (Phase 3, unchanged)
│  └─ bq76940_measurement.h  (NEW)
│  └─ bq76940_measurement.c  (NEW)
└─ Tests/
   ├─ test_phase4.h          (NEW)
   ├─ test_phase4_main.c     (NEW)
   ├─ test_phase4_mapping.c  (NEW)
   ├─ test_phase4_measurement.c (NEW)
   ├─ verify_phase4.py       (NEW)
   ├─ phase4_tests.sct       (NEW)
   ├─ phase4_simulator.ini   (NEW)
   └─ Build/Phase4/          (NEW: objects, AXF, map, logs)
```

## 6. 13S logical mapping（最关键门禁）

显式逻辑 Cell → VC channel 表（logical index 0..12 = Cell 1..13）：

| Logical Cell | VC channel |
|---:|---:|
| 1..8 | VC1..VC8 |
| 9 | VC10 |
| 10 | VC11 |
| 11 | VC12 |
| 12 | VC13 |
| 13 | VC15 |

- **VC9、VC14 永不作为 logical cell 暴露**（编译/运行双测试断言）。
- 表长度编译期断言 `== BMS_CELL_COUNT == 13`。
- 依据：TI SLUSBK2I Rev.I Table 9-4 "13 Cells"（`CELL 13 short CELL 12 ... CELL 9 short CELL 8 ...`），与规格 §5 完全一致。
- 禁止隐含连续公式；测试全范围（0..255）探测非法索引返回 0。

## 7. VC register mapping

- VC1_HI=0x0C 起，每 channel 连续 HI/LO 占 2 字节，至 VC15_LO=0x29（Phase 3 `bq76940_regs.h` 已验证）。
- 30 字节 VC window = `VC1_HI..VC15_LO` 连续区间，编译期断言 `VC_WINDOW_BYTES == (VC15_LO - VC1_HI + 1) == 30` 且 `<= BQ76940_MAX_BLOCK_LENGTH`。

## 8. Cell read strategy

采用**单次 30-byte `BQ76940_ReadBlock(VC1_HI, ...)`**：

```text
S, W10(A), 0x0C(A), RS, W11(A), 30×data/CRC 读, P
```

- 30 个 data 字节与 30 个 CRC 全部由 Phase 3 transport 校验，STOP 成功后原子提交。
- 本地 staging decode 15 个 raw14，按 §6 映射取 13 个 logical，再统一换算。
- 测试断言：trace 中 **START=1、RESTART=1、STOP=1**（单 block transaction，非 13 次独立 pointer 事务），60 个 read-byte 事件。
- TI 官方 auto-increment 规则允许连续寄存器 block 读（SLUSBK2I 8.3.1.1 block read）。

## 9. Cell conversion

完全复用 Phase 3 已验证：

- `BQ76940_DecodeRaw14`（14-bit：`(HI&0x3F)<<8 | LO`）
- `BQ76940_ConvertCellRawToMv`（TI eq. 1：`V(cell)=GAIN×ADC+OFFSET`，64-bit 中间量，非负 nearest half-up）
- `BQ76940_Calibration_t`（GAIN 365..396 µV/LSB，OFFSET -128..127 mV）

**没有第二套 cell gain/offset 公式。** `calibration.valid==false` 时返回 `BQ76940_STATUS_CALIBRATION_INVALID` 且 caller 数组完全不变。

Golden（GAIN=380, OFFSET=+30）：`0x1800→2365 mV ... 0x2600→3727 mV`，13 项逐个断言（expected 由独立 Python oracle 产生，非被测 C 生成）。

## 10. BAT 官方公式及单位（TI eq. 9）

```text
BAT 寄存器 = (Σ cell ADC) / 4      （SLUSBK2I 8.3.1.1.6）
V(BAT)[µV] = 4 × GAIN[µV/LSB] × BAT_raw + #Cells × OFFSET[mV]×1000
pack_mv    = (V(BAT)[µV] + 500) / 1000     （非负 nearest half-up）
```

- `#Cells = BQ76940_MEASUREMENT_BAT_NUM_CELLS = BMS_CELL_COUNT = 13`（13S 参考配置）。
- 读取：单次 `BQ76940_ReadAdjacentU16(BAT_HI)`（atomic 2-byte transaction）。
- 全部 64-bit 中间量，无 float；输出 `uint32_t pack_mv`。
- Golden：`0x0000→390 mV`、`0x4E20→30790 mV`、`0xFFFF→100003 mV`（超出 65535 是合法的——BAT 是 16 位寄存器，修复了初版错误的 65535 上限）。
- 与 cell-sum 总压的关系：BAT 仅作诊断值（规格 §18），不做保护依据。

## 11. CC raw / current 公式及单位（TI eq. 3）

```text
CC raw = 16-bit two's complement（BQ76940_DecodeSigned16 复用）
CC Reading[µV] = CC_raw × 8.44 µV/LSB       （CCLSB typ, SLUSBK2I eq. 3）
I[mA] = CC_raw × 8440[nV/LSB] / Rsense[µΩ]  （4 mΩ → 4000 µΩ）
```

- 读取：单次 `BQ76940_ReadAdjacentU16(CC_HI)`（atomic）。
- 换算：64-bit 分子，**向零截断**整数除法（与规格 §19.4 表达式及 ARMCC5 C 语义一致），显式 overflow 检查，输出 `int32_t mA`。
- 非法参数：`Rsense==0` 或超大 → `RANGE_ERROR`；`polarity∉{+1,-1}` → `INVALID_ARGUMENT`。
- Golden（Rsense=4000 µΩ）：`0→0`、`1→2`、`32767→69138`、`-32768→-69140`、`-1→-2`、`10000→21100`、`-10000→-21100` mA。

## 12. current polarity 处理

- API 显式参数 `polarity ∈ {+1, -1}`：`I = polarity × CC_raw×8440/Rsense`。
- **不静默假定 raw 正 = charging**；参考板硬件极性未验证，`polarity` 由调用方按板级事实提供（默认约定 `+1` 仅作 reference 占位）。
- 硬件极性最终确认：`HARDWARE VALIDATION REQUIRED`。

## 13. TS1 处理

读取：单次 `BQ76940_ReadAdjacentU16(TS1_HI)`（atomic），14-bit raw。

换算（TI eq. 4/5）：

```text
VTSX[µV] = raw × 382 µV/LSB
RTS[Ω]   = (10000 × VTSX[µV]) / (3,300,000 − VTSX[µV])    （10 k 上拉，3.3 V REGOUT）
```

- 64-bit 中间量，向零截断，`VTSX >= 3.3 V` → `RANGE_ERROR`（分压模型外）。
- Golden：`0x0000→0 Ω`、`0x0A00→4211 Ω`、`0x1000→9016 Ω`；`0x27DC→RANGE_ERROR`（VTS=3.898 V）。
- **Celsius 温度换算未实现**：参考配置只有 10 kΩ NTC，无 Beta/table/曲线，禁止编造温度表 → `R→°C` 标记 **CALIBRATION REQUIRED**（Phase 8/10 边界），不作为本阶段软件 blocker。

## 14. calibration dependency

- `ReadCellVoltages13` / `ReadPackVoltageMv` 都先做 calibration 校验（`valid && gain∈[365,396] && offset∈[-128,127]`），失败返回 `BQ76940_STATUS_CALIBRATION_INVALID`，输出不变。
- 复用 Phase 3 `BQ76940_Calibration_t`，无第二套结构。

## 15. output transactional contract

| API | 失败行为 |
|---|---|
| `ReadCellVoltages13` | 任一 I2C/CRC/decode/calibration/range 失败 → caller `cell_mv[13]` 完全不变 |
| `ReadPackVoltageMv` | 失败 → `pack_mv` 不变 |
| `ReadCcRaw` | 失败 → `cc_raw` 不变 |
| `ReadTs1Raw` | 失败 → `ts1_raw14` 不变 |
| `ConvertCcRawToCurrentMa` / `ConvertTs1RawToResistanceOhm` | 失败 → 输出不变 |

测试覆盖：mid-window CRC 破坏、mid-window I2C timeout、invalid calibration、NULL、uninitialized device，全部验证输出保持 sentinel。

## 16. Software tests

实际生产 C（`bq76940.c` + `bq76940_measurement.c` + `crc8_bq76940.c`）+ trace-recording mock SoftI2C，ARMCC5 5.06u7 编译链接，Keil Cortex-M3 Simulator 执行：

```text
PHASE4_TEST_COMPLETED=1
PHASE4_TEST_FAILURES=0
PHASE4_MAPPING_FAILURES=0
PHASE4_MEASUREMENT_FAILURES=0
（cell_window=0, cell_trans=0, pack=0, cc=0, ts=0）
```

覆盖：

- A. 13S mapping：13 项逐个 golden + VC9/VC14 永不暴露 + 全范围非法索引；
- B. 30-byte block：trace 断言单 START/RESTART/STOP、pointer=0x0C、60 read-byte；
- C. cell conversion：raw min/normal/max、gain/offset 极值（Phase 3 已有向量 + Phase 4 13 项）；
- D. transactional commit：mid-window CRC fail / I2C timeout / invalid cal / NULL / uninit；
- E. BAT：3 组 golden + CRC fail 保持输出；
- F. CC：`0x0000/0x0001/0x7FFF/0x8000/0x8001/0xFFFF` two's complement 解码 + 定点换算正/负/零/极值/极性/非法参数；
- G. TS1：raw golden + 电阻 golden + VTS≥3.3V RANGE_ERROR + raw>0x3FFF；
- H. invalid inputs：NULL、invalid calibration、uninit、Rsense=0、polarity=0。

## 17. ARMCC5 Simulator 结果

`firmware/Tests/Build/Phase4/phase4_simulator.log`（SHA-256 `39b2a2231dac8a5435f5552fda63e7c6152267c73e2374962ce457d4e988ab44`）：

```text
PHASE4_TEST_COMPLETED=1
PHASE4_TEST_FAILURES=0
```

`phase4_tests.map` 同时出现 production `bq76940_measurement.o`/`bq76940.o`/`crc8_bq76940.o` 与 mock SoftI2C 符号。这是软件模拟执行，不是硬件 I2C/BQ 验证。

## 18. Python oracle 结果

命令：

```powershell
python firmware/Tests/verify_phase4.py
```

结果：exit 0，`PHASE4_STATIC_AND_EXECUTION_CHECKS: PASS`（8 组：Phase1/2/3 回归哈希、13S mapping oracle、cell oracle、BAT oracle、CC oracle、TS oracle、driver boundary、Simulator execution）。脚本 SHA-256 `2939f53e967d019a559329f9c64dbad38430c41a63bc2a593e112af15b4a6b3b`；log：`verify_phase4.log`（SHA-256 `3739bca4578193ca8132dc0ff4691ce1697226d6a98cdcac5fee448baab775f6`）。

## 19. Production Rebuild 结果

命令：

```powershell
& 'D:\Keil_v5\UV4\UV4.exe' -cr 'D:\AI\Codex\Bms_shop\firmware\Project\Keil\BMS_V1.uvprojx' -t 'BMS_V1' -j0 -o 'D:\AI\Codex\Bms_shop\firmware\Project\Keil\Build\BMS_V1_Phase4_build.log'
```

结果：ARM Compiler 5.06 update 7 build 960，16 个 source/assembly units 全量重编译，`0 Error(s), 0 Warning(s)`。build log SHA-256 `a489d878836199f1320d2327868a40de93983568b087ed1754665352be25944a`。

## 20. Code / RO / RW / ZI

```text
Code=3164
RO-data=268
RW-data=32
ZI-data=1896
Total RO=3432 B
Total RW=1928 B
Total ROM=3464 B
```

与 Phase 3 相同的原因：`main.c` 不调用 measurement API，ARMCC5 split sections 从最终 image 移除未引用函数（与 Phase 3 报告 §18 对 transport 的说明一致）；measurement 全路径由独立 test AXF 链接并执行，因此生产 ROM 增量不体现其代码。

## 21. Phase 1 → 4 size table

| 指标 | P1 | P2 | P3 | P4 | P3→P4 |
|---|---:|---:|---:|---:|---:|
| Code | 812 | 3084 | 3164 | 3164 | 0 |
| RO-data | 252 | 268 | 268 | 268 | 0 |
| RW-data | 0 | 24 | 32 | 32 | 0 |
| ZI-data | 1856 | 1896 | 1896 | 1896 | 0 |
| Total ROM | 1064 | 3376 | 3464 | 3464 | 0 |
| RW+ZI | 1856 | 1920 | 1928 | 1928 | 0 |

P3→P4 增量为 0 是 split-sections 未引用移除的预期结果（见 §20）；Measurement 代码实际由 Phase 4 test AXF 承载验证。

## 22. Phase 1～3 regression 结果

| Verifier | 结果 | 说明 |
|---|---|---|
| `verify_phase1.py` | FAIL（历史门禁） | 检查"Phase 2+ 文件不得存在"，Phase 2/3 合法内容加入后 Phase 4 之前即已 FAIL；其 Phase 1 模型哈希回归由 verify_phase3/4 覆盖 PASS |
| `verify_phase2.py` | FAIL（历史门禁） | 检查"main 无 BQ76940_"，Phase 3 合法加入 `BQ76940_Init` 后 Phase 4 之前即已 FAIL；其 Phase 2 源哈希回归由 verify_phase3/4 覆盖 PASS |
| `verify_phase3.py` | **PASS** | 唯一因 Phase 4 触发的检查（forbidden 含 `bq76940_measurement.c`）已按规则二十五最小化适配：仅从该历史完成门禁列表移除已授权文件，其余 5 组检查（Phase2 hashes、project/toolchain、register、transport/atomicity、CRC/calibration oracle、Simulator+Rebuild）全部原样 PASS |
| `verify_phase4.py` | **PASS** | 8 组全过 |

分析结论：`verify_phase1/2` 的 FAIL 是历史阶段门禁（"该阶段不得有后续阶段内容"），非 Phase 4 引入，未篡改其历史行为；Phase 1/2 的实质回归（公共模型/源哈希）由 verify_phase3（Phase2 hashes + App hashes）与 verify_phase4（App/Phase2/Phase3 全哈希）持续覆盖并 PASS。

## 23. Hardware Validation TODO

统一状态：`HARDWARE VALIDATION REQUIRED / DEFERRED`（不得写成已验证）：

- 真实 BQ7694003 身份、丝印、13S 连接与 VC9/VC14 板级 short；
- 真实 cell voltage / BAT / CC / TS1 读数与噪声；
- 4 mΩ Rsense 实值/公差/极性 → current polarity 与 CC 精度；
- 10 kΩ NTC 真实曲线与温度标定（R→°C）；
- BQ 进入 NORMAL 后约 800 ms 首批 cell 有效等待的实测（SLUSBK2I 8.3.1.1.3：BQ76940 800 ms）；
- I2C waveform、真实 CRC/ACK/NACK、TS1 wake、brownout、热行为。

Simulator/mock 只证明软件逻辑，不冒充硬件。

## 24. Phase 5 明确未实现内容

未实现：ALERT/EXTI1、ProtectTask/SampleTask/StateTask/SOCTask/BalanceTask、FreeRTOS scheduler/queue/mutex/semaphore/event group、SYS_STAT service loop、OV/UV/SCD/OCD policy、PROTECT1/2/3 正式配置、OV_TRIP/UV_TRIP、CHG/DSG、SYS_CTRL2 FET control、CELLBAL/balance policy、SOC 积分、CAN、Flash A/B、SOC log、IWDG。没有 HAL/Cube/hardware-I2C/CMSIS-RTOS。

## 25. Git commit list

```text
61d8578 phase4: add validated 13S measurement mapping and driver
af33ef5 phase4: add measurement source to ARMCC5 target and rebuild
578a47f phase4: add measurement simulator harness, oracle and golden tests
b3e506e phase4: allow authorized measurement module in historical Phase 3 gate
bc99e0c phase4: add Phase 4 report (13S measurement layer)
```

全部在 `dsh/phase4`；`main` 与 `phase3-validated` 未改变；无 push；未创建 `phase4-validated` tag。

## 26. git diff --stat phase3-validated..HEAD

```text
 deliverables/phase4/BMS_V1_Phase4_Report.md        | 345 ++++++++++++
 deliverables/review/BMS_V1_Git_基线建立任务报告.md  | 163 ++++++
 firmware/Driver/bq76940_measurement.c              | 259 +++++++++
 firmware/Driver/bq76940_measurement.h              | 175 ++++++
 firmware/Project/Keil/BMS_V1.uvoptx                |   2 +-
 firmware/Project/Keil/BMS_V1.uvprojx               |   5 +
 firmware/Project/Keil/Build/BMS_V1_Phase4_build.log|  27 +
 firmware/Project/Keil/Listings/BMS_V1.map          | 226 ++++----
 firmware/Tests/Build/Phase4/phase4_simulator.log   |  22 +
 firmware/Tests/Build/Phase4/phase4_tests.map       | 142 +++++
 firmware/Tests/Build/Phase4/verify_phase4.log      |   9 +
 firmware/Tests/phase4_simulator.ini                |  16 +
 firmware/Tests/phase4_tests.sct                    |  13 +
 firmware/Tests/test_phase4.h                       |   9 +
 firmware/Tests/test_phase4_main.c                  |  24 +
 firmware/Tests/test_phase4_mapping.c               |  91 +++
 firmware/Tests/test_phase4_measurement.c           | 617 +++++++++++++++++++++
 firmware/Tests/verify_phase3.py                    |   7 +-
 firmware/Tests/verify_phase4.py                    | 316 +++++++++++
 19 files changed, 2366 insertions(+), 102 deletions(-)
```

## 27. Codex takeover review 注意事项

1. **verify_phase3.py 有唯一一处最小适配**：forbidden 列表移除 `bq76940_measurement.c`（§22），其余检查未动；对比 `phase3-validated` 版本即可审查该行 diff。
2. **verify_phase1/2 的 FAIL 是历史门禁**，Phase 3 完成时已存在，非 Phase 4 引入；实质回归由 verify_phase3/4 覆盖。
3. **BAT 上限**：pack_mv 为 uint32，0xFFFF→100,003 mV 合法（16 位 BAT 寄存器）。
4. **CC rounding 为向零截断**，与规格 §19.4 和 ARMCC5 C 语义一致。
5. **current polarity 是显式参数**，板级极性未定 → 硬件验证项。
6. **TS 温度换算未实现**（缺 NTC 曲线），CALIBRATION REQUIRED。
7. **uvprojx 增量**：仅加入 `bq76940_measurement.c`（`d829c41f...`）；uvoptx 仅切换 sIfile。
8. main 未调用 measurement API → 生产 ROM 无增量（split sections），测试证据在 `Tests/Build/Phase4/*`。

## Phase 4 Hard Gate

| Gate | Result |
|---|---|
| 当前分支 dsh/phase4 | PASS |
| main / phase3-validated 未改变 | PASS |
| Phase 3 preflight + verify_phase3 | PASS |
| 13S mapping exact tests（VC9/VC14 跳过） | PASS |
| cell HI/LO atomic contract（单 30-byte block） | PASS |
| calibration dependency | PASS |
| cell array transactional commit | PASS |
| BAT official conversion（TI eq. 9） | PASS |
| CC signed decode（16-bit two's complement） | PASS |
| CC/current fixed-point tests（向零截断、overflow） | PASS |
| TS measurement software path（raw→resistance） | PASS |
| independent Python oracle | PASS |
| ARMCC5 Simulator actual-C tests | PASS |
| Production ARMCC5 Clean Rebuild 0/0 | PASS |
| 无 Phase 5 功能 | PASS |
| 无 HAL / 无 hardware I2C | PASS |
| Git commits 完成 | PASS |
| Phase 4 report 完成 | PASS |
| 真实 BQ/板级验证 | DEFERRED |

`PHASE 4: COMPLETE`

`STATUS: CANDIDATE FOR CODEX REVIEW`

本判定仅表示软件 Phase 4 门禁通过；最终接受权留给 Codex takeover review，不创建 `phase4-validated` tag。
