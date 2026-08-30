# BMS V1 Phase 8 Blocker Input Sheet

> 目的：记录 Phase 8 production C 的两个 policy input contract 及其后续绑定要求。
>
> 本文记录需求分析检查点；后续批准、实现与验证 identity 由最终 Release Baseline 收口。

## 0. 分析基线与结论摘要

- 候选：`codex/phase8-phase9` @ `08bf1944d12ea57f439a8710718e7057ac7fe5e4`
- Phase 7 基线：`origin/codex/review-phase7` @ `4cb25b60a2374c06a0a59f903bb73dd6bd716d74`
- 检查点状态：Phase 8 software implementation PASS；regression/build evidence PASS；两个 policy input request 已记录并在后续流程完成绑定。
- 当前 verifier：`firmware/Tests/verify_phase8.py:1311-1351` 对两个 blocker 使用 unconditional `block()`；普通源码表、标识符、注释或默认值不会自动解除 blocker。

### 总判断

### A. 当前两个 blocker 是否定义得合理？

```text
PARTIALLY
```

原因：两个 blocker 指向的缺失决策是正确的，但作为可交付输入接口仍需细化：

1. Blocker 1 不仅需要“有一张表”，还需要把表绑定到实际 NTC 零件、TS1 分压电路、覆盖范围、出界语义及批准 revision。
2. Blocker 2 不应只写成“PROTECT3 + calibration”；它至少要包含完整 startup register policy、OV/UV/OCD/SCD 的硬件编码目标、Rsense/极性、XREADY recovery/handoff 和 FET enable 条件。
3. `PROTECT3` 只承载 OV/UV delay；SCD delay 实际在 `PROTECT1`，OCD delay 在 `PROTECT2`。这必须在批准表中拆开，不能以一个模糊的“PROTECT3 参数”代替。

### B. 是否存在第三个实际上也应该阻塞 Phase 8 的外部输入？

```text
没有独立的第三个 blocker。
```

但以下输入必须作为两个 blocker 的子项纳入批准 artifact：

- NTC TS1 电路是否满足当前软件的 10 kΩ / 3.3 V 分压模型；
- Rsense nominal 与 current polarity，因为 OCD/SCD sense-voltage code 和 current conversion 都依赖它；
- startup 完成后 calibration、NTC table、first-valid-frame 到 SampleTask 的 handoff；
- XREADY 后是否允许重新授权 FET。

如果这些子项没有提供，实际仍应保持 Blocker 1 或 Blocker 2 为 `MISSING`，而不是新增 blocker 名称。

### C. 明天只提供最小输入集是否可继续？

理论上可以走以下链路：

```text
用户/硬件工程师提交批准 artifact
→ DSH 绑定精确 artifact revision/hash
→ 更新未来 gate revision（不是修改当前 gate 的文字命中条件）
→ 重跑 Phase 8 full gate
→ WorkBuddy 独立验证 source → build → test → evidence
→ 后续 Codex Sol High safety review
```

后续 gate 按层验证 artifact schema、hash、approval evidence、production wiring、生成配置、运行证据与接口矩阵的一致性。

---

## 1. 当前 production C 实际消费的接口

### 1.1 NTC / TS1 路径

| 代码事实 | 当前行为 | 对外部输入的含义 |
|---|---|---|
| `BMS_NtcPoint_t` (`firmware/App/bms_ntc.h:13-17`) | `uint32_t resistance_ohm` + `int16_t temperature_decic` | 表必须使用 Ω 和 0.1°C 整数；不能直接给浮点或 Beta 字符串。|
| `BMS_Ntc_ValidateTable()` (`bms_ntc.c:10-45`) | 至少 2 点；电阻严格单调；温度严格反向单调；升序/降序均可 | artifact 必须明确点数、排序和单调性。|
| `BMS_Ntc_Interpolate()` (`bms_ntc.c:61-102`) | 只在相邻点闭区间内插值；表外返回 false；输出保持不变 | 表端点定义了软件可转换范围；不能把外推当成有效温度。|
| `BMS_Sample_SetNtcTable()` (`bms_sample.c:435-466`) | 接受外部只读表指针；表必须在所有采样期间保持 immutable 且生命周期足够长；`NULL,0` 清除 | handoff 必须提供稳定存储、不可变 revision 和绑定时机。|
| `BMS_Sample_RunOnce()` (`bms_sample.c:686-737`) | TS1 raw/resistance 可有效；无曲线或插值失败时 `temperature_valid=false`、`temperature_unavailable=true`，但 core cell/pack 仍可单独发布 | NTC blocker 主要决定温度组是否可用，不得误称为完整 13S core 失效。|
| `BQ76940_ConvertTs1RawToResistanceOhm()` (`bq76940_measurement.c:222-258`) | 已将 TS1 raw 转为电阻 | NTC artifact 不应重复提供 raw ADC→电阻公式。|

### 1.2 AFE startup / calibration 路径

