# BMS V1 统一项目方案与软件设计规格

> **项目定位**：13S / 48V 级锂离子电池智能 BMS  
> **主控**：STM32F103C8T6  
> **MCU 开发库**：STM32F10x Standard Peripheral Library（SPL，标准外设库），**不使用 HAL / CubeMX**  
> **RTOS**：FreeRTOS，原生 API  
> **AFE**：TI BQ7694003（BQ76940，3.3V REGOUT、I²C 7-bit 地址 0x08、CRC 版本）  
> **通信**：STM32 bxCAN，500 kbps，自定义 29 位扩展帧协议  
> **电池配置**：13S NMC/常规 4.2V 锂离子体系，标称约 48.1V，满充 54.6V  
> **SOC**：BQ76940 Coulomb Counter + MCU 库仑积分 + OCV 静置校正  
> **均衡**：BQ76940 内部被动均衡开关，V1 每次最多均衡 1 节  
> **保护**：BQ76940 OV/UV/OCD/SCD 硬件保护 + MCU 软件二级保护 + FreeRTOS 故障管理  
> **版本**：V1.0  
> **文档用途**：软件需求规格、系统设计规格、驱动设计规格、Codex 代码生成输入

---

## 0. 文档目的与边界

### 0.1 文档目的

本文档不是“项目介绍稿”，而是用于**真实生成 BMS 软件工程**的实现蓝图。阅读本文档后，应能够完成：

1. STM32F103C8T6 + SPL + FreeRTOS 工程搭建；
2. BQ7694003 软件 I²C + CRC 驱动；
3. BQ76940 初始化、校准、采样、硬件保护配置；
4. 13S 单体电压映射和采样；
5. 电流、温度、总压采集；
6. ALERT 中断及故障处理闭环；
7. CHG / DSG 控制；
8. 软件保护、故障恢复、故障锁定；
9. MCU 侧 SOC 估算；
10. 被动均衡；
11. BMS 状态机；
12. CAN 数据上报、命令接收、参数修改；
13. Flash 参数持久化；
14. I²C、CAN、任务、看门狗等异常恢复；
15. 单元测试、模块测试和系统级验证。

本文档可直接交给 Codex，并要求它按照“文件目录 → 接口 → 数据结构 → 伪代码 → C 实现”的顺序生成工程。

### 0.2 V1 明确包含

- 13 节串联锂离子电池；
- BQ76940 AFE；
- STM32F103C8T6；
- SPL V3.x 风格 API；
- FreeRTOS；
- 软件模拟 I²C；
- BQ76940 CRC；
- BQ76940 ALERT；
- BQ76940 硬件 OV/UV/OCD/SCD；
- MCU 软件过压、欠压、过温、低温、过流预警与二次保护；
- BQ76940 CHG/DSG 低边控制；
- 单体电压、电流、温度；
- SOC；
- 被动均衡；
- CAN；
- Flash 参数；
- IWDG；
- 调试 UART。

### 0.3 V1 明确不包含

以下部分**不应让 Codex 在 V1 中实现**：

- BQ34Z100-G1；
- BQ76200；
- 高边 MOS 驱动；
- 预充 PCHG 功率电路；
- CAN 隔离、TJA1050 电路设计；
- DC/DC、电源完整设计；
- MOSFET 选型、热设计、PCB；
- 充电器识别硬件；
- WiFi、以太网、蓝牙；
- ISO 26262、ASIL 认证；
- AUTOSAR；
- UDS；
- OTA；
- Qmax 自学习的量产级算法；
- SOH 阻抗模型；
- 精确电化学模型。

V1 的目标不是“堆芯片”，而是完成一个**逻辑闭环、接口闭环、故障闭环、任务闭环**的单 AFE 智能 BMS。

---

# 1. 技术基线与统一口径

## 1.1 MCU

采用 STM32F103C8T6：

- Cortex-M3；
- SYSCLK 72 MHz；
- SRAM 20 KB；
- Flash 按官方 C8 资源边界设计为 64 KB；
- HSE 8 MHz；
- AHB 72 MHz；
- APB1 36 MHz；
- APB2 72 MHz；
- bxCAN 位于 APB1；
- USART1 位于 APB2；
- TIM3 用于 1 MHz 微秒时基；
- SysTick 独占给 FreeRTOS，1 ms tick。

> 不依赖“C8 可当 128 KB 使用”这种非正式行为。链接脚本和参数区均按 64 KB Flash 设计。

## 1.2 BQ76940 型号

V1 固定逻辑目标器件：

```text
BQ7694003DBT
```

统一参数：

```text
Cell count:      9~15S，V1 使用 13S
I2C 7-bit addr:  0x08
REGOUT:           3.3V
CRC:              Yes
```

因此软件定义：

```c
#define BQ_I2C_ADDR_7BIT     0x08U
#define BQ_I2C_ADDR_W        0x10U
#define BQ_I2C_ADDR_R        0x11U
#define BQ_USE_CRC           1
```

CRC 多项式：

```text
x^8 + x^2 + x + 1
poly = 0x07
init = 0x00
```

## 1.3 BQ76940 关键事实

统一以下口径，后续代码不得与此冲突：

1. BQ76940 本体支持 9~15 串；
2. 单体电压 ADC 为 14 位；
3. 电流使用独立的 16 位积分型 Coulomb Counter；
4. 单体完整更新周期约 250 ms；
5. CC 连续模式每 250 ms 产生新结果；
6. 温度测量周期约 2 s；
7. CC_READY 也会使 ALERT 置高；
8. ALERT 是 **active-high**；
9. ALERT 是 SYS_STAT 各事件位的 OR；
10. SYS_STAT 为“写 1 清零”；
11. ADCGAIN、ADCOFFSET 必须在初始化时读取；
12. `CC_CFG(0x0B)` 启动时写入 `0x19`；
13. ADC 必须打开，OV/UV 硬件保护才工作；
14. BQ76940 CHG/DSG 是低边 N-MOS 驱动/控制信号；
15. V1 使用 CC ALWAYS ON；
16. BQ 上电/POR 后位于 SHIP 状态，需要 TS1 rising edge boot；
17. boot 后约 1 ms 可 I²C 通信，完整 boot ready 约 10 ms；
18. BQ76940 从 SHIP 进入 NORMAL 后，首次完整电压数据建议等待更长时间，V1 软件启动后等待 800 ms 再把采样标记为可信。

---

# 2. 系统目标

## 2.1 功能目标

| 模块 | V1 目标 |
|---|---|
| 单体采样 | 13S 单体电压 |
| 总压 | 单体求和为主，BQ BAT 寄存器为一致性检查 |
| 电流 | BQ CC + 4 mΩ Rsense 默认配置 |
| 温度 | TS1 外部 10k NTC |
| 硬件保护 | OV / UV / OCD / SCD |
| 软件保护 | OV / UV / OC / OT / UT / AFE_COMM |
| SOC | OCV 初值 + Coulomb Count + 静置校正 |
| 均衡 | 充电状态下被动均衡 |
| MOS | CHG / DSG 软件控制与故障联动 |
| CAN | 周期上报 + 故障上报 + 控制 + 参数读写 |
| 参数 | Flash A/B 冗余保存 |
| RTOS | 七任务 |
| 安全 | ALERT 中断 + IWDG + 通信故障 Fail-Safe |

## 2.2 设计原则

### 原则 A：硬件负责“先保命”

BQ76940 对：

- OV；
- UV；
- OCD；
- SCD；

做硬件比较器保护。

因此软件故障处理任务的作用主要是：

- 读取原因；
- 保存故障；
- 更新状态；
- 决定恢复；
- 上报外部；
- 防止错误重新开启 MOS。

### 原则 B：ISR 必须极短

所有 ISR 只允许：

- 读取最少硬件状态；
- 清 MCU 外设中断；
- `xSemaphoreGiveFromISR`；
- `xQueueSendFromISR`；
- `vTaskNotifyGiveFromISR`；
- `portYIELD_FROM_ISR`。

禁止：

- I²C；
- printf；
- Flash；
- 浮点 SOC；
- 多寄存器读取；
- MOS 复杂状态机；
- 长延时。

### 原则 C：所有共享硬件只有一个并发入口

- BQ I²C：`xI2CMutex`
- BMS 数据：`xDataMutex`
- CAN 硬件发送：只允许 `CANTxTask`
- Flash：只允许参数模块，实际写入由低频调用执行

### 原则 D：驱动不包含业务

例如：

```text
bq76940.c
```

只能知道：

- 寄存器；
- 电压；
- 电流；
- 温度；
- MOS；
- 均衡；
- 硬件故障位。

它不能知道：

- “AGV”
- “SOC 低于 10%”
- “充电状态”
- “CAN 报警格式”
- “故障恢复 3 次”

这些应放在 App 层。

---

# 3. 总体架构

```text
             +--------------------+
             |  External Host     |
             |  PC / AGV / Tool   |
             +----------+---------+
                        |
                    CAN 500k
                        |
+--------------------------------------------------+
|                STM32F103C8T6                     |
|                                                  |
|  +---------------- App Layer -----------------+  |
|  | Protect | Sample | State | SOC | Balance   |  |
|  | CAN TX  | CAN RX                          |  |
|  +-------------------+------------------------+  |
|                      |                           |
|  +-------------- Middleware -----------------+  |
|  | FreeRTOS | CAN Protocol | Param | Fault   |  |
|  +--------------+----------------------------+  |
|                 |                               |
|  +---------------- Driver --------------------+ |
|  | BQ76940 | Soft-I2C | CAN | UART | Flash   | |
|  | EXTI | GPIO | TIM3 us delay | IWDG        | |
|  +-------------------+-------------------------+ |
+----------------------+---------------------------+
                       |
                   I2C + ALERT
                       |
              +--------+--------+
              |    BQ7694003    |
              | 13S / CC / NTC  |
              | OV UV OCD SCD   |
              | CHG DSG BAL     |
              +-----------------+
```

---

# 4. 硬件接口抽象

外围电路本版本不展开，但软件必须假设下列硬件连接已经正确完成。

## 4.1 建议引脚定义

| 功能 | STM32 引脚 | 说明 |
|---|---|---|
| BQ I2C SCL | PB8 | GPIO 开漏，软件 I²C |
| BQ I2C SDA | PB9 | GPIO 开漏，软件 I²C |
| BQ ALERT | PB1 | EXTI1，上升沿 |
| BQ WAKE | PA8 | 通过板级电路向 TS1 产生 rising edge |
| CAN RX | PA11 | CAN1_RX |
| CAN TX | PA12 | CAN1_TX |
| UART1 TX | PA9 | 调试 |
| UART1 RX | PA10 | 调试 |
| SWDIO | PA13 | 调试 |
| SWCLK | PA14 | 调试 |
| LED | PC13 或板级自定义 | 心跳 |
| KEY | 可选 | V1 非必需 |

> PB2 不作为 ALERT，避免与 BOOT1 上电采样产生不必要的硬件耦合。

