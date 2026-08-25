# BMS V1 模拟硬件参数与产品策略基线 v1

> **文档定位：学习/实践项目的统一模拟参数基线。**
>
> 本项目目标是把 BMS 软件架构、驱动、保护、状态机、SOC、均衡、通信和故障恢复完整落地，形成可解释、可验证、可迭代的软件闭环；**不是量产产品认证项目**。
>
> 因此，本文件中的 `SIM_BASELINE_V1` / `SIM_POLICY_V1` 参数允许直接用于当前软件开发、单元测试、仿真与集成验证。未来拿到真实 BOM、原理图和实板后，再把相关字段替换为 `REAL_HW_VERIFIED` 并重新验证。
>
> **规则：真实硬件参数未知，不再作为当前学习项目的软件开发阻塞条件。**

---

## 1. 参数等级与项目执行规则

| 等级 | 含义 | 当前是否可用于开发 |
|---|---|---:|
| `SOURCE_FIXED` | 已由当前源码、芯片接口或冻结架构明确 | ✅ |
| `SIM_BASELINE_V1` | 为学习项目人为设定的模拟硬件参数 | ✅ |
| `SIM_POLICY_V1` | 为上层状态机/保护/恢复人为设定的模拟产品策略 | ✅ |
| `DERIVED` | 由其他已选参数计算得到 | ✅ |
| `REAL_HW_TBD` | 未来真实硬件需要替换/确认 | ✅ 当前不阻塞 |
| `REAL_HW_VERIFIED` | 未来已通过 BOM/实测/波形确认 | ✅ |

### 1.1 从本版本开始的强制流程

```text
遇到硬件/产品参数
        ↓
先查本文件
        ↓
存在 SIM_BASELINE_V1 / SIM_POLICY_V1
        ↓
直接继续开发
        ↓
仿真/单元测试/软件集成
        ↓
未来有真实硬件
        ↓
替换参数 + 实板验证
```

只有以下两类问题允许真正阻塞开发：

1. 参数之间逻辑矛盾，导致软件行为无法定义；
2. 违反已经冻结的安全架构所有权，例如 FET/XREADY/fault ownership/IWDG writer 冲突。

**“没有真实 BOM”“NTC 还没买”“Rsense 以后可能改”“保护阈值以后可能调”不再构成当前学习项目的停止条件。**

---

# 2. 系统总体基线

| 参数 | 基线 | 等级 |
|---|---:|---|
| 项目 | BMS V1 | `SOURCE_FIXED` |
| 应用 | 两轮车 48 V 级 BMS 学习项目 | `SIM_BASELINE_V1` |
| 电芯体系 | NMC 三元锂 | `SOURCE_FIXED/REFERENCE` |
| 串数 | 13S | `SOURCE_FIXED` |
| 标称容量 | 20 Ah | `SOURCE_FIXED/REFERENCE` |
| MCU | STM32F103C8T6 | `SOURCE_FIXED` |
| AFE | TI BQ7694003 | `SOURCE_FIXED` |
| RTOS | FreeRTOS | `SOURCE_FIXED` |
| 系统时钟 | 72 MHz | `SOURCE_FIXED` |
| CAN | 500 kbit/s | `SOURCE_FIXED` |

---

# 3. 电池包模拟参数

## 3.1 电芯/Pack 基本电压

| 参数 | Cell | 13S Pack | 等级 |
|---|---:|---:|---|
| 标称电压 | 3.70 V | 48.10 V | `SIM_BASELINE_V1` |
| 满充参考 | 4.20 V | 54.60 V | `SIM_BASELINE_V1` |
| 软件欠压保护点 | 3.00 V | 39.00 V | `SIM_POLICY_V1` |
| 硬件欠压保护目标 | 2.80 V | 36.40 V | `SIM_BASELINE_V1` |
| 软件测量合法最小值 | 2.00 V | — | `SOURCE_FIXED` |
| 软件测量合法最大值 | 5.00 V | — | `SOURCE_FIXED` |
| Startup 合法最小值 | 2.50 V | — | `SOURCE_FIXED` |
| Startup 合法最大值 | 4.30 V | — | `SOURCE_FIXED` |

### 3.2 仿真初始条件

默认启动场景：