| 代码事实 | 当前行为 | 对外部输入的含义 |
|---|---|---|
| `BMS_AfeStartupConfig_t` (`bms_afe_startup.h:30-50`) | 接受 OV/UV mV、PROTECT1/2/3 presence 和 code/RSNS 字段 | 这些字段必须逐项冻结，禁止依靠零初始化。|
| `BMS_AfeStartup_StageEarlyRegisters()` (`bms_afe_startup.c:105-127`) | SYS_CTRL2 FET off、CELLBAL1/2/3=0、CC_CFG=`BQ76940_CC_CFG_REQUIRED_VALUE` | startup safe-off/CC/cellbal policy 可显式记录为固定项。|
| `BMS_AfeStartup_StageProtectionRegisters()` (`bms_afe_startup.c:129-179`) | 根据 runtime calibration 编码 OV_TRIP/UV_TRIP，并写 PROTECT3/1/2、SYS_CTRL1、最终 SYS_CTRL2 | OV/UV 目标是物理 mV；实际 OV_TRIP/UV_TRIP byte 受每次读到的 ADC calibration 影响。|
| calibration (`bms_afe_startup.c:645-679`) | 读取 ADCGAIN1、ADCOFFSET、ADCGAIN2，调用 `BQ76940_DecodeCalibration()` | 这是 runtime device input，不是用户填写一个默认 gain/offset。|
| XREADY (`bms_afe_startup.c:389-487`) | 最终状态读到 XREADY 才允许一次 W1C；清除后废弃旧证据，重新走完整配置和 800 ms settle；再次 XREADY fail-closed | approval artifact 必须冻结 recovery/handoff 语义，但不能用默认值绕过。|
| `main.c:71-92` | 当前只 SetDevice，不调用 AFE startup、SetCalibration 或 SetNtcTable | 当前 production wiring 明确保持 fail-closed；解除 blocker 后仍需未来 gate 验证合法 handoff。|

### 1.3 BQ measurement / control 的固定边界

- TS1 电阻模型：`382 µV/LSB`、`10 kΩ` pull-up、`3.3 V REGOUT`（`bq76940_measurement.h:17-23,57-60`）。
- CC：`8.44 µV/LSB`；软件用 `Rsense` 和 polarity 转换为 mA（`bq76940_measurement.h:127-146`）。
- ADC calibration：`ADCGAIN1/ADCOFFSET/ADCGAIN2` 从器件读取；当前允许 gain 365..396 µV/LSB、offset -128..127（`bq76940_regs.h:69-76`，`bq76940_measurement.c:28-38`）。
- 13S cell mapping 已固定为 VC1..VC8、VC10..VC13、VC15；不属于本轮 blocker 输入。
- `BMS_RSENSE_REFERENCE_UOHM=4000`、`BMS_CURRENT_POLARITY=1` 绑定当前 profile identity 与 revision（`bms_config.h:19-37`）。

---

# 2. Blocker 1：Production NTC curve/table

## 2.1 器件信息输入

### MUST HAVE

1. 实际装配的 NTC manufacturer、完整 part number、variant/温度等级；不能只写“10 kΩ NTC”。
2. 曲线来源：datasheet 或供应商 calibration document 的 revision、发布日期/页码或等效可追溯位置。
3. 供应商 R-T 数据或经批准的拟合来源；如果输入是公式，必须由批准流程生成最终离散 table，不能让 firmware 自己隐式推导。
4. 生产适用范围与零件 tolerance/lot assumptions，使批准者知道该 table 是否覆盖实际器差。
5. 用户批准人、硬件/校准负责人、批准日期、artifact revision 和 SHA-256。

### SHOULD HAVE

- R25、B-value 或 Steinhart-Hart 参数（仅作为 provenance/交叉检查；如果最终 table 是权威输入，runtime 不需要再保存这些参数）。
- vendor 原始点数、拟合残差、温度点来源、不同 lot/装配批次差异。
- 额定工作温度、存储温度、推荐测量温度、曲线有效上下限。

### OPTIONAL

- 额外高密度原始曲线；
- 拟合模型与离散 table 的误差报告；
- 生产测试工装的温度校验记录。

> 禁止自行填入“10K B3950”或任何常见 NTC 作为替代。R25/B-value 只有在来源明确且被批准时才有意义；当前软件最终消费的是离散 `resistance_ohm / temperature_decic` table。

## 2.2 硬件电路信息

当前软件已经把 raw TS1 conversion 抽象成电阻，但这个抽象有明确前提：

```text
TS1 path follows BQ7694003 TS equation;
VTSX = raw × 382 µV/LSB;
RTS = 10000 × VTSX / (3300000 - VTSX).
```

因此不需要把 raw ADC 公式重复写入 NTC table，但必须确认前提确实适用于目标板。

| 输入 | 是否需要 | 说明 |
|---|---|---|
| TS1 pin connection / net name | MUST HAVE | 必须确认使用的是 BQ TS1 路径，而不是 TS2/TS3 或其他 MCU ADC。|
| NTC topology（pull-up/pull-down、分压方向） | MUST HAVE | 当前公式是固定分压模型；拓扑不匹配时 table 无效。|
| bias resistor nominal 与 tolerance | MUST HAVE | 当前软件常量为 10000 Ω；实际值和容差必须由 schematic/BOM 支撑。|
| REGOUT/reference voltage | MUST HAVE | 当前软件使用 3.3 V；必须确认 BQ REGOUT 与目标接法。|
| filter RC、串联电阻、保护器件 | REQUIRES HARDWARE SCHEMATIC | 软件未建模；需作为模型适用性和后续动态误差依据。|
| ADC excitation assumptions | REQUIRES DATASHEET | BQ 的 382 µV/LSB、REGOUT 和 TS equation 已由 measurement primitive 使用，但批准 artifact 仍需引用确切 datasheet revision。|
| 静态电阻/温度接口记录 | INTERFACE VERIFICATION ITEM | 与 NTC artifact、board/profile identity 和校准记录绑定。|

## 2.3 软件 table 格式

推荐最终批准 artifact 使用机器可解析、整数化、canonical 的格式；下面是 schema，不是实际数值：

```json
{
  "artifact_type": "BMS_V1_NTC_CONFIG",
  "schema_version": 1,
  "revision": "<immutable revision>",
  "manufacturer": "<manufacturer>",
  "part_number": "<exact part number>",
  "r25_ohm": "<integer or null if not applicable>",
  "curve_model": "<vendor table | approved fit>",
  "temperature_unit": "deci_C",
  "resistance_unit": "ohm",
  "ordering": "<resistance_ascending | resistance_descending>",
  "points": [
    {"resistance_ohm": "<uint32>", "temperature_decic": "<int16>"}
  ],
  "coverage": {
    "measurement_min_decic": "<decision>",
    "measurement_max_decic": "<decision>",
    "out_of_range_behavior": "reject_no_extrapolation"
  },
  "source": {
    "datasheet_part": "<part>",
    "datasheet_revision": "<revision>",
    "source_location": "<page/table or calibration record>"
  },
  "approval": {
    "approved_by": "<identity>",
    "approved_at": "<ISO-8601>",
    "approval_record": "<immutable approval evidence id>"
  }
}
```