## 4.2 软件 I²C 电气假设

- SCL/SDA 有外部上拉；
- MCU GPIO 配置 Open-Drain；
- 写 0 = 主动拉低；
- 写 1 = 释放，由上拉拉高；
- 每次释放 SCL/SDA 后都允许读取线电平；
- 软件 I²C 目标约 100 kHz；
- 不追求 400 kHz。

## 4.3 微秒延时

禁止用：

```c
for (...) __NOP();
```

作为正式 I²C 时基。

统一使用 TIM3：

```text
TIM3 input clock = 72 MHz
Prescaler = 71
counter clock = 1 MHz
1 count = 1 us
```

接口：

```c
void BSP_TIM3_UsTickInit(void);
uint16_t BSP_UsTickGet(void);
void BSP_DelayUs(uint16_t us);
```

`BSP_DelayUs()` 仅用于几十微秒量级驱动时序，不应用于任务级延时。

---

# 5. 13S BQ76940 通道映射

这是 Codex 实现时最容易出错的部分之一。

BQ76940 不是在 13S 下简单使用 VC1~VC13。

13S 物理连接映射：

| 逻辑 Cell | BQ 差分输入 | BQ ADC Channel |
|---:|---|---:|
| 1 | VC1-VC0 | VC1 |
| 2 | VC2-VC1 | VC2 |
| 3 | VC3-VC2 | VC3 |
| 4 | VC4-VC3 | VC4 |
| 5 | VC5-VC4 | VC5 |
| 6 | VC6-VC5B | VC6 |
| 7 | VC7-VC6 | VC7 |
| 8 | VC8-VC7 | VC8 |
| - | VC9-VC8 | short |
| 9 | VC10-VC9 | VC10 |
| 10 | VC11-VC10B | VC11 |
| 11 | VC12-VC11 | VC12 |
| 12 | VC13-VC12 | VC13 |
| - | VC14-VC13 | short |
| 13 | VC15-VC14 | VC15 |

软件必须定义：

```c
static const uint8_t g_cell_adc_channel[13] =
{
    1, 2, 3, 4, 5,
    6, 7, 8,
    10, 11, 12, 13, 15
};
```

禁止：

```c
for (i = 0; i < 13; ++i)
    read VC(i+1);
```

因为这样 Cell9、Cell13 映射错误。

## 5.1 均衡 bit 映射

逻辑 Cell → BQ CELLBAL bit：

```text
Cell1  -> CB1
Cell2  -> CB2
Cell3  -> CB3
Cell4  -> CB4
Cell5  -> CB5

Cell6  -> CB6
Cell7  -> CB7
Cell8  -> CB8
Cell9  -> CB10

Cell10 -> CB11
Cell11 -> CB12
Cell12 -> CB13
Cell13 -> CB15
```

定义：

```c
static const uint8_t g_cell_cb_index[13] =
{
    1,2,3,4,5,
    6,7,8,10,
    11,12,13,15
};
```

---

# 6. 工程目录

Codex 应生成如下目录，不要把所有代码塞入 `main.c`。

```text
BMS_V1/
├─ App/
│  ├─ app_main.c
│  ├─ app_tasks.c
│  ├─ app_tasks.h
│  ├─ bms_data.c
│  ├─ bms_data.h
│  ├─ bms_state.c
│  ├─ bms_state.h
│  ├─ bms_fault.c
│  ├─ bms_fault.h
│  ├─ bms_soc.c
│  ├─ bms_soc.h
│  ├─ bms_balance.c
│  ├─ bms_balance.h
│  ├─ bms_protect.c
│  └─ bms_protect.h
│
├─ Protocol/
│  ├─ bms_can_protocol.c
│  ├─ bms_can_protocol.h
│  ├─ bms_command.c
│  └─ bms_command.h
│
├─ Driver/
│  ├─ bq76940.c
│  ├─ bq76940.h
│  ├─ soft_i2c.c
│  ├─ soft_i2c.h
│  ├─ crc8.c
│  ├─ crc8.h
│  ├─ bsp_can.c
│  ├─ bsp_can.h
│  ├─ bsp_uart.c
│  ├─ bsp_uart.h
│  ├─ bsp_gpio.c
│  ├─ bsp_gpio.h
│  ├─ bsp_exti.c
│  ├─ bsp_exti.h
│  ├─ bsp_timer.c
│  ├─ bsp_timer.h
│  ├─ bsp_flash.c
│  ├─ bsp_flash.h
│  ├─ bsp_iwdg.c
│  └─ bsp_iwdg.h
│
├─ Service/
│  ├─ param_store.c
│  ├─ param_store.h
│  ├─ system_health.c
│  └─ system_health.h
│
├─ Config/
│  ├─ bms_config.h
│  ├─ FreeRTOSConfig.h
│  └─ can_protocol_cfg.h
│
├─ FreeRTOS/
│  └─ ...
│
├─ Libraries/
│  ├─ CMSIS/
│  └─ STM32F10x_StdPeriph_Driver/
│
└─ User/
   ├─ main.c
   ├─ stm32f10x_it.c
   └─ system_stm32f10x.c
```

---

# 7. 编码约束

## 7.1 禁止项

Codex 不得生成：

```c
HAL_GPIO_WritePin(...)
HAL_CAN_Start(...)
HAL_I2C_Master_Transmit(...)
osThreadNew(...)
osMutexAcquire(...)
```

V1 必须使用：

```c
GPIO_Init(...)
CAN_Init(...)
USART_Init(...)
FLASH_ProgramHalfWord(...)
xTaskCreate(...)
xSemaphoreCreateMutex(...)
xQueueCreate(...)
```

即：

- SPL；
- 原生 FreeRTOS；
- 不使用 CMSIS-RTOS2 封装。

## 7.2 C 语言

建议：

```text
C99
stdint.h
stdbool.h
```

避免：

- 大量动态 malloc；
- 任务运行后创建/删除对象；
- 在 ISR 使用浮点；
- 巨型局部数组；
- 无界字符串；
- sprintf 进入关键任务。

---

# 8. 全局配置

`Config/bms_config.h` 建议：

```c
#define BMS_CELL_COUNT                  13U

#define BMS_RSENSE_UOHM                 4000L

#define BMS_SAMPLE_PERIOD_MS            250U
#define BMS_TEMP_PERIOD_MS              2000U
#define BMS_STATE_PERIOD_MS             100U
#define BMS_BALANCE_PERIOD_MS           1000U

#define BMS_CAN_BITRATE                 500000UL
#define BMS_CAN_STATUS_PERIOD_MS        100U
#define BMS_CAN_CELL_PERIOD_MS          500U
#define BMS_CAN_HEARTBEAT_PERIOD_MS     200U

#define BMS_STATE_ENTER_CURRENT_MA      300
#define BMS_STATE_EXIT_CURRENT_MA       100
#define BMS_STATE_DEBOUNCE_MS           500U

#define BMS_SW_OV_WARN_MV               4150U
#define BMS_SW_OV_CUTOFF_MV             4200U
#define BMS_SW_OV_RECOVER_MV            4100U

#define BMS_SW_UV_WARN_MV               3100U
#define BMS_SW_UV_CUTOFF_MV             3000U
#define BMS_SW_UV_RECOVER_MV            3200U

#define BMS_HW_OV_DEFAULT_MV             4250U
#define BMS_HW_UV_DEFAULT_MV             2800U

#define BMS_CHG_OT_C                     50
#define BMS_DSG_OT_C                     60
#define BMS_CHG_UT_C                      0
#define BMS_DSG_UT_C                    -20

#define BMS_BALANCE_START_DELTA_MV       50U
#define BMS_BALANCE_STOP_DELTA_MV        30U
#define BMS_BALANCE_MIN_CELL_MV        4000U
#define BMS_BALANCE_MAX_TEMP_C           45

#define BMS_COMM_FAIL_LIMIT               3U

#define BMS_SOC_SCALE                  1000U   /* 0~1000 = 0.0~100.0% */

#define BMS_DEFAULT_CAPACITY_MAH       20000U  /* 示例，必须允许参数修改 */

#define BMS_PARAM_VERSION                 1U
```

> 上述阈值是“软件工程默认配置”，不是对所有电池包都正确。真实硬件必须按电芯、MOS、Rsense、线束、负载重新定标。

---

# 9. 核心数据模型

## 9.1 BMS 状态

```c
typedef enum
{
    BMS_STATE_INIT = 0,
    BMS_STATE_STANDBY,
    BMS_STATE_CHARGE,
    BMS_STATE_DISCHARGE,
    BMS_STATE_FAULT
} BMS_State_t;
```

V1 暂不实现真实深度休眠状态，避免把 TS1/SHIP/系统唤醒硬件复杂化。

V1.1 可增加：

```c
BMS_STATE_SLEEP
```

## 9.2 MOS 状态

```c
typedef struct
{
    uint8_t chg_cmd;
    uint8_t dsg_cmd;
    uint8_t chg_actual;
    uint8_t dsg_actual;
} BMS_MosState_t;
```

其中：

- `cmd`：软件希望的状态；
- `actual`：根据 SYS_CTRL2 读回和 fault 推断的状态。

## 9.3 采样有效性

```c
typedef struct
{
    uint8_t voltage_valid;
    uint8_t current_valid;
    uint8_t temperature_valid;
    uint8_t afe_online;
    uint8_t sample_ready;

    uint32_t sample_seq;
    TickType_t last_voltage_tick;
    TickType_t last_current_tick;
    TickType_t last_temperature_tick;
} BMS_DataValidity_t;
```

## 9.4 核心数据结构

```c
typedef struct
{
    uint16_t cell_mv[BMS_CELL_COUNT];

    uint16_t cell_min_mv;
    uint16_t cell_max_mv;
    uint8_t  cell_min_index;
    uint8_t  cell_max_index;
    uint16_t cell_delta_mv;

    uint32_t pack_mv_sum;
    uint32_t pack_mv_bq;

    int32_t  current_ma;

    int16_t  temp_c_x10;

    uint16_t soc_permille;
    int32_t  remain_mah;
    uint32_t full_capacity_mah;

    BMS_State_t state;
    BMS_MosState_t mos;

    uint32_t fault_active;
    uint32_t fault_latched;
    uint32_t warning_bits;

    uint16_t balance_bitmap;

    BMS_DataValidity_t valid;

    uint32_t i2c_error_count;
    uint32_t afe_comm_fail_count;
    uint32_t can_tx_fail_count;
    uint32_t cc_missed_count;

} BMS_Data_t;

extern BMS_Data_t g_bms;
```

所有任务读取 `g_bms` 时必须使用：

```c
xDataMutex
```

推荐做法：

```c
BMS_Data_t local;

xSemaphoreTake(xDataMutex, ...);
local = g_bms;
xSemaphoreGive(xDataMutex);

/* 后续计算用 local */
```

不要持锁做复杂算法。

---

# 10. 故障定义

## 10.1 Active 与 Latched 分离

必须区分：