- 13 节全部初始化为 **3.70 V ± 5 mV**
- Pack 约 **48.1 V**
- 初始 SOC：**50%**
- 初始电流：**0 mA**
- 初始温度：**25.0 °C**
- 所有 Fault：inactive
- CHG/DSG：Startup 阶段均 OFF

---

# 4. MCU / BSP / 通信硬件基线

当前源码已经存在以下参考配置：`bms_config.h`。

| 功能 | 参数 | 等级 |
|---|---|---|
| HSE | 8 MHz | `SOURCE_FIXED` |
| SYSCLK | 72 MHz | `SOURCE_FIXED` |
| PCLK1 | 36 MHz | `SOURCE_FIXED` |
| PCLK2 | 72 MHz | `SOURCE_FIXED` |
| 软件 I2C SCL | PB8 | `SOURCE_FIXED` |
| 软件 I2C SDA | PB9 | `SOURCE_FIXED` |
| AFE ALERT | PB1 / EXTI1 | `SOURCE_FIXED` |
| AFE WAKE | PA8 | `SOURCE_FIXED` |
| CAN RX | PA11 | `SOURCE_FIXED` |
| CAN TX | PA12 | `SOURCE_FIXED` |
| Software-I2C half cycle | 5 µs | `SOURCE_FIXED` |
| 名义 I2C 速率 | ≈100 kHz | `DERIVED` |
| SCL high timeout | 1000 µs | `SOURCE_FIXED` |
| Bus free timeout | 1000 µs | `SOURCE_FIXED` |
| I2C mutex timeout | 20 ms | `SOURCE_FIXED` |
| CAN bitrate | 500 kbit/s | `SOURCE_FIXED` |

---

# 5. AFE Startup 模拟基线

## 5.1 Startup 固定策略

| 项目 | 模拟/源码基线 | 等级 |
|---|---:|---|
| Wake settle | 10 ms | `SOURCE_FIXED` |
| Initial data settle | 800 ms | `SOURCE_FIXED` |
| Startup probe 最大次数 | 3 | `SIM_BASELINE_V1` |
| SYS_CTRL1 | `0x18` | `SOURCE_FIXED/REFERENCE` |
| SYS_CTRL2 early | `0x00` | `SOURCE_FIXED/REFERENCE` |
| SYS_CTRL2 final before scheduler handoff | `0x40` | `SOURCE_FIXED/REFERENCE` |
| CELLBAL1 | `0x00` | `SOURCE_FIXED` |
| CELLBAL2 | `0x00` | `SOURCE_FIXED` |
| CELLBAL3 | `0x00` | `SOURCE_FIXED` |
| CC_CFG | `0x19` | `SOURCE_FIXED/REFERENCE` |
| Startup CHG | OFF | `FROZEN` |
| Startup DSG | OFF | `FROZEN` |
| ADC calibration | 每次 startup/XREADY 从器件读取 | `FROZEN` |
| 默认 ADC calibration | 禁止 | `FROZEN` |

**说明：** `OV_TRIP` / `UV_TRIP` 最终寄存器 byte 不在本文件硬编码，因为它们必须根据运行时读取到的 ADC gain/offset 编码。

---

# 6. 电流采样 / Rsense 模拟基线

| 参数 | 值 | 等级 |
|---|---:|---|
| Rsense nominal | **4000 µΩ (4 mΩ)** | `SIM_BASELINE_V1` |
| Current polarity | **+1** | `SIM_BASELINE_V1` |
| 正电流定义 | 充电 | `SIM_POLICY_V1` |
| 负电流定义 | 放电 | `SIM_POLICY_V1` |
| BQ CC LSB | 8.44 µV/LSB | `SOURCE_FIXED` |
| RSNS selection | **0** | `SIM_BASELINE_V1` |

计算关系：

```text
Vsense[mV] = I[A] × Rsense[mΩ]

本基线：
1 A  ≈ 4 mV
5 A  ≈ 20 mV
10 A ≈ 40 mV
20 A ≈ 80 mV
```

---

# 7. BQ 硬件保护模拟基线

## 7.1 Hardware OV

| 参数 | 值 |
|---|---:|
| 目标 Cell OV | **4250 mV** |
| Delay | **2 s** |
| PROTECT3 OV delay code | **1** |
| Action | CHG inhibit |
| Recovery | 由上层恢复握手判断，不以 SYS_STAT low 单独证明 |

等级：`SIM_BASELINE_V1`