最终绑定时至少同时记录：

```text
artifact ID: BMS_V1_NTC_Config
revision: <exact revision>
canonical serialization: UTF-8, LF, fixed key/order rules, no floating point
SHA-256: <hash of canonical bytes>
source datasheet revision: <revision>
approval evidence hash/id: <immutable evidence>
generated firmware table hash: <if a generated C table is used>
```

生成的 C 形式必须严格对应当前 API：

```c
static const BMS_NtcPoint_t g_bms_ntc_table_<revision>[] = {
    /* approved integer points only; no guessed values */
};
```

但“源码中存在一个数组”不能作为 gate 通过条件；future verifier 必须检查 artifact hash、approval record、table content、generated-source mapping 和 production link。

## 2.4 温度覆盖范围

当前 Phase 8 没有足够产品数据确定以下数值，均不得猜：

| 范围 | 当前代码状态 | 结论 |
|---|---|---|
| measurement physical range | 由 NTC、divider、BQ TS raw range、板级热环境共同决定 | `REQUIRES PRODUCT DECISION` + `REQUIRES HARDWARE SCHEMATIC` |
| software protection range | Phase 8 没有 temperature-high/low recovery owner；`bms_protect` 仅处理 BQ SYS_STAT fault bits | `REQUIRES PRODUCT DECISION`，属于后续 policy，不应由 NTC table 猜出 |
| recovery hysteresis range | 当前没有温度 recovery/hysteresis 参数 | `REQUIRES PRODUCT DECISION` |
| plausible fault/out-of-range margin | 当前软件只对表外电阻拒绝，不做外推；端点如何覆盖故障边界未定义 | `REQUIRES PRODUCT DECISION` |

最低要求是：批准者明确 table endpoint 覆盖、表外处理、允许温度范围以及“测量有效”与“保护允许”的关系。不能把 table endpoint 自动当成软件保护阈值。

## 2.5 Blocker 1 输入表

| ID | Category | Required Input | Unit/Format | Why Software Needs It | Source | Status |
|---|---|---|---|---|---|---|
| NTC-01 | Device identity | Manufacturer、exact part number、variant | Text + immutable revision | 绑定 table 与实际 BOM，防止把别的 10 kΩ NTC 当目标件 | BOM + vendor datasheet | MISSING |
| NTC-02 | Curve provenance | 权威 R-T 数据或批准拟合来源 | Datasheet revision/page/table or calibration record | `BMS_NtcPoint_t` 没有内建曲线，必须有可审计来源 | Vendor datasheet | MISSING |
| NTC-03 | Curve points | 离散电阻/温度点 | `resistance_ohm:uint32_t`, `temperature_decic:int16_t` | 这是 `BMS_Ntc_Interpolate()` 的实际输入 | Approved source | MISSING |
| NTC-04 | Table semantics | 点数、排序、严格单调性、禁止外推 | `count:uint16_t`, enum ordering | 对应 `BMS_Ntc_ValidateTable()` 和 segment search | Software contract | KNOWN |
| NTC-05 | Coverage | physical measurement endpoints、out-of-range policy | 0.1°C + Ω; no extrapolation | 表外返回 false；决定 TS1 valid 但 temperature invalid 的边界 | Product + hardware | REQUIRES PRODUCT DECISION |
| NTC-06 | Tolerance | NTC tolerance、bias resistor tolerance、lot assumptions | % / production rule | 判断 table 是否覆盖实际器差，避免 nominal-only safety claim | BOM/datasheet/calibration | MISSING |
| NTC-07 | TS1 topology | TS1 net、pull-up/pull-down、分压方向 | Schematic/BOM | 当前 10 kΩ/3.3 V 电阻模型必须匹配目标板 | Hardware schematic | REQUIRES HARDWARE SCHEMATIC |
| NTC-08 | Bias/reference | bias resistor nominal；REGOUT/reference confirmation | Ω、%、V | `bq76940_measurement.c` 固定使用 10000 Ω 与 3300000 µV | Schematic + BQ datasheet | REQUIRES HARDWARE SCHEMATIC |
| NTC-09 | Filter | RC/filter/protection network | Schematic values and placement | 软件未建模；用于确认静态/动态模型边界 | Hardware schematic | REQUIRES HARDWARE SCHEMATIC |
| NTC-10 | Protection range | 温度保护阈值、hysteresis、delay、out-of-range action | 0.1°C / ms / policy enum | Phase 8 没有温度保护 owner，不能从 curve 猜 | Product safety policy | REQUIRES PRODUCT DECISION |
| NTC-11 | Approval binding | artifact revision、canonical hash、批准人和证据 | SHA-256 + ISO-8601 + evidence ID | 当前 gate 是 unconditional blocker，未来 gate 必须绑定不可变批准输入 | User approval record | MISSING |
| NTC-12 | Runtime handoff | 生产何时/如何调用 `BMS_Sample_SetNtcTable()`；表存储 immutable | API contract + source hash | 防止 NULL、悬空指针或运行时被改写 | Production integration design | MISSING |

---

# 3. Blocker 2：AFE startup / PROTECT3 policy + calibration/configuration handoff

## 3.1 按寄存器冻结输入

### SYS_CTRL1