- `fault_active`：当前条件仍存在；
- `fault_latched`：历史严重故障，需要人工/命令复位。

```c
#define FAULT_HW_OV            (1UL << 0)
#define FAULT_HW_UV            (1UL << 1)
#define FAULT_HW_OCD           (1UL << 2)
#define FAULT_HW_SCD           (1UL << 3)

#define FAULT_SW_OV            (1UL << 4)
#define FAULT_SW_UV            (1UL << 5)
#define FAULT_SW_OC            (1UL << 6)
#define FAULT_SW_OT            (1UL << 7)
#define FAULT_SW_UT            (1UL << 8)

#define FAULT_AFE_COMM         (1UL << 9)
#define FAULT_AFE_XREADY       (1UL << 10)
#define FAULT_DATA_INVALID     (1UL << 11)
#define FAULT_CAN              (1UL << 12)
#define FAULT_INTERNAL         (1UL << 13)
```

## 10.2 Warning

```c
#define WARN_CELL_HIGH         (1UL << 0)
#define WARN_CELL_LOW          (1UL << 1)
#define WARN_TEMP_HIGH         (1UL << 2)
#define WARN_TEMP_LOW          (1UL << 3)
#define WARN_CELL_IMBALANCE    (1UL << 4)
#define WARN_CAN_TIMEOUT       (1UL << 5)
```

---

# 11. FreeRTOS 架构

## 11.1 七任务

| 任务 | 优先级 | 触发 | 主要职责 |
|---|---:|---|---|
| ProtectTask | 5 | AFE ALERT Semaphore | SYS_STAT、CC_READY、故障、恢复 |
| SampleTask | 4 | 250 ms | Cell/BAT/TS 采样 |
| StateTask | 3 | 100 ms | 状态机、软件保护、监督、IWDG |
| SOCTask | 3 | CC Queue + 1s timeout | SOC |
| BalanceTask | 2 | 1 s | 被动均衡 |
| CANTxTask | 2 | TX Queue + 周期 | CAN 唯一发送者 |
| CANRxTask | 2 | RX Queue | 解析命令 |

FreeRTOS Idle Task 自动存在，优先级 0。

## 11.2 同步对象

```c
SemaphoreHandle_t xI2CMutex;
SemaphoreHandle_t xDataMutex;
SemaphoreHandle_t xAfeAlertSem;

QueueHandle_t xCanTxQueue;
QueueHandle_t xCanRxQueue;
QueueHandle_t xCcSampleQueue;

EventGroupHandle_t xSysEvents;
```

事件位：

```c
#define EVT_SAMPLE_READY      (1U << 0)
#define EVT_AFE_ONLINE        (1U << 1)
#define EVT_FAULT_PRESENT     (1U << 2)
#define EVT_PARAM_DIRTY       (1U << 3)
```

## 11.3 队列元素

```c
typedef struct
{
    int16_t raw;
    TickType_t tick;
} BMS_CcSample_t;

typedef struct
{
    uint32_t ext_id;
    uint8_t dlc;
    uint8_t data[8];
} BMS_CanFrame_t;
```

队列长度：

```c
xCanTxQueue   = xQueueCreate(24, sizeof(BMS_CanFrame_t));
xCanRxQueue   = xQueueCreate(12, sizeof(BMS_CanFrame_t));
xCcSampleQueue= xQueueCreate(8,  sizeof(BMS_CcSample_t));
```

## 11.4 FreeRTOS Heap

建议：

```c
#define configTOTAL_HEAP_SIZE (8 * 1024)
#define configUSE_PREEMPTION  1
#define configTICK_RATE_HZ    1000
#define configMAX_PRIORITIES  8
#define configUSE_MUTEXES     1
#define configCHECK_FOR_STACK_OVERFLOW 2
```

任务栈单位为 word：

| 任务 | words |
|---|---:|
| Protect | 160 |
| Sample | 192 |
| State | 128 |
| SOC | 192 |
| Balance | 160 |
| CAN TX | 160 |
| CAN RX | 160 |

## 11.5 中断优先级

推荐：

```c
#define configPRIO_BITS 4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
```

使用：

```c
NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
```

允许调用 FreeRTOS `FromISR` API 的 ISR，例如：

```text
EXTI1 ALERT: preemption priority 6
USB_LP_CAN1_RX0: priority 7
```

不得给这些 ISR 设置 0~4 后再调用 FreeRTOS API。

---

# 12. 启动流程

## 12.1 总体启动状态

```text
RESET
  |
  +--> SystemInit / Clock 72MHz
  |
  +--> NVIC grouping
  |
  +--> GPIO
  +--> TIM3 1MHz
  +--> UART1 115200
  +--> Soft I2C
  +--> CAN1 500k
  +--> ALERT EXTI
  +--> IWDG
  |
  +--> RTOS objects
  |
  +--> BQ76940 Init (scheduler start 前执行)
  |
  +--> Create 7 Tasks
  |
  +--> vTaskStartScheduler()
```

## 12.2 为什么 BQ 初始化建议调度器前执行

优点：

- 初始化过程单线程；
- 无需 I²C mutex；
- 错误路径简单；
- MOS 安全状态可以先建立；
- 启动后任务直接进入稳定结构。

BQ 初始化失败时：

```text
Retry 1
  -> bus recovery
  -> wake
Retry 2
Retry 3
  |
  +-- still fail --> keep CHG/DSG OFF
                   set startup_error
                   start RTOS only for CAN/diagnostic
```

不要：

```c
while(1);
```

完全失去诊断能力。

## 12.3 BQ 初始化顺序

```text
1. BQ_Wake()
2. delay > 10ms
3. Probe I2C
4. clear SYS_STAT known bits
5. write CC_CFG = 0x19
6. read ADCGAIN1
7. read ADCGAIN2
8. read ADCOFFSET
9. calculate adc_gain_uv_per_lsb
10. configure OV_TRIP
11. configure UV_TRIP
12. configure PROTECT3
13. configure PROTECT1
14. configure PROTECT2
15. SYS_CTRL1: ADC_EN=1, TEMP_SEL=1
16. SYS_CTRL2: CC_EN=1, CHG=0, DSG=0
17. clear SYS_STAT
18. wait initial BQ76940 data stabilization
19. read initial cell set
20. validate 13 cells
21. if valid and no fault:
       enable CHG/DSG
```

---

# 13. 软件 I²C 驱动

## 13.1 接口

```c
void SoftI2C_Init(void);
bool SoftI2C_BusRecover(void);

bool SoftI2C_Start(void);
void SoftI2C_Stop(void);

bool SoftI2C_WriteByte(uint8_t byte);
uint8_t SoftI2C_ReadByte(bool ack);

bool SoftI2C_Write(
    uint8_t addr7,
    const uint8_t *buf,
    uint16_t len);

bool SoftI2C_WriteRead(
    uint8_t addr7,
    const uint8_t *wbuf,
    uint16_t wlen,
    uint8_t *rbuf,
    uint16_t rlen);
```

## 13.2 总线恢复

当检测：

```text
SDA = LOW while idle
```

执行：

```text
release SDA
for 9 times:
    SCL low
    5us
    SCL high
    wait high with timeout
    5us

generate STOP
check SDA high
```

若失败：

- `afe_comm_fail_count++`
- 禁止本次事务；
- 上层可重新初始化 BQ；
- 连续 3 次失败进入 `FAULT_AFE_COMM`。

## 13.3 Clock stretching

虽然本项目不主动依赖 stretching，但 SCL 释放后应：

```c
while (SCL_Read() == 0)
{
    if (timeout) error;
}
```

不要假定 SCL 写 1 后立即为 1。

---

# 14. BQ76940 CRC I²C

## 14.1 CRC 接口

```c
uint8_t CRC8_BQ(const uint8_t *data, uint8_t len);
```

poly：

```c
0x07
```

init：

```c
0x00
```

## 14.2 单字节写

抽象：

```c
bool BQ_WriteReg(uint8_t reg, uint8_t value);
```

逻辑 CRC 输入：

```text
[slave_address_write][register][data]
```

随后发送：

```text
START
ADDR_W
REG
DATA
CRC
STOP
```

## 14.3 Block Write

第一个 data 的 CRC：

```text
ADDR_W + REG + DATA0
```

后续每个 data 的 CRC：

```text
DATAi
```

## 14.4 Block Read

对 CRC 版本必须按 BQ76940 的 transaction 规则读取。

V1 建议统一封装：

```c
bool BQ_ReadReg(uint8_t reg, uint8_t *value);
bool BQ_ReadBlock(uint8_t start_reg, uint8_t *buf, uint8_t len);
```

应用层永远不得自己组 BQ CRC。

---

# 15. BQ76940 寄存器子集

```c
#define BQ_REG_SYS_STAT        0x00
#define BQ_REG_CELLBAL1        0x01
#define BQ_REG_CELLBAL2        0x02
#define BQ_REG_CELLBAL3        0x03
#define BQ_REG_SYS_CTRL1       0x04
#define BQ_REG_SYS_CTRL2       0x05
#define BQ_REG_PROTECT1        0x06
#define BQ_REG_PROTECT2        0x07
#define BQ_REG_PROTECT3        0x08
#define BQ_REG_OV_TRIP         0x09
#define BQ_REG_UV_TRIP         0x0A
#define BQ_REG_CC_CFG          0x0B

#define BQ_REG_VC1_HI          0x0C
/* ... */
#define BQ_REG_VC15_LO         0x29

#define BQ_REG_BAT_HI          0x2A
#define BQ_REG_BAT_LO          0x2B

#define BQ_REG_TS1_HI          0x2C
#define BQ_REG_TS1_LO          0x2D
#define BQ_REG_TS2_HI          0x2E
#define BQ_REG_TS2_LO          0x2F
#define BQ_REG_TS3_HI          0x30
#define BQ_REG_TS3_LO          0x31

#define BQ_REG_CC_HI           0x32
#define BQ_REG_CC_LO           0x33

#define BQ_REG_ADCGAIN1        0x50
#define BQ_REG_ADCOFFSET       0x51
#define BQ_REG_ADCGAIN2        0x59
```

SYS_STAT：

```c
#define BQ_STAT_CC_READY       (1U << 7)
#define BQ_STAT_DEVICE_XREADY  (1U << 5)
#define BQ_STAT_OVRD_ALERT     (1U << 4)
#define BQ_STAT_UV             (1U << 3)
#define BQ_STAT_OV             (1U << 2)
#define BQ_STAT_SCD            (1U << 1)
#define BQ_STAT_OCD            (1U << 0)
```

SYS_CTRL2：

```c
#define BQ_CTRL2_CC_EN         (1U << 6)
#define BQ_CTRL2_DSG_ON        (1U << 1)
#define BQ_CTRL2_CHG_ON        (1U << 0)
```

---

# 16. ADC Gain / Offset

## 16.1 Gain

```text
ADCGAIN<4:0>:
  bits [4:3] from ADCGAIN1
  bits [2:0] from ADCGAIN2

GAIN = 365 + ADCGAIN_code  [uV/LSB]
```