## 7.2 Hardware UV

| 参数 | 值 |
|---|---:|
| 目标 Cell UV | **2800 mV** |
| Delay | **4 s** |
| PROTECT3 UV delay code | **1** |
| Action | DSG inhibit |

等级：`SIM_BASELINE_V1`

## 7.3 Hardware OCD

模拟目标：

```text
requested discharge OCD = 10.0 A
Rsense = 4 mΩ
requested Vsense = 40 mV
RSNS = 0
```

BQ 当前 source table 中第一个不低于 40 mV 的值：

```text
42 mV
```

因此：

| 参数 | 值 |
|---|---:|
| Requested OCD | 10.0 A |
| Selected sense threshold | 42 mV |
| 实际等效阈值 | 10.5 A |
| OCD threshold code | 12 |
| Delay | 320 ms |
| OCD delay code | 5 |
| PROTECT2 | `0x5C` |
| Action | DSG inhibit |

等级：`SIM_BASELINE_V1 + DERIVED`

## 7.4 Hardware SCD

模拟目标：

```text
requested SCD = 20.0 A
Rsense = 4 mΩ
requested Vsense = 80 mV
RSNS = 0
```

第一个不低于 80 mV 的合法值：

```text
89 mV
```

因此：

| 参数 | 值 |
|---|---:|
| Requested SCD | 20.0 A |
| Selected sense threshold | 89 mV |
| 实际等效阈值 | 22.25 A |
| SCD threshold code | 6 |
| Delay | 200 µs |
| SCD delay code | 2 |
| PROTECT1 | `0x16` |
| Action | CHG + DSG inhibit |
| Latch | YES |

等级：`SIM_BASELINE_V1 + DERIVED`

## 7.5 PROTECT3

UV delay code = 1，OV delay code = 1：

```text
PROTECT3 = 0x50
```

等级：`DERIVED`

---

# 8. NTC / 温度采样模拟基线

## 8.1 模拟热敏电阻

本项目为了软件闭环，使用一个**人为模拟型号**：

```text
SIM_NTC_10K_B3950
```

它不是对真实 BOM 的声明。

| 参数 | 值 | 等级 |
|---|---:|---|
| R25 | 10 kΩ | `SIM_BASELINE_V1` |
| Beta | 3950 K | `SIM_BASELINE_V1` |
| NTC nominal tolerance | ±1% | `SIM_BASELINE_V1` |
| Bias resistor | 10 kΩ | `SIM_BASELINE_V1` |
| Bias resistor tolerance | 10000 ppm (1%) | `SIM_BASELINE_V1` |
| REGOUT | 3.3 V | `SIM_BASELINE_V1` |
| TS ADC LSB | 382 µV/LSB | `SOURCE_FIXED` |
| Channel | TS1 | `SOURCE_FIXED` |
| Out-of-range | reject / no extrapolation | `SOURCE_FIXED` |

## 8.2 SIM_NTC_10K_B3950 离散表

> 该表仅用于本项目模拟。由 R25=10k、B=3950 的 Beta 模型生成并四舍五入到整数 Ω。

| Temperature | Resistance | temperature_decic |
|---:|---:|---:|
| -30 °C | 200204 Ω | -300 |
| -20 °C | 105385 Ω | -200 |
| -10 °C | 58246 Ω | -100 |
| +0 °C | 33621 Ω | 0 |
| +10 °C | 20175 Ω | 100 |
| +20 °C | 12535 Ω | 200 |
| +25 °C | 10000 Ω | 250 |
| +30 °C | 8037 Ω | 300 |
| +40 °C | 5301 Ω | 400 |
| +50 °C | 3588 Ω | 500 |
| +60 °C | 2486 Ω | 600 |
| +70 °C | 1760 Ω | 700 |
| +80 °C | 1270 Ω | 800 |
| +90 °C | 934 Ω | 900 |
| +100 °C | 698 Ω | 1000 |

生产代码最终使用：

```text
resistance_ohm
temperature_decic
```

进行严格单调表插值。

---

# 9. 软件电压保护策略

## 9.1 SW OV

| 参数 | 值 |
|---|---:|
| Warning | 4150 mV |
| Fault | **4200 mV** |
| Recovery | **4100 mV** |
| Fault debounce | 500 ms |
| Recovery qualify | 2000 ms |
| FET action | CHG inhibit |
| DSG | allow unless another reason inhibits |