当前代码：`BMS_AFE_STARTUP_SYS_CTRL1_REQUIRED = 0x18` (`bms_afe_startup.h:9-13,169-172`)。

- 软件当前固定写入并 exact readback。
- 该值包含当前实现所需的 ADC/TEMP 选择语义；具体 bit 意义必须引用目标 BQ7694003 datasheet revision。
- 批准 artifact 必须记录：desired byte、允许的 readback mask、reserved-bit policy、TS1 selection、ADC enable、失败动作。
- 不能因为代码已有 `0x18` 就把它当作用户已批准的 production policy。

### SYS_CTRL2

当前代码有两个 startup 阶段：

- early safe-off：`BMS_AFE_STARTUP_SYS_CTRL2_FET_OFF = 0x00`；
- final safe-off with CC enabled：`BMS_AFE_STARTUP_SYS_CTRL2_CC_FET_OFF = 0x40`，通过 `BQ76940_Control_SysCtrl2WithFets(..., NULL)` 生成。

批准 artifact 必须冻结：

- startup CHG=off、DSG=off；
- CC_EN 是否必须启用以及何时启用；
- DELAY_DIS、CC_ONESHOT、reserved bits 必须保持 0；
- exact readback/mask；
- FET enable 的最终授权条件。

### PROTECT1：SCD

当前 `BQ76940_Control_ComposeProtect1()` 使用：

```text
RSNS = bit7
SCD delay = bits4:3
SCD threshold = bits2:0
bits6:5 = 0
```

- SCD delay 的硬件选项由 driver 表定义为 70/100/200/400 µs；
- SCD threshold 是 SRP-SRN sense voltage，不是直接的 pack current；
- 需要 Rsense 才能把产品目标电流转换为 sense-voltage 目标；
- 当前 Phase 7 `BMS_Protect_Decide()` 对 SCD 是 active + latched + CHG/DSG 双 off + W1C；后续恢复/显式 reset 仍需 policy。

### PROTECT2：OCD

当前 `BQ76940_Control_ComposeProtect2()` 使用：

```text
OCD delay = bits6:4
OCD threshold = bits3:0
```

- driver 的 OCD delay 表为 8..1280 ms；
- threshold 表随 RSNS=0/1 分为不同 sense-voltage range；
- `SelectFromTable()` 使用“选择不低于 requested value 的最小合法 code”策略 (`bq76940_control.c:200-221`)；
- approval artifact 必须同时写物理目标、RSNS、选择策略、最终 code 和最终 register byte。

### PROTECT3：OV/UV delay

当前 `BQ76940_Control_ComposeProtect3()` 使用：

```text
UV delay = bits7:6
OV delay = bits5:4
bits3:0 = 0
```

driver 表定义为：

- UV delay：1/4/8/16 s；
- OV delay：1/2/4/8 s。

这里必须明确：**PROTECT3 不包含 SCD delay；SCD delay 在 PROTECT1。**

### OV_TRIP / UV_TRIP

当前 startup 接收 `ov_trip_mv` / `uv_trip_mv`，再用每次读取的 ADC calibration 编码：

```text
full_code = (target_mv - offset_mv) * 1000 / gain_uv_per_lsb
OV_TRIP / UV_TRIP = encoded middle 8 bits
```

因此批准输入必须是：

- 每节 cell 的 OV trip target mV；
- 每节 cell 的 UV trip target mV；
- 允许的 BQ quantization/rounding policy；
- readback verification policy；
- 不能把一个静态 OV_TRIP/UV_TRIP byte 当成所有器件通用值，因为 runtime ADC gain/offset 会影响 encoded byte。

### CC_CFG

当前 `BQ76940_CC_CFG_REQUIRED_VALUE = 0x19` (`bq76940_regs.h:24-25`)；startup early pass 写入并 exact readback。

- 这是当前软件/Phase 3 contract 中的已知固定值；
- 生产 artifact 仍应记录该值、datasheet basis、CC enable/first CC_READY 预期和失败动作；
- 不需要用户另猜一个 CC_CFG，但需要批准方确认它确实适用于目标 BQ7694003 revision 和板级电流采样。

### CELLBAL1/2/3

startup 当前固定写 0 并 exact readback，再进入后续流程；initial blocking status 也要求三组 balancing-off 安全证据。

- Phase 8 startup 输入不需要未来 balancing policy；
- 但批准 artifact 应冻结 startup 的 `0x00/0x00/0x00` 与 readback 要求；
- 何时允许后续 BalanceTask 改写属于后续阶段，不应在本轮实现。

### ADCGAIN1 / ADCOFFSET / ADCGAIN2

这三者是 runtime device inputs：

1. startup 每次完整 startup/XREADY recovery 读取；
2. `BQ76940_DecodeCalibration()` 产生 `gain_uv_per_lsb`、`offset_mv`、`valid`；
3. 只有 `valid` 且处于当前 XREADY generation 才能交给 SampleTask；
4. 不能提供一个默认 calibration 来替代器件读值。

批准 artifact 需要冻结的是 handoff contract，而不是伪造固定 ADC 数值：

```text
read each startup/recovery: yes
read failure: startup terminal fail-safe
invalid decode: no Sample calibration binding
XREADY generation: calibration must be rebound
first valid core frame: product-defined FET authorization gate
```

## 3.2 保护参数：硬件编码 vs 软件 recovery policy