范围约：

```text
365~396 uV/LSB
```

## 16.2 Offset

```text
ADCOFFSET: int8_t
unit = mV
```

结构：

```c
typedef struct
{
    uint16_t adc_gain_uv;
    int8_t adc_offset_mv;
} BQ_Calibration_t;
```

---

# 17. 单体电压采集

## 17.1 原始值

每个 VC：

```text
HI: lower 6 bits effective
LO: 8 bits
```

组合：

```c
raw14 = ((uint16_t)(hi & 0x3F) << 8) | lo;
```

## 17.2 电压转换

```text
Vcell(mV) =
    raw14 * gain(uV/LSB) / 1000
    + offset(mV)
```

建议使用 32 位整数：

```c
int32_t BQ_ConvertCellMv(uint16_t raw)
{
    return ((int32_t)raw * g_bq_cal.adc_gain_uv) / 1000
           + g_bq_cal.adc_offset_mv;
}
```

## 17.3 合法性

每个逻辑电芯 V1 判定：

```text
2000 mV <= Vcell <= 5000 mV
```

初始化时更严格：

```text
2500~4300 mV
```

若单个通道异常：

- 标记数据无效；
- 不立即用错误数据做 SOC；
- 软件保护可进入 Data Invalid；
- 连续多周期异常才升级 Fault。

---

# 18. 总压

V1 同时维护两种总压：

```c
pack_mv_sum = sum(cell_mv[0..12]);
pack_mv_bq  = BQ BAT register conversion;
```

决策优先级：

```text
pack_mv_sum = 主值
pack_mv_bq  = 诊断值
```

如果：

```text
abs(pack_mv_sum - pack_mv_bq) > PACK_VOLT_DIAG_THRESHOLD
```

持续多个周期，产生 warning。

V1 不声称该差异可直接诊断所有“虚接”，仅作为采样一致性诊断。

---

# 19. 电流采集

## 19.1 Rsense

V1 默认：

```text
Rsense = 4 mΩ
```

必须参数化。

## 19.2 CC

BQ CC：

```text
16-bit signed
LSB ~= 8.44 uV
integration period = 250ms
```

读取：

```c
int16_t raw =
    (int16_t)(((uint16_t)cc_hi << 8) | cc_lo);
```

## 19.3 电流符号

V1 统一：

```text
current_ma > 0 : charging
current_ma < 0 : discharging
```

若板级实际极性相反，仅修改：

```c
#define BMS_CURRENT_POLARITY (-1)
```

## 19.4 换算

```text
Vsense(uV) = raw * 8.44
I(mA) = Vsense(uV) / Rsense(mΩ)
```

4mΩ：

```text
1 LSB ≈ 2.11mA
```

为了避免浮点，使用：

```c
current_ma =
    ((int32_t)raw * 8440L) / BMS_RSENSE_UOHM;
```

其中：

```text
8440 nV / LSB
Rsense in uΩ
```

实现前必须人工验证单位。

---

# 20. ALERT 与 CC_READY 的关键设计

这是 V1 的重要架构点。

因为：

```text
ALERT = OR(SYS_STAT bits)
```

且：

```text
CC_READY 每 250ms 会置位
```

所以：

> ALERT 不是“纯故障引脚”。

## 20.1 ISR

PB1 EXTI1 上升沿：

```c
void EXTI1_IRQHandler(void)
{
    BaseType_t hpw = pdFALSE;

    if (EXTI_GetITStatus(EXTI_Line1) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line1);

        xSemaphoreGiveFromISR(xAfeAlertSem, &hpw);

        portYIELD_FROM_ISR(hpw);
    }
}
```

ISR 不读取 BQ。

## 20.2 ProtectTask

ProtectTask 被 ALERT 唤醒后：

```text
Take I2C mutex
Read SYS_STAT

if CC_READY:
    Read CC_HI/LO immediately
    push to xCcSampleQueue
    mark CC_READY clear mask

if OCD/SCD/OV/UV/XREADY:
    map hardware fault
    apply software safety state
    report fault

write-1-to-clear handled bits
release I2C mutex
```

必须允许一次 SYS_STAT 同时包含：

```text
CC_READY + OV
```

所以不能：

```c
if (CC_READY) return;
```

必须逐 bit 处理。

---

# 21. 温度采集

BQ 温度 ADC 的硬件更新周期约 2 s。

因此：

- SampleTask 250 ms 运行；
- 每 8 次运行读取一次 TS1。

## 21.1 TS1 ADC

```text
Vts = raw * 382 uV
```

内部 thermistor bias 约 10k。

```text
Rntc = 10000 * Vts / (3.3 - Vts)
```

正式代码不建议仅使用随意 β 常数。

接口：

```c
int16_t NTC_ResistanceToTempX10(uint32_t ohm);
```

V1 推荐使用：

```c
typedef struct
{
    uint32_t resistance_ohm;
    int16_t temp_x10;
} NTC_Point_t;
```

查表 + 线性插值。

---

# 22. BQ 硬件保护配置

## 22.1 OV / UV

OV_TRIP / UV_TRIP 必须根据本芯片 Gain/Offset 计算。

不能把：

```c
0xBF
```

写死并认为它永远等于某个电压。

完整 ADC code：

```text
full = (target_voltage - offset) / gain
```

单位要统一。

然后取中间 8 bit：

```c
trip = (full >> 4) & 0xFF;
```

但代码必须额外验证 upper/lower 固定位映射符合 OV/UV 的合法范围。

建议 API：

```c
bool BQ_ConfigureOV(uint16_t mv, BQ_OvDelay_t delay);
bool BQ_ConfigureUV(uint16_t mv, BQ_UvDelay_t delay);
```

默认：

```text
HW OV = 4250mV
HW UV = 2800mV
```

## 22.2 OCD / SCD

以 4mΩ 为默认：

### OCD 示例

选择：

```text
threshold input ≈ 56mV
I ≈ 14A
delay = 80ms
```

### SCD 示例

选择：

```text
threshold input ≈ 111mV
I ≈ 27.75A
delay = 100us
```

这些只是 V1 默认值。

应建立表驱动：

```c
typedef struct
{
    uint16_t threshold_mv;
    uint8_t code;
} BQ_ThresholdCode_t;
```

根据目标电流：

```text
Vsense_target = I_target * Rsense
```

选择“不低于或最接近安全策略”的合法档位。

## 22.3 软件保护必须比硬件保护更保守

示例：

```text
SW OV cutoff : 4200mV
HW OV cutoff : 4250mV

SW OC        : 10A, 500ms
HW OCD       : 14A, 80ms
HW SCD       : 27.75A, 100us
```

逻辑：

```text
software tries to intervene first
hardware is the final independent safety layer
```

---

# 23. CHG / DSG 控制

## 23.1 驱动 API

```c
bool BQ_SetChargeFet(bool enable);
bool BQ_SetDischargeFet(bool enable);
bool BQ_SetBothFets(bool chg, bool dsg);

bool BQ_ReadFetState(bool *chg, bool *dsg);
```

实现为 SYS_CTRL2 read-modify-write。

必须保留：

```text
CC_EN
```

不可每次修改 MOS 时误清掉 `CC_EN`。

错误：

```c
BQ_WriteReg(SYS_CTRL2, BQ_CTRL2_CHG_ON);
```

因为可能把 CC_EN 清除。

正确：

```c
uint8_t ctrl2;
BQ_ReadReg(SYS_CTRL2, &ctrl2);
ctrl2 |= ...;
ctrl2 &= ...;
BQ_WriteReg(SYS_CTRL2, ctrl2);
```

## 23.2 正常状态

V1 正常待机：

```text
CHG = ON
DSG = ON
current ~= 0
state = STANDBY
```

不是把两颗 MOS 都关掉。

否则系统无法自然检测负载/充电电流。

---

# 24. 状态机

## 24.1 状态

```text
INIT
STANDBY
CHARGE
DISCHARGE
FAULT
```

## 24.2 INIT

条件：

- BQ online；
- 13 节有效；
- 无 latched fault；
- 初始采样完成。

成功：

```text
CHG ON
DSG ON
-> STANDBY
```

失败：

```text
CHG OFF
DSG OFF
-> FAULT
```

## 24.3 STANDBY

条件：

```text
|I| < 100mA
```

若：

```text
I > +300mA for 500ms
```

进入：

```text
CHARGE
```

若：

```text
I < -300mA for 500ms
```

进入：

```text
DISCHARGE
```

## 24.4 CHARGE

退出到 STANDBY：

```text
|I| < 100mA for 500ms
```

故障：

- OV；
- Charge OT；
- Charge UT；
- AFE fault；

根据 fault action 关闭 CHG。

## 24.5 DISCHARGE

退出到 STANDBY：

```text
|I| < 100mA for 500ms
```

故障：

- UV；
- OCD；
- SCD；
- Discharge OT；
- Discharge UT；

关闭 DSG。

## 24.6 FAULT

状态不是简单“一故障全部锁死”。

必须由 fault action 决定：

| Fault | CHG | DSG |
|---|---|---|
| OV | OFF | 可 ON |
| UV | 可 ON | OFF |
| OCD | 可 ON | OFF |
| SCD | OFF | OFF |
| OT severe | OFF | OFF |
| UT charge only | OFF | 可 ON |
| AFE COMM | OFF | OFF |
| XREADY | OFF | OFF |

当 fault active 清除且无 latched fault：

```text
recover -> STANDBY
```

---

# 25. 软件保护

StateTask 每 100ms 对本地 snapshot 做软件保护。

## 25.1 单体 OV

```text
warning:
cell_max >= 4150

soft fault:
cell_max >= 4200
持续 1s
```

动作：

```text
CHG OFF
```

恢复：

```text
cell_max <= 4100
持续 5s
```

## 25.2 UV

```text
warning:
cell_min <= 3100

soft fault:
cell_min <= 3000
持续 1s
```

动作：

```text
DSG OFF
```

恢复：

```text
cell_min >= 3200
持续 5s
```

## 25.3 过流

软件：

```text
abs(discharge current) > 10A
持续 500ms
```

动作：

```text
DSG OFF
```

硬件 OCD/SCD 保持独立。

## 25.4 温度

充电：

```text
T >= 50°C -> CHG OFF
T <= 0°C  -> CHG OFF
```

放电：

```text
T >= 60°C -> DSG OFF
T <= -20°C -> DSG OFF
```

温度恢复必须加入 5°C 左右迟滞。

---

# 26. 故障锁定与恢复

## 26.1 可自动恢复

- SW OV；
- SW UV；
- HW OV；
- HW UV；
- 单次 OCD；
- OT/UT；
- 临时 I²C 错误。

## 26.2 条件锁定

OCD：

```text
10 min 内 >= 3 次
```

设置：

```text
fault_latched |= FAULT_HW_OCD;
```

## 26.3 永久锁定直到明确 reset