等级：`SIM_POLICY_V1`

## 9.2 SW UV

| 参数 | 值 |
|---|---:|
| Warning | 3100 mV |
| Fault | **3000 mV** |
| Recovery | **3200 mV** |
| Fault debounce | 500 ms |
| Recovery qualify | 2000 ms |
| FET action | DSG inhibit |
| CHG | allow unless another reason inhibits |

等级：`SIM_POLICY_V1`

### 9.3 分层关系

```text
正常区
    ↓
软件保护先动作
    ↓
硬件保护作为独立后备
```

例如：

```text
SW OV 4200 mV
HW OV 4250 mV

SW UV 3000 mV
HW UV 2800 mV
```

---

# 10. 软件过流策略

## 10.1 Charge OC

| 参数 | 值 |
|---|---:|
| Trigger | +5000 mA |
| Delay | 500 ms |
| Recovery threshold | +4500 mA |
| Recovery qualify | 2000 ms |
| Action | CHG inhibit |
| Latch | NO |

## 10.2 Discharge OC

| 参数 | 值 |
|---|---:|
| Trigger | -10000 mA |
| Delay | 500 ms |
| Recovery threshold | > -9000 mA |
| Recovery qualify | 2000 ms |
| Action | DSG inhibit |
| Normal latch | NO |
| Escalation | 3 OCD events / 60 s → DSG latch |
| Escalated reset | explicit service reset |

等级：`SIM_POLICY_V1`

---

# 11. 温度保护策略

## 11.1 Charge temperature

| 条件 | 动作 |
|---|---|
| T ≤ 0 °C | CHG inhibit |
| T ≥ 50 °C | CHG inhibit |
| 恢复低温 | T ≥ 5 °C 持续 2 s |
| 恢复高温 | T ≤ 45 °C 持续 2 s |

## 11.2 Discharge temperature

| 条件 | 动作 |
|---|---|
| T ≤ -20 °C | DSG inhibit |
| T ≥ 60 °C | DSG inhibit |
| 恢复低温 | T ≥ -15 °C 持续 2 s |
| 恢复高温 | T ≤ 55 °C 持续 2 s |

因此方向行为自然形成：

```text
0°C 以下但高于 -20°C
→ 禁止充电
→ 仍允许放电

50~60°C
→ 禁止充电
→ 仍允许放电

≤ -20°C 或 ≥ 60°C
→ CHG + DSG 都被温度原因禁止
```

保护判定 debounce：**1000 ms**

等级：`SIM_POLICY_V1`

---

# 12. Measurement / Freshness / DATA_STALE

源码已有：

| 数据 | Fresh limit |
|---|---:|
| Cell voltage | 1000 ms |
| Current | 1000 ms |
| Temperature | 5000 ms |

采样：

| 项目 | 周期 |
|---|---:|
| SampleTask | 250 ms |
| 温度采样 | 每 8 个 Sample cycle = 2000 ms |

### 模拟 DATA_STALE 策略

任一安全关键测量超过自己的 freshness limit：

```text
DATA_STALE active
CHG inhibit
DSG inhibit
```

新鲜、有效、当前 AFE generation 的数据连续恢复 **2 个 Sample cycle** 后：

```text
DATA_STALE active clear
```

历史事件不要求 latch。

等级：`SIM_POLICY_V1`

---

# 13. 状态机参数

状态：

```text
INIT
STANDBY
CHARGE
DISCHARGE
FAULT
```

**注意：State 只是运行状态分类，不是 FET 安全权限。**

## 13.1 INIT → STANDBY

必须同时满足：

- AFE startup COMPLETE
- current XREADY generation 合法
- calibration valid
- 至少 1 个 current-generation accepted measurement
- 13 cell core valid
- 无阻塞性 safety inhibit

Startup 模拟超时：

```text
5000 ms
```

超时 → FAULT。

## 13.2 STANDBY

```text
|I| < 100 mA
持续 1000 ms
```

## 13.3 CHARGE

进入：

```text
I >= +300 mA
持续 500 ms
```

退出到 STANDBY：

```text
I < +150 mA
持续 1000 ms
```

## 13.4 DISCHARGE

进入：

```text
I <= -300 mA
持续 500 ms
```

退出到 STANDBY：