| 功能 | BQ hardware encoding | Phase 9 / App software policy | 当前 Phase 8 状态 |
|---|---|---|---|
| OV | `OV_TRIP` target mV + `PROTECT3.OV_D` code | recovery threshold、hysteresis、recovery delay、CHG action、DSG action | OV event CHG-off 边界；完整 recovery 由后续 policy binding 承接 |
| UV | `UV_TRIP` target mV + `PROTECT3.UV_D` code | recovery threshold、hysteresis、recovery delay、CHG action、DSG action | UV event DSG-off 边界；完整 recovery 由后续 policy binding 承接 |
| OCD | `PROTECT2` threshold/delay code，依赖 RSNS | recovery condition、retry/lockout、DSG action | OCD event DSG-off 边界；完整 recovery 由后续 policy binding 承接 |
| SCD | `PROTECT1` threshold/delay code，依赖 RSNS | latch/reset/explicit service、CHG/DSG action | 当前 active+latched、双 off；不自动恢复 |
| RSNS | `PROTECT1.bit7`；决定 sense threshold table | 目标电流映射到 sense voltage | 当前 profile 绑定 4000 µΩ 与 polarity identity |
| XREADY | 非保护 threshold；recovery 后 W1C | full reconfiguration、calibration rebind、first-valid/FET gate | 软件状态机和 quarantine 已有，production wiring 未接入 |

### 必须冻结的 OV 输入

- trip threshold：每节 mV；
- hardware delay：PROTECT3 OV code/物理秒数；
- recovery threshold；
- hysteresis；
- recovery delay；
- CHG/DSG action；
- latch/clear policy；
- threshold quantization and readback rule。

### 必须冻结的 UV 输入

- trip threshold：每节 mV；
- hardware delay：PROTECT3 UV code/物理秒数；
- recovery threshold；
- hysteresis；
- recovery delay；
- CHG/DSG action；
- latch/clear policy；
- threshold quantization and readback rule。

### 必须冻结的 OCD 输入

- target current 或 target sense voltage；
- RSNS selection；
- threshold code；
- hardware delay/code；
- recovery condition、retry/lockout；
- DSG action；
- 量化后“不得低于目标”的确认。

### 必须冻结的 SCD 输入

- target current 或 target sense voltage；
- RSNS selection；
- `PROTECT1.SCD_T` code；
- `PROTECT1.SCD_D` code（不是 PROTECT3）；
- latch/reset policy；
- CHG/DSG action；
- ambiguous/communication failure 后的保持策略。

## 3.3 Rsense / current 输入

### 软件配置 blocker 子项

以下是生成正确 PROTECT1/2 code 和 current conversion 所需的最小信息：

- Rsense nominal（当前 profile identity 为 4000 µΩ）；
- `RSNS` bit 选择；
- current polarity（当前 `BMS_CURRENT_POLARITY=+1` 只是显式 reference assumption）；
- 目标 OCD/SCD current 或 sense voltage；
- 选码规则和最终 code/byte；
- calibration handoff 中使用的 CC LSB 与 Rsense conversion 版本。

### 不是当前 software blocker、但必须后续验证

- Rsense tolerance；
- pulse/continuous power rating；
- Kelvin routing、铜阻和 layout；
- 实际 current gain/offset 与 polarity；
- 真实 OCD/SCD analog trip accuracy。

nominal Rsense 与 polarity 必须由同一 approved profile identity 绑定到 protection policy artifact，并与接口矩阵记录一致。

## 3.4 Startup policy 的逐步输入接口

| 步骤 | 当前软件固定行为 | 仍需外部输入/确认 | 结论 |
|---|---|---|---|
| Power-up/init | `main.c` 初始化 data/sample、创建 BQ device；当前不连接 startup | startup owner、调用时机、失败终态 | MISSING handoff |
| FET fail-safe off | SYS_CTRL2 写 off、readback；Protect init request 也为双 off | exact register/mask 和硬件 FET mapping | KNOWN software contract + approval binding |
| CELLBAL off | 三寄存器写 0、readback；unsafe probe 也要求验证 | reserved-bit/readback policy | KNOWN software contract |
| Wake/probe | wake callback bounded；最多 3 probe attempts；10 ms settle | PA8→TS1 wiring、pulse polarity、electrical level、datasheet basis | REQUIRES HARDWARE SCHEMATIC |
| Initial SYS_STAT | 读取；blocking mask `0x1F` 使 startup 在安全输出确认后终止；初始 XREADY 不盲清 | initial fault ownership、initial XREADY disposition | REQUIRES PRODUCT DECISION |
| Calibration | 读 ADCGAIN1/ADCOFFSET/ADCGAIN2，decode，invalid 即失败 | approved read/reject/handoff contract；不允许 default | MISSING |
| Protection encode | OV/UV 依 runtime cal；PROTECT1/2/3 依 policy codes | exact targets/codes/RSNS | MISSING |
| Register readback | 每个 register 写后读回；mismatch terminal，FET-off 有一次 bounded correction | allowed masks、reserved-bit rule、diagnostic mapping | MISSING approval detail |
| Initial data settle | 800 ms；不持 I2C | 是否满足目标 BQ datasheet revision | REQUIRES DATASHEET |
| Final SYS_STAT | blocking fault terminal fail-safe；只对当前最终读到的 XREADY 做一次 W1C | final status acceptance and FET gate | MISSING policy binding |
| XREADY clear | W1C ambiguous terminal；成功清除后清旧证据并完整重配；second XREADY fail | recovery authorization and second-event policy | MISSING policy binding |
| First valid measurement | Sample 需要 current-generation valid calibration；core 需全 13 cell + BQ pack valid | 是否要求 current/temperature 也 valid 才能授权 FET | REQUIRES PRODUCT DECISION |
| Later State/FET control | Phase 9 owner contract | FET enable owner、fault/stale gating | BOUND IN RELEASE BASELINE |

## 3.5 XREADY production policy

当前软件已实现或明确的部分：

- `BMS_Protect` 在首次 inactive→active 观察时递增 generation，并使 latest CC mailbox invalid；
- Sample acquire 前与 publish 前都检查 active/generation/calibration/device/config；
- `BMS_Protect_RecoverXready()` 的 hook contract 要求完整 re-init、settle、calibration reload、protection/config re-apply、readback/status verification 后才可 W1C；
- W1C finalization ambiguous 时不 replay、不宣布成功；
- startup 状态机在一次成功 XREADY clear 后废弃旧 calibration/FET/safe-output evidence，重新执行完整配置和 800 ms settle；第二次 XREADY fail-closed。