- SCD；
- XREADY；
- 连续 AFE 通信失败；
- internal fatal。

恢复需要：

```text
CAN reset command
AND
physical measurements safe
AND
BQ online
```

不能仅收到 CAN 命令就无条件重开 MOS。

---

# 27. ProtectTask 伪代码

```c
void Task_Protect(void *arg)
{
    for (;;)
    {
        xSemaphoreTake(xAfeAlertSem, portMAX_DELAY);

        if (xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(20)) != pdTRUE)
            continue;

        uint8_t stat = 0;
        if (!BQ_ReadReg(BQ_REG_SYS_STAT, &stat))
        {
            xSemaphoreGive(xI2CMutex);
            BMS_Fault_ReportAfeComm();
            continue;
        }

        uint8_t clear_mask = 0;

        if (stat & BQ_STAT_CC_READY)
        {
            int16_t raw;
            if (BQ_ReadCcRaw(&raw))
            {
                BMS_CcSample_t s = {
                    .raw = raw,
                    .tick = xTaskGetTickCount()
                };

                if (xQueueSend(xCcSampleQueue, &s, 0) != pdPASS)
                {
                    BMS_IncrementCcMissed();
                }
            }

            clear_mask |= BQ_STAT_CC_READY;
        }

        if (stat & BQ_STAT_OV)
        {
            BMS_Fault_SetActive(FAULT_HW_OV);
            BMS_Protect_DisableCharge();
            clear_mask |= BQ_STAT_OV;
        }

        if (stat & BQ_STAT_UV)
        {
            BMS_Fault_SetActive(FAULT_HW_UV);
            BMS_Protect_DisableDischarge();
            clear_mask |= BQ_STAT_UV;
        }

        if (stat & BQ_STAT_OCD)
        {
            BMS_Fault_SetActive(FAULT_HW_OCD);
            BMS_Protect_DisableDischarge();
            clear_mask |= BQ_STAT_OCD;
        }

        if (stat & BQ_STAT_SCD)
        {
            BMS_Fault_SetActive(FAULT_HW_SCD);
            BMS_Fault_Latch(FAULT_HW_SCD);
            BMS_Protect_DisableBoth();
            clear_mask |= BQ_STAT_SCD;
        }

        if (stat & BQ_STAT_DEVICE_XREADY)
        {
            BMS_Fault_SetActive(FAULT_AFE_XREADY);
            BMS_Fault_Latch(FAULT_AFE_XREADY);
            BMS_Protect_DisableBoth();
            clear_mask |= BQ_STAT_DEVICE_XREADY;
        }

        if (clear_mask)
        {
            BQ_WriteReg(BQ_REG_SYS_STAT, clear_mask);
        }

        xSemaphoreGive(xI2CMutex);

        if (stat & (BQ_STAT_OV | BQ_STAT_UV |
                    BQ_STAT_OCD | BQ_STAT_SCD |
                    BQ_STAT_DEVICE_XREADY))
        {
            BMS_CAN_ReportFaultUrgent();
        }

        Health_Heartbeat(TASK_BIT_PROTECT);
    }
}
```

---

# 28. SampleTask

## 28.1 周期

```text
250 ms
```

使用：

```c
vTaskDelayUntil()
```

不要：

```c
vTaskDelay(250)
```

作为长期精确周期，因为执行时间会积累漂移。

## 28.2 伪代码

```c
void Task_Sample(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    uint8_t temp_div = 0;

    for (;;)
    {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(250));

        BMS_SampleLocal_t local;
        memset(&local, 0, sizeof(local));

        if (xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(20)) == pdTRUE)
        {
            bool ok = BQ_Read13CellVoltages(local.cell_mv);

            if (ok)
            {
                local.pack_sum = Sum13Cells(local.cell_mv);
                BQ_ReadPackMv(&local.pack_bq);
            }

            temp_div++;
            if (temp_div >= 8)
            {
                temp_div = 0;
                BQ_ReadTemperature(&local.temp_c_x10);
            }

            xSemaphoreGive(xI2CMutex);

            if (ok)
            {
                BMS_ProcessCellMinMax(&local);

                xSemaphoreTake(xDataMutex, portMAX_DELAY);
                BMS_Data_CommitSample(&g_bms, &local);
                xSemaphoreGive(xDataMutex);

                xEventGroupSetBits(xSysEvents, EVT_SAMPLE_READY);
            }
            else
            {
                BMS_Data_MarkVoltageInvalid();
            }
        }

        Health_Heartbeat(TASK_BIT_SAMPLE);
    }
}
```

---

# 29. SOC 设计

V1 SOC 定位：

> 工程可解释、可实现、可校准的 MCU 混合 SOC，不宣称达到独立 Fuel Gauge 的量产精度。

## 29.1 SOC 状态

```c
typedef struct
{
    int64_t remaining_uAs; /* 建议内部高分辨率 */
    uint32_t qmax_mah;

    uint16_t soc_permille;

    uint8_t initialized;

    TickType_t rest_start_tick;
    uint8_t rest_active;

    uint16_t last_ocv_soc;
} BMS_SocCtx_t;
```

## 29.2 上电初始化

等待：

```text
EVT_SAMPLE_READY
```

读取：

- 13S 电压；
- delta；
- current。

若：

```text
|I| < 100mA
```

可做 OCV 初始估算。

如果压差：

```text
delta < 50mV
```

使用：

```text
average cell voltage
```

否则保守使用：

```text
minimum cell voltage
```

查：

```text
OCV -> SOC table
```

得到：

```text
SOC0
Qremain = Qmax * SOC0
```

## 29.3 OCV 表

OCV 表必须可替换。

建议：

```c
typedef struct
{
    uint16_t mv;
    uint16_t soc_permille;
} OCV_Point_t;
```

Codex 可以内置一个“示例 NMC 表”，但必须注释：

```text
TODO: replace with measured cell chemistry OCV curve.
```

## 29.4 动态积分

每个 `xCcSampleQueue` 数据：

```text
current_ma
integration period ~= 0.25 s
```

增量：

```text
delta_mAh = I(mA) * 0.25 / 3600
```

内部建议使用：

```text
uA*s
```

避免长期浮点误差。

例如：

```c
delta_uAs = current_ma * 1000 * 250 / 1000;
```

实际应以明确单位封装函数，避免魔法数字。

定义：

```c
void SOC_IntegrateCurrent(int32_t current_ma, uint32_t dt_ms);
```

## 29.5 限幅

```text
0 <= remain <= qmax
0 <= SOC <= 1000
```

## 29.6 静置检测

满足：

```text
|I| < 100mA
持续 >= 30min
状态 STANDBY
无均衡
无 fault
cell 数据有效
```

则进行 OCV 修正。

## 29.7 OCV 修正不要瞬间跳变

如果：

```text
abs(SOC_ocv - SOC_cc) < 3%
```

可直接小步校正。

如果差异很大：

- 标记 `SOC_DRIFT_WARNING`；
- 可按 1%/min 慢速拉近；
- 不要 UI 瞬间从 70% 跳到 40%。

## 29.8 边界校正

满电：

```text
cell_max >= 4150~4200mV
charge current 已明显下降
持续条件满足
```

可把 SOC 向 100% 锚定。

低端：

```text
cell_min <= 3000mV
```

可以把 SOC 向低值锚定，但不能把软件 SOC 当作保护依据。

保护永远依赖真实电压/电流/温度。

---

# 30. SOCTask 伪代码

```c
void Task_SOC(void *arg)
{
    xEventGroupWaitBits(
        xSysEvents,
        EVT_SAMPLE_READY,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY);

    SOC_InitFromOCV();

    for (;;)
    {
        BMS_CcSample_t cc;

        if (xQueueReceive(
                xCcSampleQueue,
                &cc,
                pdMS_TO_TICKS(1000)) == pdPASS)
        {
            int32_t current_ma = BQ_CcRawToCurrentMa(cc.raw);

            SOC_IntegrateCurrent(current_ma, 250);

            xSemaphoreTake(xDataMutex, portMAX_DELAY);
            g_bms.current_ma = current_ma;
            g_bms.valid.current_valid = 1;
            g_bms.valid.last_current_tick = cc.tick;
            g_bms.soc_permille = SOC_GetPermille();
            g_bms.remain_mah = SOC_GetRemainMah();
            xSemaphoreGive(xDataMutex);
        }

        SOC_RestDetectionAndCorrection();

        Health_Heartbeat(TASK_BIT_SOC);
    }
}
```

---

# 31. 被动均衡

## 31.1 V1 策略

V1 每次最多均衡：

```text
1 个逻辑 Cell
```

这样天然避免：

- 同组相邻 cell 同时均衡；
- 复杂 thermal budget；
- 多 bit 冲突。

## 31.2 允许开启条件

全部满足：

```text
state == CHARGE
no active fault
temperature < 45°C
cell_max > 4000mV
cell_delta > 50mV
sample valid
```

## 31.3 停止条件

任一满足：

```text
state != CHARGE
fault
temperature >= 45°C
delta < 30mV
max cell < 4000mV
sample invalid
```

## 31.4 均衡周期

```text
1 s
```

每周期重新选最高 cell。

## 31.5 API

```c
bool BQ_BalanceStopAll(void);
bool BQ_BalanceLogicalCell(uint8_t logical_index);
```

`logical_index` 为 0~12，内部根据 `g_cell_cb_index[]` 映射。

---

# 32. BalanceTask

```c
void Task_Balance(void *arg)
{
    TickType_t last = xTaskGetTickCount();

    for (;;)
    {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(1000));

        BMS_Data_t d = BMS_Data_GetSnapshot();

        bool allow =
            d.valid.voltage_valid &&
            d.state == BMS_STATE_CHARGE &&
            d.fault_active == 0 &&
            d.temp_c_x10 < (BMS_BALANCE_MAX_TEMP_C * 10) &&
            d.cell_max_mv >= BMS_BALANCE_MIN_CELL_MV &&
            d.cell_delta_mv >= BMS_BALANCE_START_DELTA_MV;

        xSemaphoreTake(xI2CMutex, portMAX_DELAY);

        if (!allow)
        {
            BQ_BalanceStopAll();
        }
        else
        {
            BQ_BalanceLogicalCell(d.cell_max_index);
        }

        xSemaphoreGive(xI2CMutex);

        Health_Heartbeat(TASK_BIT_BALANCE);
    }
}
```

---

# 33. StateTask

StateTask 是系统监督者。

职责：

1. 状态机；
2. 软件保护；
3. 故障恢复条件；
4. MOS 目标状态整合；
5. 任务健康检测；
6. IWDG 喂狗；
7. 参数延迟写入调度；
8. 数据 stale 检测。

## 33.1 数据 stale

例如：

```text
voltage age > 1000ms -> invalid
current age > 1000ms -> invalid
temp age > 5000ms -> invalid
```

## 33.2 IWDG

所有任务每周期：

```c
Health_Heartbeat(TASK_BIT_xxx);
```

StateTask 每 1s 检查：