```text
I > -150 mA
持续 1000 ms
```

## 13.5 FAULT

当存在需要故障状态显示的 safety source 时进入 FAULT。

**FET 最终动作仍由 authoritative directional inhibit reasons 决定，不能通过 `state == FAULT` 推导。**

等级：`SIM_POLICY_V1`

---

# 14. FET 模拟策略

## 14.1 默认请求

| 状态 | CHG request | DSG request |
|---|---:|---:|
| INIT | OFF | OFF |
| STANDBY | ON | ON |
| CHARGE | ON | ON |
| DISCHARGE | ON | ON |
| FAULT | OFF | OFF |

最终输出：

```text
actual permission =
state/request intent
AND
NOT authoritative inhibit reasons
AND
FET manager transaction confirmed
```

所以即使 CHARGE/STANDBY request 为 ON，只要出现方向 inhibit，FET Manager 仍禁止对应方向。

## 14.2 FET Enable ambiguity

如果 ENABLE write finalization ambiguous：

```text
进入 QUARANTINED
禁止 blind enable replay
允许后续 safe-off 尝试
```

等级：`FROZEN`

---

# 15. XREADY 模拟恢复策略

Runtime XREADY：

```text
ProtectTask
→ sole runtime W1C owner
```

Startup exception：

```text
BMS_AfeStartup
```

恢复流程：

```text
PRE_CLEAR_PREPARE
→ PRE_CLEAR_READY
→ WAIT_CLEAR_ACK
→ POST_CLEAR_CONFIG
→ POST_CLEAR_SETTLE
→ POST_CLEAR_VERIFY
→ CALIBRATION_HANDOFF
→ WAIT_FIRST_VALID_SAMPLE
→ COMPLETE
```

从首次 XREADY observation 到 COMPLETE：

```text
CHG inhibit
DSG inhibit
```

### SIM_POLICY_V1：XREADY historical latch reset

为了学习项目能够自动跑通恢复闭环：

```text
技术恢复 COMPLETE
+
current generation first-valid sample accepted
+
无新的 blocking fault
        ↓
允许自动清除 XREADY historical action latch
```

事件历史通过 counter/log 保留，不依赖 latch 永久保存。

如果恢复失败：

```text
保持 both inhibit
```

新 XREADY generation：

```text
旧恢复证据全部失效
重新开始完整流程
```

---

# 16. Fault 全量动作矩阵

所有当前 Fault ID 都在这里明确，不允许“未定义然后临时停项目”。

| Fault | Active action | Latched policy | Recovery |
|---|---|---|---|
| HW_OV | CHG inhibit | no | qualified HW recovery |
| HW_UV | DSG inhibit | no | qualified HW recovery |
| HW_OCD | DSG inhibit | escalation only | qualified recovery |
| HW_SCD | BOTH inhibit | YES | explicit service reset |
| AFE_XREADY | BOTH inhibit | YES during recovery | auto-clear at successful COMPLETE under SIM policy |
| AFE_OVRD_ALERT | BOTH inhibit | YES | explicit service reset after source clear |
| AFE_COMM | BOTH inhibit | latch if continuous >5 s | 3 consecutive successful transactions clear active; latch service-reset |
| AFE_CRC | BOTH inhibit while active | no | 3 consecutive good frames |
| AFE_STALE | reserved / unasserted | no | no Phase9 assertion |
| SW_OV | CHG inhibit | no | voltage recovery qualify |
| SW_UV | DSG inhibit | no | voltage recovery qualify |
| SW_OC_CHARGE | CHG inhibit | no | current recovery qualify |
| SW_OC_DISCHARGE | DSG inhibit | optional escalation | current recovery qualify |
| TEMPERATURE_HIGH | directional | no | temp hysteresis + qualify |
| TEMPERATURE_LOW | directional | no | temp hysteresis + qualify |
| DATA_STALE | BOTH inhibit | no | 2 fresh accepted frames |
| CAN | reviewed NO FET effect | no | communication recovery |
| FLASH_CONFIG | depends on valid compiled fallback | no | config/fallback recovery |
| CLOCK | BOTH inhibit | no | reset/reinit |
| RTOS_HEALTH | BOTH inhibit + stop IWDG feed | no | watchdog reset |

### AFE_COMM 模拟定义