仍需外部批准的 production policy：

1. 每次 XREADY 是否必须重新读取 ADCGAIN1/ADCOFFSET/ADCGAIN2：应固定为必须；artifact 不能提供默认值替代。
2. 每次 XREADY 是否重新写 CC_CFG、SYS_CTRL、CELLBAL、PROTECT、OV/UV：应固定为完整 reconfiguration；不得复用旧证据。
3. FET 何时可恢复：至少应定义 `XREADY inactive + recovery complete + calibration bound + first valid core frame + no active blocking fault`；是否要求 current/temperature valid 是产品决策。
4. XREADY W1C 的授权身份：仅当前 status read 看到高且 recovery contract 成功后允许一次；ACK/STOP ambiguity 不可从软件“推断提交”。
5. second XREADY：当前代码设为 terminal fail-safe；若产品想自动 retry，必须另立批准政策和测试，不能由 Phase 8 默认实现隐式开启。
6. production handoff 的唯一 owner：startup 完成后由谁调用 `BMS_Sample_SetCalibration()`、谁安装 immutable NTC table、谁发布 `EVT_AFE_ONLINE`/允许 FET，必须明确。

## 3.6 FET policy 输入

当前 Phase 7 已冻结的硬件事件动作（来源：`bms_protect.c:412-480`、Phase 7 report）：

- startup：CHG off、DSG off；
- OV：CHG off；
- UV：DSG off；
- OCD：DSG off；
- SCD：CHG/DSG 双 off + latched；
- OVRD_ALERT：CHG/DSG 双 off + latched；
- XREADY：CHG/DSG 双 off + latched。

下面这些仍需要产品明确，但属于后续 App/Phase 9 policy；不能在本轮自行补值：

| 条件 | 当前代码 | 外部输入 |
|---|---|---|
| communication failure | 记录 AFE comm/CRC fault；当前没有完整 runtime FET recovery owner | `REQUIRES PRODUCT DECISION`：立即双 off、保持、重试、人工 reset 等 |
| stale measurement | Sample 记录 stale transition；当前没有 StateTask FET action | `REQUIRES PRODUCT DECISION`：CHG/DSG 分别动作、超时、恢复条件 |
| XREADY recovery complete | 当前 recovery 后 active 可清，但历史 latch 与 FET enable 不由 Phase 8 自动放开 | `REQUIRES PRODUCT DECISION`：first valid frame 和 fault-clear gate |
| hardware over/under thresholds | 当前 SYS_STAT event capture 只处理事件，不实现完整 software recovery | `REQUIRES PRODUCT DECISION`：hysteresis/delay/lockout |

---

# 4. Blocker 2 输入表

| ID | Category | Required Input | Unit/Format | Why Software Needs It | Source | Status |
|---|---|---|---|---|---|---|
| AFE-01 | Device basis | BQ7694003 exact variant、datasheet revision、errata basis | Text + revision | 冻结 register semantics、TS/CC equations、W1C 和 timing 来源 | TI datasheet/errata | REQUIRES DATASHEET |
| AFE-02 | SYS_CTRL1 | Desired byte、TS1/ADC enable semantics、readback mask | uint8 + bit policy | startup exact write/readback；当前 implementation constant 为 `0x18` | Datasheet + approved policy | MISSING |
| AFE-03 | SYS_CTRL2 | Early/final safe-off bytes、CC_EN、reserved bits、FET enable rule | uint8 + bit masks | 确保 startup 和 recovery 不开启 FET；当前 early `0x00`、final `0x40` | Datasheet + product policy | MISSING |
| AFE-04 | CELLBAL | Startup `0/0/0`、readback mask、later enable owner | 3 × uint8 + policy | startup safe-off 证据；防 balancing 与 startup 并行 | Datasheet + product policy | MISSING |
| AFE-05 | CC_CFG | Approved `0x19` meaning、readback、CC enable/first-event policy | uint8 + revision | current startup writes exact value；防 default/omission | Datasheet + existing software contract | MISSING |
| AFE-06 | OV hardware | Per-cell OV target、PROTECT3 OV delay code/physical delay、quantization/readback | mV/cell + code + s | 生成 `OV_TRIP` 和 PROTECT3；runtime calibration affects OV byte | Product + datasheet | MISSING |
| AFE-07 | UV hardware | Per-cell UV target、PROTECT3 UV delay code/physical delay、quantization/readback | mV/cell + code + s | 生成 `UV_TRIP` 和 PROTECT3；runtime calibration affects UV byte | Product + datasheet | MISSING |
| AFE-08 | OCD hardware | Target current or sense voltage、OCD delay、threshold code/byte | A / mV sense + ms + code | 生成 PROTECT2；driver按“不低于目标”选码 | Product + datasheet | MISSING |
| AFE-09 | SCD hardware | Target current or sense voltage、SCD delay、threshold code/byte | A / mV sense + µs + code | 生成 PROTECT1；SCD delay在 PROTECT1，不在 PROTECT3 | Product + datasheet | MISSING |
| AFE-10 | RSNS | Nominal Rsense、RSNS bit、tolerance assumption、current polarity | µΩ、bool、%、`+1/-1` | OCD/SCD sense mapping和CC current conversion依赖 | Schematic/BOM + product policy | MISSING |
| AFE-11 | Runtime calibration | 每次 startup/XREADY 读取三 calibration registers；invalid/no-default policy | ADCGAIN1/2 + ADCOFFSET runtime contract | 把有效 calibration 绑定到当前 XREADY generation | BQ datasheet + software contract | MISSING |
| AFE-12 | Calibration handoff | startup complete→`BMS_Sample_SetCalibration()` 的唯一 owner、时机、失败动作 | State/sequence contract | 当前 main 未接线；Sample 不能使用未绑定或旧代 calibration | Production integration design | MISSING |
| AFE-13 | NTC handoff | 已批准 NTC artifact 的 immutable storage、`SetNtcTable()` 调用时机 | Artifact hash + pointer lifetime | 温度组不能依靠默认表或悬空表 | NTC approval + integration design | MISSING |
| AFE-14 | Wake/probe | PA8→TS1 wiring、wake polarity/level、settle assumptions | Schematic + timing | `BMS_AfeStartupWakeFn_t` 是板级 callback，不含电气实现 | Hardware schematic | REQUIRES HARDWARE SCHEMATIC |
| AFE-15 | Startup sequence | complete register order、每步 readback、800 ms settle、terminal failures | State-machine policy | 与现有 state machine 和 evidence 绑定，禁止跳步 | Approved policy + datasheet | MISSING |
| AFE-16 | Initial status | blocking status mask、initial XREADY/CC_READY ownership、fault terminal action | Bit mask + policy enum | 当前 mask `0x1F`；保护事件不能被 startup 误清 | Datasheet + product policy | MISSING |
| AFE-17 | XREADY recovery | full reconfiguration scope、calibration reload、W1C authorization、second XREADY action | State/sequence contract | 防跨 generation frame、旧 calibration、旧 FET证据复用 | Product + datasheet + software contract | MISSING |
| AFE-18 | FET authorization | first valid core frame 是否足够；current/temperature 是否必须 valid；fault/stale gate | Boolean/policy matrix | 决定何时从全 off 进入后续 State/FET owner | Product safety policy | REQUIRES PRODUCT DECISION |
| AFE-19 | FET event actions | comm failure、stale、OV/UV/OCD/SCD/XREADY 的 CHG/DSG action/latch | Policy matrix | 当前仅有 Phase 7 capture/inhibit，完整 recovery 属后续 owner | Product safety policy | REQUIRES PRODUCT DECISION |
| AFE-20 | Approval binding | artifact revision、canonical hash、approval record、generated config/source mapping | SHA-256 + evidence ID | 当前 verifier 只接受未来明确 revision，不接受文本命中 | User approval record | MISSING |
| AFE-21 | Physical behavior | actual FET/MOS response、BQ delay、ALERT/W1C commit behavior | Board integration evidence | 与 board/profile/firmware identity 绑定 | Integration test | INTERFACE VERIFICATION ITEM |