```text
all required task bits seen?
```

是：

```text
IWDG_ReloadCounter()
clear task alive bits
```

否：

```text
do not feed watchdog
```

最终 MCU reset。

这比“每个任务自己喂狗”可靠。

---

# 34. CAN 底层

## 34.1 bxCAN

配置：

```text
PCLK1 = 36MHz
Bitrate = 500kbps

Prescaler = 9
BS1 = 6 tq
BS2 = 1 tq
SJW = 1 tq

total = 1 + 6 + 1 = 8 tq
36MHz / 9 / 8 = 500kbps
sample point = 87.5%
```

SPL：

```c
CAN_InitTypeDef
CAN_FilterInitTypeDef
CAN_ITConfig()
CAN_Transmit()
CAN_Receive()
```

## 34.2 RX ISR

CAN FIFO0 中断：

```c
void USB_LP_CAN1_RX0_IRQHandler(void)
{
    BMS_CanFrame_t frame;

    CAN_Receive(CAN1, CAN_FIFO0, &rx);

    convert_to_frame();

    xQueueSendFromISR(xCanRxQueue, &frame, &hpw);

    portYIELD_FROM_ISR(hpw);
}
```

业务解析不在 ISR。

---

# 35. 29 位扩展 CAN ID

统一：

```text
bit 28..26 : Priority 3 bit
bit 25..18 : MsgType  8 bit
bit 17..10 : SrcAddr  8 bit
bit  9..2  : DstAddr  8 bit
bit  1..0  : Reserved 0
```

```c
#define MAKE_CAN_ID(prio,type,src,dst) \
    ((((uint32_t)(prio) & 0x7U) << 26) | \
     (((uint32_t)(type) & 0xFFU) << 18) | \
     (((uint32_t)(src)  & 0xFFU) << 10) | \
     (((uint32_t)(dst)  & 0xFFU) << 2))
```

地址：

```c
#define CAN_ADDR_BMS        0x01U
#define CAN_ADDR_HOST       0x02U
#define CAN_ADDR_BROADCAST  0xFFU
```

优先级：

```text
0 = fault/emergency
1 = control response
2 = realtime data
3 = parameter
6 = heartbeat
```

---

# 36. CAN MsgType

```c
/* Fault */
#define MSG_FAULT_REPORT          0x01
#define MSG_WARNING_REPORT        0x02

/* MOS control */
#define MSG_CMD_CHG_ENABLE        0x10
#define MSG_CMD_CHG_DISABLE       0x11
#define MSG_CMD_DSG_ENABLE        0x12
#define MSG_CMD_DSG_DISABLE       0x13
#define MSG_CMD_FAULT_RESET       0x14

/* Data */
#define MSG_DATA_PACK             0x20
#define MSG_DATA_CELL_GROUP       0x21
#define MSG_DATA_TEMP             0x22
#define MSG_DATA_SOC              0x23

/* Parameter */
#define MSG_PARAM_READ            0x40
#define MSG_PARAM_WRITE           0x41
#define MSG_PARAM_RESPONSE        0x42

/* System */
#define MSG_HEARTBEAT             0x60
#define MSG_ACK                   0x61
```

---

# 37. CAN 数据格式

## 37.1 PACK

`MSG_DATA_PACK`

```text
Byte0..1  pack voltage, 0.1V, big endian
Byte2..3  current, 0.1A signed, big endian
Byte4..5  SOC, 0.1%
Byte6     state
Byte7     active fault low byte
```

## 37.2 13 节 Cell

使用 group index。

```text
Byte0     group index
Byte1     valid count
Byte2..7  3 cells x uint16 mV
```

13 节需要 5 帧：

```text
group0: cell1~3
group1: cell4~6
group2: cell7~9
group3: cell10~12
group4: cell13
```

这种方式简单、扩展性高。

## 37.3 Temperature

```text
Byte0..1 temp x0.1°C signed
Byte2..3 cell delta mV
Byte4    min cell index
Byte5    max cell index
Byte6..7 reserved
```

## 37.4 Heartbeat

```text
Byte0 protocol version
Byte1 firmware major
Byte2 firmware minor
Byte3 state
Byte4 active fault summary
Byte5 latched fault summary
Byte6 alive counter
Byte7 reset reason
```

---

# 38. 控制命令安全

经典 CAN 自带帧 CRC，但应用层控制仍增加简单认证和 CRC8。

控制 payload：

```text
Byte0 = 0x55
Byte1 = command
Byte2 = sequence
Byte3 = parameter
Byte4 = optional
Byte5 = optional
Byte6 = optional
Byte7 = application CRC8(Byte0..6)
```

收到命令必须检查：

1. Extended frame；
2. DstAddr == BMS 或 broadcast；
3. MsgType 合法；
4. Byte0 == 0x55；
5. CRC8；
6. sequence 防重复；
7. 当前系统状态允许。

例如：

```text
收到 DSG_ENABLE
```

还要检查：

- 无 UV；
- 无 SCD；
- 无 AFE_COMM；
- 无 latched discharge fault；
- cell 有效。

否则 ACK 返回：

```text
DENIED_BY_SAFETY
```

---

# 39. CANTxTask

CANTxTask 是唯一调用：

```c
CAN_Transmit()
```

的任务。

职责：

- 消费 `xCanTxQueue`；
- 100ms PACK；
- 500ms Cell；
- 500ms SOC/Temp；
- 200ms heartbeat；
- fault frame 可以由其他任务 `xQueueSendToFront`。

伪代码：

```c
for (;;)
{
    now = xTaskGetTickCount();

    if (time_due(pack))
        enqueue pack;

    if (time_due(cell))
        enqueue cell groups;

    if (time_due(heartbeat))
        enqueue heartbeat;

    if (xQueueReceive(txq, &frame, 10ms))
        BSP_CAN_SendExt(&frame);

    Health_Heartbeat(TASK_BIT_CANTX);
}
```

---

# 40. CANRxTask

```c
for (;;)
{
    xQueueReceive(xCanRxQueue, &frame, portMAX_DELAY);

    if (!BMS_CAN_ParseId(...))
        continue;

    if (!address_match)
        continue;

    switch (msg_type)
    {
        case MOS:
            BMS_Command_HandleMos();
            break;

        case PARAM:
            BMS_Command_HandleParam();
            break;

        case RESET:
            BMS_Command_HandleReset();
            break;
    }

    Health_Heartbeat(TASK_BIT_CANRX);
}
```

---

# 41. 参数系统

## 41.1 参数结构

```c
typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t sequence;

    uint16_t hw_ov_mv;
    uint16_t hw_uv_mv;

    uint16_t sw_ov_warn_mv;
    uint16_t sw_ov_cutoff_mv;
    uint16_t sw_uv_warn_mv;
    uint16_t sw_uv_cutoff_mv;

    int32_t sw_oc_ma;

    int16_t chg_ot_x10;
    int16_t dsg_ot_x10;
    int16_t chg_ut_x10;
    int16_t dsg_ut_x10;

    uint16_t balance_start_mv;
    uint16_t balance_stop_mv;
    uint16_t balance_min_cell_mv;

    uint32_t rsense_uohm;
    uint32_t qmax_mah;

    uint8_t current_polarity;

    uint32_t crc32;
} BMS_ParamRecord_t;
```

## 41.2 Flash 地址

STM32F103C8 64KB：

```text
FLASH start = 0x08000000
FLASH end   = 0x08010000
page size   = 1KB
```

保留最后 2 page：

```c
#define PARAM_PAGE_A 0x0800F800UL
#define PARAM_PAGE_B 0x0800FC00UL
```

链接脚本必须保证代码不占用：

```text
0x0800F800 ~ 0x0800FFFF
```

应用可用代码 Flash 限制到：

```text
62 KB
```

## 41.3 A/B 冗余

读取：

```text
validate A
validate B
choose greater sequence
```

写：

```text
write inactive page
verify
only then treat as latest
```

掉电不会同时损坏两份。

## 41.4 参数修改流程

```text
CAN param write
  |
validate range
  |
update RAM config
  |
apply to BQ if hardware protection param
  |
set EVT_PARAM_DIRTY
  |
StateTask 延迟 2s 后写 Flash
```

避免 CAN 每次写入都立刻擦 Flash。

---

# 42. SOC 持久化

SOC 不要每 250ms 写 Flash。

条件：

```text
SOC change >= 1%
OR
60s elapsed
```

写入简化运行状态记录。

但要限制写频率。

更成熟的做法：

- SOC record 单独 wear-level；
- V1 可以每 5min 写一次；
- 上电仍用 OCV 校正，不把 Flash SOC 当绝对真值。

---

# 43. AFE 通信异常策略

每次 BQ 操作：

```text
OK -> consecutive fail = 0
FAIL -> consecutive fail++
```

第一次：

```text
bus recovery
```

第二次：

```text
retry transaction
```

连续 3 次：

```text
FAULT_AFE_COMM
CHG OFF
DSG OFF
```

然后周期性每 1s 尝试：

```text
wake/probe/reinit
```

如果恢复：

```text
重新读 13S
重新校验
重新配置硬件保护
恢复 fault_active
但 latched 是否清除由策略决定
```

---

# 44. 数据有效性原则

任何业务逻辑使用数据前必须检查：

```text
valid flag
age
range
```

不能：

```c
if (cell[0] > 4200)
```

而不知道 cell 数据是 10 秒前的。

建议 API：

```c
bool BMS_Data_IsVoltageFresh(const BMS_Data_t *d);
bool BMS_Data_IsCurrentFresh(const BMS_Data_t *d);
bool BMS_Data_IsTempFresh(const BMS_Data_t *d);
```

---

# 45. Fail-Safe 规则

以下情况：

- AFE 连续通信故障；
- XREADY；
- 13S 数据明显无效；
- 参数 CRC 错且无默认安全参数；
- 内部严重状态不一致；

统一：

```text
CHG OFF
DSG OFF
FAULT
CAN report
```

注意：

如果 I²C 已彻底失效，MCU 可能无法通过 SYS_CTRL2 再关 FET。

因此真实硬件的安全底线仍然依赖：

- BQ 硬件保护；
- 外部功率级设计；
- 硬件默认关断策略。

软件文档不能把“MCU 一定可以在 AFE 通信失效后关掉 BQ CHG/DSG”描述成物理保证。

---

# 46. Reset Reason

启动时读取 RCC reset flag：

```c
RCC_GetFlagStatus(...)
```

记录：

```text
POR
PIN
IWDG
WWDG
SOFTWARE
LOWPOWER
```

然后：

```c
RCC_ClearFlag();
```

用于 heartbeat 和诊断。

---

# 47. UART 调试

USART1：

```text
115200 8N1
```

只用于：

- 初始化日志；
- 命令简化；
- 调试。

正式关键任务中不要大量 printf。

推荐：

```c
LOG_E(...)
LOG_W(...)
LOG_I(...)
```

编译级别：

```c
#define LOG_LEVEL
```