```text
active:
3 consecutive BQ transport failures
OR
no successful required AFE transaction for 1000 ms

continuous latch:
active continuously for 5000 ms
```

等级：`SIM_POLICY_V1`

---

# 17. Task / RTOS 基线

已有任务优先级：

| Task | Priority | Sim period / wait |
|---|---:|---:|
| Protect | 5 | bounded wait ≤100 ms |
| Sample | 4 | 250 ms |
| State | 3 | 100 ms |
| SOC | 3 | 1000 ms |
| Balance | 2 | 1000 ms |
| CAN Tx | 2 | 10 ms service loop；100 ms frame publication |
| CAN Rx | 2 | bounded wait ≤100 ms |

### Heartbeat

每个 required task：

```text
uint32 monotonic generation counter
sole writer = task itself
```

StateTask 只 snapshot / compare，不清零。

---

# 18. IWDG / Health 模拟基线

## 18.1 Required roster

Phase9/后续所有任务实现后：

- Protect
- Sample
- State
- SOC
- Balance
- CAN Tx
- CAN Rx

## 18.2 最大 liveness interval

| Task | Max liveness |
|---|---:|
| Protect | 300 ms |
| Sample | 750 ms |
| State | 300 ms |
| SOC | 2500 ms |
| Balance | 2500 ms |
| CAN Tx | 2500 ms |
| CAN Rx | 500 ms |

## 18.3 Watchdog

| 参数 | 模拟值 |
|---|---:|
| IWDG nominal timeout | **4000 ms** |
| Startup health grace | **5000 ms** |
| Feed owner | **StateTask only** |
| Normal feed opportunity | 每 100 ms State loop |
| Arm condition | baseline + 所有 required task 至少 advance 一次 |

如果任一 required task 超过 max liveness：

```text
RTOS_HEALTH active
both inhibit
StateTask stops feeding IWDG
```

等级：`SIM_POLICY_V1`

---

# 19. SOC 模拟参数

| 参数 | 值 |
|---|---:|
| Nominal capacity | 20000 mAh |
| Initial fallback SOC | 50% |
| Charge coulomb efficiency | 995 / 1000 |
| Discharge efficiency | 1000 / 1000 |
| SOC update period | 1000 ms |
| SOC range | 0…1000 permille |

### 19.1 Startup OCV 粗略表

仅用于**仿真初始 SOC**，运行后以库仑计为主：

| Cell mV | SOC |
|---:|---:|
| ≤3000 | 0% |
| 3300 | 10% |
| 3500 | 20% |
| 3600 | 30% |
| 3700 | 50% |
| 3800 | 65% |
| 3900 | 80% |
| 4000 | 90% |
| 4100 | 97% |
| ≥4200 | 100% |

### 19.2 Full correction

```text
min cell >= 4180 mV
AND
0 <= charge current <= 500 mA
持续 60 s
→ SOC = 100%
```

### 19.3 Empty correction

```text
min cell <= 3050 mV
AND
|discharge current| <= 1000 mA
持续 30 s
→ SOC = 0%
```

等级：`SIM_POLICY_V1`

---

# 20. Balance 模拟参数

| 参数 | 值 |
|---|---:|
| Balance task period | 1000 ms |
| 允许状态 | CHARGE / STANDBY |
| 最低开启电压 | 4100 mV |
| 开启压差 | 20 mV |
| 关闭压差 | 10 mV |
| 关闭低电压 | 4050 mV |
| 最大同时通道 | 2 |
| 相邻 Cell 同时均衡 | 禁止（模拟保守策略） |
| 最大允许温度 | 45 °C |
| 最低允许温度 | 0 °C |
| 最大允许 |I| | 3000 mA |
| Rotation period | 5000 ms |

出现以下任一条件立即关闭均衡：

- FAULT / safety inhibit
- XREADY recovery
- DATA_STALE
- temperature unavailable
- cell data invalid
- DISCHARGE state

等级：`SIM_POLICY_V1`

---

# 21. CAN 模拟参数

CAN bitrate：

```text
500 kbit/s
11-bit Standard ID
```

建议学习项目 message map：

| ID | Direction | 周期 | 内容 |
|---:|---|---:|---|
| 0x180 | TX | 100 ms | BMS state / permission / summary |
| 0x181 | TX | 100 ms | Pack V / Current / SOC |
| 0x182 | TX | 100 ms | Cell min/max / temperature |
| 0x183 | TX | 100 ms | active/latched fault |
| 0x184 | TX | 100 ms | Cell 1…7 |
| 0x185 | TX | 100 ms | Cell 8…13 |
| 0x280 | RX | optional | host command/service request |