---

# 5. 推荐的 Blocker 2 approved artifact

建议建立一个机器可解析、不可变的 `BMS_V1_AFE_Startup_Policy_RevX`，并配套 human-readable approval record。以下为字段接口，不是默认值：

```json
{
  "artifact_type": "BMS_V1_AFE_STARTUP_POLICY",
  "schema_version": 1,
  "revision": "<immutable revision>",
  "device": {
    "part": "BQ7694003",
    "datasheet_revision": "<revision>",
    "errata_revision": "<revision or null>"
  },
  "startup_registers": {
    "sys_ctrl1": {"value": "<uint8>", "readback_mask": "<uint8>"},
    "sys_ctrl2_early": {"value": "<uint8>", "readback_mask": "<uint8>"},
    "sys_ctrl2_final": {"value": "<uint8>", "readback_mask": "<uint8>"},
    "cellbal1": {"value": 0, "readback_mask": "<uint8>"},
    "cellbal2": {"value": 0, "readback_mask": "<uint8>"},
    "cellbal3": {"value": 0, "readback_mask": "<uint8>"},
    "cc_cfg": {"value": "<uint8>", "readback_mask": "<uint8>"}
  },
  "protection": {
    "ov": {"target_mv_per_cell": "<uint16>", "delay_code": "<uint8>", "delay_s": "<uint8>"},
    "uv": {"target_mv_per_cell": "<uint16>", "delay_code": "<uint8>", "delay_s": "<uint8>"},
    "ocd": {"target_current_ma": "<int32>", "sense_mv": "<uint16>", "delay_code": "<uint8>", "delay_ms": "<uint16>", "threshold_code": "<uint8>"},
    "scd": {"target_current_ma": "<int32>", "sense_mv": "<uint16>", "delay_code": "<uint8>", "delay_us": "<uint16>", "threshold_code": "<uint8>"},
    "rsns": {"bit": "<0-or-1>", "nominal_uohm": "<uint32>", "polarity": "<-1-or-1>"}
  },
  "calibration_handoff": {
    "read_adc_gain1": true,
    "read_adc_offset": true,
    "read_adc_gain2": true,
    "default_calibration_allowed": false,
    "rebind_on_xready": true,
    "invalid_calibration_action": "<terminal fail-safe policy>"
  },
  "xready": {
    "clear_authorization": "<final-status-read-only-after-full-recovery>",
    "full_reconfiguration_required": true,
    "second_xready_action": "<terminal fail-safe policy>",
    "fet_enable_gate": "<approved policy>"
  },
  "fet_policy": {
    "startup_chg": "<OFF>",
    "startup_dsg": "<OFF>",
    "ov": {"chg": "<decision>", "dsg": "<decision>"},
    "uv": {"chg": "<decision>", "dsg": "<decision>"},
    "ocd": {"chg": "<decision>", "dsg": "<decision>"},
    "scd": {"chg": "<decision>", "dsg": "<decision>"},
    "xready": {"chg": "<OFF>", "dsg": "<OFF>"}
  },
  "source": {
    "datasheet_revision": "<revision>",
    "policy_spec_revision": "<revision>"
  },
  "approval": {
    "approved_by": "<identity>",
    "approved_at": "<ISO-8601>",
    "approval_record": "<immutable evidence id>"
  }
}
```