Release 可以关闭 Info。

---

# 48. main.c

`main.c` 应极简。

```c
int main(void)
{
    SystemCoreClockUpdate();

    BSP_NVIC_Init();
    BSP_GPIO_Init();
    BSP_TIM3_UsTickInit();
    BSP_UART1_Init(115200);
    SoftI2C_Init();
    BSP_CAN1_Init500K();
    BSP_ALERT_EXTI_Init();

    Param_LoadOrDefault();

    BMS_Data_Init();

    if (!BQ76940_SystemInit())
    {
        BMS_SetStartupAfeFault();
    }

    RTOS_CreateObjects();
    RTOS_CreateTasks();

    BSP_IWDG_Init();

    vTaskStartScheduler();

    while (1)
    {
    }
}
```

顺序可以根据对象依赖微调，但不能把业务 while(1) 留在 main。

---

# 49. BQ76940 驱动 API

`bq76940.h` 至少包含：

```c
typedef enum
{
    BQ_OK = 0,
    BQ_ERR_I2C,
    BQ_ERR_CRC,
    BQ_ERR_RANGE,
    BQ_ERR_STATE
} BQ_Status_t;

BQ_Status_t BQ_Init(void);
BQ_Status_t BQ_ReInit(void);
BQ_Status_t BQ_Probe(void);

BQ_Status_t BQ_ReadReg(uint8_t reg, uint8_t *val);
BQ_Status_t BQ_WriteReg(uint8_t reg, uint8_t val);
BQ_Status_t BQ_ReadBlock(uint8_t reg, uint8_t *buf, uint8_t len);

BQ_Status_t BQ_ReadCalibration(BQ_Calibration_t *cal);

BQ_Status_t BQ_Read13Cells(uint16_t mv[13]);
BQ_Status_t BQ_ReadPackVoltage(uint32_t *mv);
BQ_Status_t BQ_ReadCcRaw(int16_t *raw);
BQ_Status_t BQ_ReadTemperatureX10(int16_t *temp);

BQ_Status_t BQ_SetChargeFet(bool en);
BQ_Status_t BQ_SetDischargeFet(bool en);

BQ_Status_t BQ_BalanceStopAll(void);
BQ_Status_t BQ_BalanceLogicalCell(uint8_t logical);

BQ_Status_t BQ_ReadSysStat(uint8_t *stat);
BQ_Status_t BQ_ClearSysStat(uint8_t mask);

BQ_Status_t BQ_ConfigureProtection(
    const BMS_ParamRecord_t *p);
```

---

# 50. App API

## 50.1 Data

```c
void BMS_Data_Init(void);
BMS_Data_t BMS_Data_GetSnapshot(void);
void BMS_Data_CommitSample(...);
```

## 50.2 Fault

```c
void BMS_Fault_SetActive(uint32_t bits);
void BMS_Fault_ClearActive(uint32_t bits);
void BMS_Fault_Latch(uint32_t bits);
bool BMS_Fault_CanReset(uint32_t bits);
bool BMS_Fault_ResetRequested(uint32_t mask);
```

## 50.3 Protect

```c
void BMS_Protect_DisableCharge(void);
void BMS_Protect_DisableDischarge(void);
void BMS_Protect_DisableBoth(void);
void BMS_Protect_EvaluateRecovery(void);
```

## 50.4 State

```c
void BMS_State_Update(const BMS_Data_t *snapshot);
```

## 50.5 SOC

```c
void SOC_Init(void);
void SOC_InitFromOCV(void);
void SOC_IntegrateCurrent(int32_t current_ma, uint32_t dt_ms);
void SOC_UpdateRestState(const BMS_Data_t *d);
uint16_t SOC_GetPermille(void);
```

## 50.6 Balance

```c
void Balance_Evaluate(const BMS_Data_t *d);
```

---

# 51. StateTask 伪代码

```c
void Task_State(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    uint8_t watchdog_div = 0;

    for (;;)
    {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(100));

        BMS_Data_t d = BMS_Data_GetSnapshot();

        BMS_Data_CheckFreshness(&d);

        BMS_SoftwareProtection_Update(&d);

        BMS_Fault_UpdateRecovery(&d);

        BMS_State_Update(&d);

        BMS_Mos_ApplyDesiredState();

        watchdog_div++;
        if (watchdog_div >= 10)
        {
            watchdog_div = 0;

            if (Health_AllRequiredTasksAlive())
            {
                BSP_IWDG_Feed();
                Health_ClearHeartbeatWindow();
            }
        }

        Param_ProcessDeferredSave();

        Health_Heartbeat(TASK_BIT_STATE);
    }
}
```

---

# 52. 软件保护计时器不要写成 blocking delay

错误：

```c
if (voltage > limit)
{
    vTaskDelay(1000);
    if (voltage > limit)
        fault();
}
```

正确：

维护 counter：

```c
if (condition)
{
    if (counter < limit)
        counter++;
}
else
{
    counter = 0;
}

if (counter >= threshold_count)
    fault();
```

StateTask 100ms：

```text
1s = 10 counts
5s = 50 counts
```

---

# 53. MOS Desired State 合并

多个模块不能直接互相覆盖。

建议使用“许可”模型：

```c
typedef struct
{
    bool charge_allowed;
    bool discharge_allowed;
} BMS_PowerPermission_t;
```

每轮根据 fault 重新计算：

```text
charge_allowed =
    no OV
    no charge OT
    no charge UT
    no AFE comm fatal
    no latched fatal

discharge_allowed =
    no UV
    no OCD/SCD
    no discharge OT/UT
    no AFE fatal
```

最终：

```c
BQ_SetChargeFet(charge_allowed);
BQ_SetDischargeFet(discharge_allowed);
```

这样不会出现：

```text
OV 任务刚关 CHG
另一个状态任务下一行又开 CHG
```

---

# 54. CAN Fault Report

故障帧应尽量包含“快照”。

由于 8 byte 限制，第一帧：

```text
Byte0..3 active fault uint32
Byte4..7 latched fault uint32
```

随后可以发 fault detail：

```text
cell min
cell max
current
temp
```

Fault frame 使用：

```text
Priority = 0
```

并：

```c
xQueueSendToFront(xCanTxQueue, ...)
```

---

# 55. 参数范围校验

任何 CAN 参数写入不得直接 memcpy 生效。

例如：

```text
HW OV:  4000~4400mV
HW UV:  2500~3100mV
SW OV cutoff < HW OV
SW UV cutoff > HW UV
Rsense: 合理范围
qmax > 0
balance start > balance stop
```

若参数之间不一致：

```text
reject whole transaction
```

不要部分应用。

---

# 56. 参数应用原子性

流程：

```text
receive new params
validate full set
copy to staging
take I2C mutex
apply BQ hardware thresholds
readback critical registers
release I2C mutex

if success:
    swap active config
    mark flash dirty
else:
    keep old config
```

---

# 57. I²C Mutex 与故障实时性

ProtectTask 可能在 SampleTask 持锁时被 ALERT 唤醒。

这是允许的。

原因：

- 硬件 SCD/OCD/OV/UV 已经在 BQ 内处理；
- ProtectTask 是软件诊断和恢复管理；
- FreeRTOS mutex 有 priority inheritance；
- SampleTask I²C 临界区必须足够短。

因此 SampleTask 禁止：

```text
Take I2C mutex
read cell
复杂计算
printf
vTaskDelay
...
Give mutex
```

正确：

```text
Take
只读硬件
Give
再计算
```

---

# 58. 任务运行时间预算

目标：

| 任务 | 典型执行时间目标 |
|---|---:|
| Protect | < 2ms（不含锁等待） |
| Sample | < 5ms |
| State | < 1ms |
| SOC | < 1ms |
| Balance | < 2ms |
| CAN TX | < 1ms |
| CAN RX | < 1ms |

软件 I²C 100 kHz 读取 30 多字节应远低于 250ms 采样周期。

---

# 59. CAN Bus-off

`bsp_can.c` 应提供：

```c
bool BSP_CAN_IsBusOff(void);
void BSP_CAN_Recover(void);
```

Bus-off：

- 设置 warning；
- 不影响 BMS 本地保护；
- 周期尝试重启 CAN；
- 不因为 CAN 失联关闭电池基本保护；
- 若系统应用场景要求通信失联关断，可作为参数策略，V1 默认不做。

---

# 60. Flash 错误

若参数 Flash A/B 都无效：

```text
load compile-time safe defaults
set warning PARAM_DEFAULT
```

不是直接完全拒绝启动。

但若默认参数本身缺失/非法：

```text
FAULT_INTERNAL
CHG/DSG OFF
```

---

# 61. OCV 表接口

`bms_soc.c`：

```c
static const OCV_Point_t g_ocv_table[] =
{
    /* 示例，不代表特定化学体系 */
    {3000,   0},
    {3300,  50},
    {3500, 100},
    {3600, 200},
    {3700, 400},
    {3800, 550},
    {3900, 700},
    {4000, 850},
    {4100, 950},
    {4200,1000},
};
```

Codex 应实现：

```c
uint16_t SOC_OcvMvToPermille(uint16_t mv);
```

线性插值。

但 README 明确标记：

> 真实项目必须用目标电芯静置实验曲线替换该示例表。

---

# 62. 单元测试友好设计

BQ 驱动应通过一层 I²C backend：

```c
typedef struct
{
    bool (*read)(...);
    bool (*write)(...);
} I2C_BusOps_t;
```

若不希望 V1 复杂化，至少在编译期：

```c
#ifdef BMS_UNIT_TEST
```

把寄存器访问替换为 mock。

这样可测试：

- OV trip 计算；
- ADC conversion；
- cell mapping；
- CRC；
- SOC；
- state machine；
- fault recovery；
- CAN serialization。

---

# 63. 建议测试矩阵

本文档不提供虚构的“实测结果”，只定义应执行的验证。

## 63.1 CRC

输入固定字节，验证 CRC8。

## 63.2 13S 映射

构造：

```text
VC1=3101
VC2=3102
...
VC15=3115
```

验证逻辑输出跳过 VC9/VC14。

## 63.3 电压换算

给定：

```text
GAIN
OFFSET
raw
```

验证 mV。

## 63.4 CC

测试：

```text
0
positive
negative
0x7FFF
0x8000
```

## 63.5 State

输入：

```text
I=0
I=+500
I=-500
```

验证 debounce 与迟滞。

## 63.6 OV

验证：

```text
4199 -> no fault
4201 0.5s -> no cutoff
4201 1.0s -> SW OV
4100 5s -> recover
```

## 63.7 SCD

模拟 SYS_STAT SCD：

- latched；
- both FET denied；
- CAN urgent；
- reset command 未满足条件不得恢复。

## 63.8 I²C failure

连续 3 次：

- AFE comm；
- MOS permission fail-safe；
- CAN fault；
- reinit path。

## 63.9 Balance

- not charge → off；
- delta 40 → off；
- delta 60 + 4.1V → max cell on；
- delta 25 → stop。