RX command timeout：

```text
1000 ms
```

M3 as-built scheduler 每 10 ms service bxCAN hardware path，并每 100 ms 一次
构造和排队上述 6 帧；这是当前 source/test 的真实实现节拍。

本 V1 中：

```text
CAN fault = diagnostic only
NO FET effect
```

因此没有上位机也不阻塞 BMS 核心状态/保护运行。

等级：`SIM_POLICY_V1`

---

# 22. Flash / 参数保存模拟基线

STM32F103C8T6 学习基线按 **64 KiB Flash、1 KiB page** 规划。

建议：

```text
Slot A: 0x0800F800
Slot B: 0x0800FC00
```

即最后两个 1 KiB page。

> 实现前必须检查链接产物没有占用该区域，但这属于软件构建检查，不需要真实硬件批准。

保存结构：

- magic
- config/model version
- monotonic sequence
- SOC
- capacity
- selected persistent counters
- CRC32

策略：

| 参数 | 值 |
|---|---:|
| 两槽 A/B | YES |
| CRC | CRC32 |
| 最小保存间隔 | 60 s |
| SOC 变化触发 | ≥1% |
| 受控关机保存 | YES |
| 两槽都坏 | fallback compiled SIM baseline |

若 fallback compiled SIM baseline 本身校验合法：

```text
FLASH_CONFIG fault 可记录诊断
不要求因为“Flash 从未配置过”停止整个学习项目
```

等级：`SIM_POLICY_V1`

---

# 23. 模拟服务复位策略

为了测试 latch fault，又不依赖真实按键/上位机，定义一个统一的**仿真 service reset 请求**。

它不是 generic clear-all API。

请求只允许让每个 fault source 自己检查自己的 reset 条件：

```text
ServiceResetRequest
        ↓
SCD source 判断条件
OVRD source 判断条件
AFE_COMM latch 判断条件
OCD escalation 判断条件
...
```

不允许：

```text
fault_latched_bitmap = 0;
```

建议 service reset 条件：

- current measurement valid/fresh
- cell measurement valid/fresh
- XREADY inactive/recovery not in progress
- communication healthy
- target source physical/logical active condition已消失
- minimum safe qualify time 2000 ms

---

# 24. Phase 9 Product Policy OPEN 项的模拟解法

为了让学习项目继续，不再让 OP-01…OP-10 卡住代码。

| OP | SIM_POLICY_V1 |
|---|---|
| OP-01 XREADY historical latch | successful COMPLETE 后自动解除 action latch，历史计数保留 |
| OP-02 SCD reset | explicit service reset |
| OP-03 OVRD_ALERT reset | source clear + explicit service reset |
| OP-04 continuous AFE_COMM | 3 failures/1s active；5s continuous latch；service reset |
| OP-05 OCD escalation | 3 events / 60s → DSG latch |
| OP-06 SW OV/UV/OC | 使用本文件第 9/10 节 |
| OP-07 temperature | 使用本文件第 11 节 |
| OP-08 DATA_STALE | both inhibit |
| OP-09 health roster/windows | 使用本文件第 17/18 节 |
| OP-10 IWDG | nominal 4 s，State sole feeder |

这些都是：

```text
SIM_POLICY_V1
```

以后真实项目可以修改，但**当前代码开发可以直接依赖。**

---

# 25. 推荐 Simulator 场景全集

后续不再临时想场景，至少固定覆盖：