### 为什么 artifact 不能只存最终寄存器 bytes

- OV_TRIP/UV_TRIP 由 runtime ADC gain/offset 生成；应存 physical target、编码规则和 readback acceptance，而不是伪造一个跨器件固定 byte。
- PROTECT1/2 threshold 依赖 RSNS；只存 `0x06/0x07` byte 会掩盖 current target、Rsense 和 quantization 关系。
- XREADY recovery 和 FET enable 是序列/状态契约，不是单个寄存器常量。
- calibration 是每次 runtime read 的 device input；artifact 需要冻结 handoff/no-default policy，不需要伪造 ADC register 内容。

---

# 6. Minimum Inputs Required to Unblock Phase 8

以下是该检查点定义的**最小且完整**输入集合；其批准 identity 与 production binding 已纳入最终 Release Baseline。

## Blocker 1 Minimum Set

1. **实际 NTC 身份与权威曲线来源**
   - manufacturer、exact part number/variant；
   - vendor datasheet/calibration source revision；
   - 用户批准的 immutable R(T) point list。
2. **符合当前 API 的 canonical table**
   - `resistance_ohm:uint32_t`、`temperature_decic:int16_t`；
   - 至少 2 点、严格单调、排序明确；
   - endpoint、coverage、表外拒绝/不外推政策明确。
3. **TS1 电路模型确认**
   - schematic/BOM 证明 TS1、分压拓扑、bias resistor、REGOUT/reference 与当前 `382 µV/LSB + 10 kΩ + 3.3 V` 模型一致；
   - RC/filter 不一致时必须先解决模型适用性，不得只交一张曲线。
4. **不可变批准绑定**
   - artifact revision、canonical SHA-256、approval evidence ID；
   - 若生成 C table，增加 generated-source hash 和 source→artifact mapping；
   - future verifier 显式绑定这些值。

## Blocker 2 Minimum Set

1. **完整 AFE startup policy artifact**，而非只给 PROTECT3 两个 code：
   - SYS_CTRL1、SYS_CTRL2 early/final；
   - CELLBAL1/2/3 startup zero；
   - CC_CFG；
   - OV/UV target mV + PROTECT3 delay code/物理 delay；
   - OCD/PROTECT2 threshold+delay；
   - SCD/PROTECT1 threshold+delay；
   - reserved bits/readback/quantization policy。
2. **Rsense/current mapping**
   - nominal Rsense、RSNS bit、current polarity；
   - OCD/SCD target current 或 sense voltage；
   - 最终选码/寄存器 byte 可由规则重算。
3. **Runtime calibration contract**
   - 每次 startup/XREADY 读取 ADCGAIN1/ADCOFFSET/ADCGAIN2；
   - invalid/read failure 不得使用默认值；
   - calibration 必须绑定当前 XREADY generation，并在 startup 完成后显式 handoff 到 SampleTask。
4. **XREADY and FET handoff policy**
   - full reconfiguration required；
   - W1C 只由最终授权 status read 触发；
   - second XREADY terminal action；
   - first valid core frame、current/temperature validity、fault/stale 条件与 FET enable 的关系；
   - startup/OV/UV/OCD/SCD/XREADY 的 CHG/DSG action 矩阵。
5. **Datasheet/policy approval binding**
   - BQ7694003 datasheet/errata revision；
   - policy artifact revision、canonical SHA-256、approval evidence；
   - future gate 显式验证 source→generated configuration→production wiring。

> 最小集合不要求提供一个固定 ADCGAIN/ADCOFFSET 数字，也不要求以 Simulator 证明真实 W1C commit、MOS timing 或 current accuracy。那些是 runtime/hardware evidence，不是可伪造的 policy input。

---

# 7. Interface observation dimensions

以下事项重要，但不应被错误包装成当前两个软件 Hard Gate blocker 的输入：

- NTC 温度准确度、全温区误差与 lot variation；
- 真实 BQ OV/UV/OCD/SCD trip delay 和 threshold accuracy；
- 真实 Rsense power、Kelvin layout、铜阻影响；
- real MOS/FET turn-off/turn-on timing、body diode、负载瞬态；
- ALERT edge timing、W1C physical commit point、STOP ambiguity 的波形证明；
- stuck bus、brownout、watchdog physical reset；
- EMI/ESD、thermal、power integrity、PCB/BOM 量产验证；
- actual current calibration accuracy and polarity waveform；
- Phase 9 StateTask 的完整 software recovery implementation；
- SOC、Balance、CAN、Flash、UART 扩展。

其中，若 schematic 与当前 TS1/RSense 模型明显不匹配，则相应 artifact 不能被批准；这仍属于 Blocker 1/2 的输入完整性，而不是新增第三 blocker。

---

# 8. Future gate binding requirements

解除 blocker 时，future verifier 至少应检查：

1. exact approved artifact path exists；
2. canonical bytes/hash exactly match expected revision；
3. approval evidence is present and binds the same revision；
4. schema validates all required fields and rejects omitted/zero-initialized policy fields；
5. NTC points match the generated production table exactly；
6. AFE physical targets/code mapping recomputes to the approved register policy；
7. runtime calibration remains a required device read, not a source default；
8. `BMS_AfeStartup_Init/Step`、`BMS_Sample_SetCalibration`、`BMS_Sample_SetNtcTable` 的 production wiring符合 handoff policy；
9. production map、manifest、Clean/Rebuild、tests 和 evidence 对应同一 candidate revision；
10. 任何新增注释、字符串、默认数组或 policy-looking identifier 都不能单独解除 blocker。

## Final disposition

```text
本检查点记录 policy input contract，未改变当时的 production code 或 tests。
两个输入的批准绑定、Phase 9 实现与验证证据已纳入最终 Release Baseline。
```