## 63.10 CAN

- wrong dst；
- wrong 0x55；
- wrong CRC；
- duplicate sequence；
- unsafe MOS enable。

---

# 64. 工程验收条件

Codex 生成的 V1 至少满足：

### Compile

- STM32F10x SPL；
- 无 HAL；
- FreeRTOS build 通过；
- 64KB Flash target；
- 20KB RAM target。

### Static

- `main.c` 无业务循环；
- 7 tasks；
- ISR 无 I²C；
- I²C 有 timeout；
- BQ CRC；
- 13S mapping；
- ALERT active-high；
- CC_READY 正确处理。

### Behavior

- BQ init fail 有重试；
- sample 数据有 freshness；
- software/hardware fault 分开；
- CHG/DSG permission merge；
- CAN TX 单任务；
- flash A/B；
- IWDG supervisor。

---

# 65. Codex 生成顺序

不要让 Codex 一次性生成整个工程。

建议分 12 个阶段。

## Phase 1

生成：

- `bms_config.h`
- 数据结构；
- fault bit；
- state enum；
- 工程目录。

## Phase 2

生成：

- GPIO；
- TIM3 us；
- UART；
- Soft I2C；
- CRC8。

先做纯底层。

## Phase 3

生成 BQ：

- reg；
- read/write；
- gain/offset；
- conversion。

## Phase 4

生成：

- 13S mapping；
- cell read；
- pack；
- CC；
- TS。

## Phase 5

生成：

- protection configuration；
- CHG/DSG；
- balance。

## Phase 6

生成：

- FreeRTOSConfig；
- RTOS objects；
- 7 task skeleton。

## Phase 7

实现：

- ALERT ISR；
- ProtectTask；
- CC queue。

## Phase 8

实现：

- SampleTask；
- data snapshot；
- stale。

## Phase 9

实现：

- state；
- software protect；
- fault recovery；
- health/IWDG。

## Phase 10

实现：

- SOC；
- OCV；
- balance。

## Phase 11

实现：

- CAN BSP；
- protocol；
- CANTx/CANRx。

## Phase 12

实现：

- flash；
- params；
- unit tests / mocks；
- README。

---

# 66. 给 Codex 的强制提示词

将本文档交给 Codex 时附加：

```text
你现在要根据 BMS_V1_Design.md 生成一个 STM32F103C8T6 BMS 工程。

强制要求：
1. 使用 STM32F10x Standard Peripheral Library，不使用 HAL。
2. 使用原生 FreeRTOS API，不使用 CMSIS-RTOS wrapper。
3. 使用 C99。
4. 代码按文档目录拆分。
5. 不得发明 BQ34Z100/BQ76200。
6. BQ76940 为 BQ7694003 CRC 版本。
7. BQ I2C 使用 PB8/PB9 软件模拟。
8. ALERT active-high，PB1 EXTI1。
9. 必须处理 CC_READY 也触发 ALERT 的事实。
10. 13S 必须使用文档规定的 VC mapping。
11. 所有 I2C 访问必须有 timeout。
12. ISR 不允许进行 I2C/Flash/printf。
13. 所有 BQ 共享访问受 xI2CMutex 保护。
14. 所有 CAN 硬件发送只允许 CANTxTask。
15. 不允许用 delay_ms 阻塞 RTOS 任务；周期任务使用 vTaskDelayUntil。
16. 微秒时基使用 TIM3 1MHz。
17. 编译目标按 64KB Flash、20KB SRAM。
18. 对尚无真实电池参数的数据用 TODO 标注，不得伪造实测。
19. 每个阶段生成后先列出接口和依赖，再生成代码。
20. 对 BQ 寄存器 bit 定义不确定时，不要猜，保留 TODO 并引用 TI BQ769x0 datasheet section。
```

---

# 67. 面向真实工程的关键闭环

如果后续要解释项目，始终按以下链路。

## 67.1 启动链

```text
MCU reset
-> clock/peripheral
-> wake BQ
-> probe
-> calibration
-> protection
-> ADC/CC
-> first sample
-> enable MOS
-> RTOS normal
```

## 67.2 采集链

```text
Cell / Rsense / NTC
-> BQ ADC/CC
-> Soft I2C
-> Sample/Protect
-> g_bms
-> State/SOC/Balance/CAN
```

## 67.3 硬件保护链

```text
physical abnormal
-> BQ comparator
-> BQ removes FET drive
-> SYS_STAT
-> ALERT high
-> EXTI
-> ProtectTask
-> fault latch/recovery
-> CAN
```

## 67.4 软件保护链

```text
sample
-> threshold + debounce
-> fault_active
-> permission
-> CHG/DSG
-> recovery hysteresis
```

## 67.5 SOC 链

```text
first valid voltage
-> OCV initial
-> CC_READY
-> current
-> Coulomb integration
-> rest detect
-> OCV correction
-> SOC report
```

## 67.6 均衡链

```text
charge
-> valid cell array
-> max/min
-> delta
-> safety gate
-> physical CB mapping
-> CELLBAL
-> next cycle re-evaluate
```

## 67.7 CAN 链

```text
g_bms snapshot
-> protocol
-> tx queue
-> CANTxTask
-> bxCAN
-> host
```

反向：

```text
host
-> bxCAN IRQ
-> rx queue
-> CANRxTask
-> validate
-> command
-> safety interlock
-> actuator
```

---

# 68. V1 与未来版本边界

## V1.1 可增加

- 多 NTC；
- Sleep/SHIP；
- SOC 更完整 OCV；
- Qmax 简单学习；
- Blackbox fault log；
- CAN bus-off 更完整状态机；
- 参数 CLI；
- bootloader。

## V2 可增加

- BQ76200；
- 高边 MOS；
- PCHG；
- PACK 电压；
- BQ34Z100；
- Impedance Track；
- SOH；
- 更高精度 SOC；
- 双 CAN；
- 隔离；
- 安全 MCU；
- 量产测试模式。

V1 不应该把这些写成“已实现”。

---

# 69. 资料依据与口径修正说明

本方案的项目组织、任务化思想、采集/保护/均衡/SOC/CAN 分层设计，参考了用户提供的：

- `BMS项目方案项目书.docx`
- `项目知识点.docx`
- 相关流程图材料
- 前期 BQ76940 / STM32 项目资料

但 V1 已统一并修正以下冲突：

1. 最终为 **单 BQ76940**，不使用 BQ34Z100/BQ76200；
2. 最终为 **13S/48V**；
3. 最终 MCU 为 **STM32F103C8T6**；
4. 最终使用 **SPL 标准库**，不是 HAL；
5. 最终使用原生 FreeRTOS；
6. 最终 BQ 型号逻辑固定为 **BQ7694003**；
7. ALERT 统一为 **active-high**；
8. ALERT 不仅代表 fault，也代表 CC_READY；
9. cell voltage 周期按 250ms；
10. temperature 数据按约 2s 更新；
11. F103C8 Flash 按官方 64KB 约束；
12. SOC 是 MCU 混合算法，不宣称独立 fuel gauge 精度；
13. STANDBY 下默认允许 CHG/DSG，而不是无条件关闭两者；
14. 微秒时基改为 TIM3，不使用不可校准的 NOP 延时；
15. 功率外围仅作为已存在接口，不虚构 PCB 级实现。

---

# 70. 官方技术参考

实现代码时，寄存器与时序优先依据以下官方资料：

1. **Texas Instruments — BQ769x0 3-Series to 15-Series Cell Battery Monitor Family for Li-Ion and Phosphate Applications, Rev. I, SLUSBK2I**
2. **Texas Instruments — bq769x0 Family Top Design Considerations**
3. **Texas Instruments — bq76930 and bq76940 Evaluation Module User's Guide**
4. **STMicroelectronics — RM0008 STM32F10x Reference Manual**
5. **STMicroelectronics — DS5319 STM32F103x8/xB Datasheet**
6. **STMicroelectronics — STSW-STM32054 STM32F10x Standard Peripheral Library**
7. **FreeRTOS Kernel Documentation — Tasks / Queues / Mutexes / Binary Semaphores / Event Groups**

当本方案与芯片官方寄存器定义发生冲突时：

> **以 TI / ST 官方资料为最终依据，并在代码 TODO 中标出差异，而不是猜测。**

---

# 71. 最终项目一句话定义

> **BMS V1 是一套基于 STM32F103C8T6 + BQ7694003 的 13S/48V 智能电池管理软件：BQ76940 承担电芯、电流、温度采集以及 OV/UV/OCD/SCD 的硬件安全底线；STM32 通过 FreeRTOS 七任务完成数据采集、故障管理、状态机、SOC、均衡及 CAN 通信，并通过 I²C Mutex、ALERT 事件、数据 freshness、MOS permission、Flash A/B 和 IWDG 构成完整的软件工程闭环。**

---

# 附录 A：建议的关键头文件骨架

```c
/* bms_config.h */
#ifndef __BMS_CONFIG_H
#define __BMS_CONFIG_H

#include <stdint.h>

#define BMS_CELL_COUNT 13U
#define BMS_RSENSE_UOHM 4000UL

/* ... */

#endif
```

```c
/* bq76940.h */
#ifndef __BQ76940_H
#define __BQ76940_H

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
    uint16_t gain_uv;
    int8_t offset_mv;
} BQ_Calibration_t;

/* APIs */

#endif
```

```c
/* bms_data.h */
#ifndef __BMS_DATA_H
#define __BMS_DATA_H

#include "FreeRTOS.h"
#include "semphr.h"

/* core data model */

#endif
```

---

# 附录 B：实现完成后的 README 应回答的问题

Codex 最终生成的 README 至少解释：

1. 为什么选择 BQ76940？
2. 为什么是 13S？
3. 为什么用软件 I²C？
4. CRC 怎么处理？
5. 为什么 ALERT 不能简单理解为 fault？
6. 为什么 ProtectTask 会每 250ms 左右被 CC_READY 唤醒？
7. 13S 的 VC mapping 为什么跳过 VC9/VC14？
8. 为什么硬件保护和软件保护同时存在？
9. 为什么软件阈值更保守？
10. 为什么 SampleTask 250ms？
11. 为什么温度 2s？
12. 为什么用 Mutex？
13. 为什么故障任务等待 Mutex 不会破坏安全底线？
14. 为什么 CAN TX 必须单出口？
15. SOC 为什么需要 OCV 修正？
16. SOC 为什么不能用于硬保护？
17. 为什么均衡只允许充电时？
18. 为什么 V1 每次只均衡一节？
19. 为什么 Flash 使用 A/B？
20. 为什么 IWDG 不能让每个任务随便喂？
21. 如果 I²C 挂死怎么办？
22. 如果 CAN bus-off 怎么办？
23. 如果 BQ 初始化失败怎么办？
24. SCD 为什么锁定？
25. V2 为什么会考虑 BQ76200/BQ34Z100？

如果这些问题全部能从代码和本文档找到清晰答案，则 V1 的逻辑闭环成立。