| ID | Scenario | 主要预期 |
|---|---|---|
| SIM-01 | normal startup | INIT→STANDBY |
| SIM-02 | standby | both FET permitted |
| SIM-03 | charge transition | STANDBY→CHARGE |
| SIM-04 | discharge transition | STANDBY→DISCHARGE |
| SIM-05 | SW OV | CHG inhibit，恢复后解除 |
| SIM-06 | HW OV | Protect event + recovery handshake |
| SIM-07 | SW UV | DSG inhibit |
| SIM-08 | HW UV | Protect recovery |
| SIM-09 | SW charge OC | CHG inhibit |
| SIM-10 | SW discharge OC | DSG inhibit |
| SIM-11 | HW OCD | DSG inhibit + event counting |
| SIM-12 | HW SCD | BOTH inhibit + latch + service reset |
| SIM-13 | charge cold | CHG off / DSG allowed |
| SIM-14 | charge hot | CHG off |
| SIM-15 | discharge cold | DSG off |
| SIM-16 | discharge hot | DSG off |
| SIM-17 | DATA_STALE | BOTH inhibit |
| SIM-18 | AFE_COMM burst | active / recover |
| SIM-19 | AFE_COMM continuous | latch |
| SIM-20 | XREADY | full generation-aware recovery |
| SIM-21 | second XREADY during recovery | abort/restart |
| SIM-22 | ambiguous FET enable | quarantine / no replay |
| SIM-23 | Sample task stall | RTOS_HEALTH / IWDG |
| SIM-24 | Protect task stall | RTOS_HEALTH / IWDG |
| SIM-25 | Balance start/stop | only eligible cells |
| SIM-26 | SOC charge integration | SOC increases |
| SIM-27 | SOC discharge integration | SOC decreases |
| SIM-28 | SOC full correction | →100% |
| SIM-29 | Flash A/B fallback | newest valid slot |
| SIM-30 | CAN absent | diagnostic only，不影响核心 |
| SIM-31 | multiple simultaneous faults | inhibit reason union |
| SIM-32 | timer wraparound | age/debounce wrap-safe |

---

# 26. Hardware Validation 留到最后

未来有真实板子时，再把以下项从 `REAL_HW_TBD` 升级为 `REAL_HW_VERIFIED`：

### NTC

- 实际型号
- R-T curve
- 实际 bias resistor
- TS1 拓扑
- 实测温度误差

### Current

- 实际 Rsense
- Kelvin layout
- current polarity
- CC accuracy
- zero offset

### AFE protection

- 实际 OV/UV trip
- OCD/SCD threshold
- trip delay
- ALERT timing
- W1C behavior

### FET

- CHG/DSG 电平与实际 MOS 导通关系
- turn-on/off waveform
- 负载瞬态
- body diode 行为

### System

- I2C waveform
- stuck bus
- brownout
- real IWDG timeout/reset
- EMI/ESD
- thermal

这些项目**不会反向阻止当前 simulation/software milestone**。

---

# 27. 以后参数怎么改

本文件采用版本管理：

```text
SIM-HW-POLICY-V1
```

如果后面发现某个模拟值不合理：

```text
修改该值
→ 说明原因
→ bump 文档 revision
→ 重新跑相关 tests
```

不需要因为“以前定错过一个模拟值”重做架构。

建议 revision：

```text
v1.0  初始完整模拟参数基线
v1.1  参数微调，不改变架构
v2.0  真实硬件 profile 或重大策略变化
```

---

# 28. 对现有 Phase 8 Blocker 的重新定位

历史上已有：

```text
P8-BLOCKER-NTC
P8-BLOCKER-AFE
```

历史记录保留，不删除。

但在本学习项目的新流程下，它们重新解释为：

```text
REAL HARDWARE REPLACEMENT / VALIDATION TODO
```

而不是：

```text
SIMULATION SOFTWARE DEVELOPMENT BLOCKER
```

因此：

```text
真实 NTC 未确认
真实 Rsense 未确认
真实保护阈值未确认
```

**不再阻止 Phase 9 simulation implementation。**

---

# 29. 后续开发顺序

建立本基线后，推荐一次性推进：

```text
Simulation Hardware/Profile v1
        ↓
Phase9 Batch A
State + SW protection
        ↓
Phase9 Batch B
FET Manager + recovery + XREADY
        ↓
Phase9 Batch C
Health + IWDG
        ↓
SOC
        ↓
Balance
        ↓
CAN
        ↓
Flash
        ↓
整机 Simulator Integration
        ↓
以后真实板子
        ↓
REAL_HW_VERIFIED 参数替换
```

---

# 30. 本文件的核心原则

> **学习项目首先追求完整、可解释、可验证的软件工程闭环。**
>
> 模拟参数是当前正式开发输入，不是假装量产参数。
>
> 真实硬件来了以后再换参数、抓波形、验证阈值、修正策略。
>
> 从本版本开始，任何普通硬件数值缺失都不再被用作“暂停整个项目”的理由。
