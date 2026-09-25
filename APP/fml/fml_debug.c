#include "fml_debug.h"

#include <stdbool.h>
#include <stddef.h>

#include "fml_balance.h"
#include "fml_can.h"
#include "fml_data.h"
#include "fml_fet_manager.h"
#include "fml_health.h"
#include "fml_persistence.h"
#include "fml_policy.h"
#include "fml_protect.h"
#include "fml_recovery.h"
#include "fml_soc.h"
#include "fml_state.h"

#define BMS_DEBUG_PERIOD_MS                      (1000UL)
#define BMS_DEBUG_LINE_CAPACITY                  (512U)

/* 待 UART 输出的一整行 BMS1 只读诊断文本。 */
static char s_line[BMS_DEBUG_LINE_CAPACITY];
/* 当前已格式化的诊断文本长度，单位字节。 */
static uint16_t s_line_length;
/* 下次非阻塞发送开始的字节偏移。 */
static uint16_t s_line_offset;
/* 上次准备周期诊断行的毫秒时刻。 */
static uint32_t s_last_output_ms;
/* 尚未发送过首行诊断文本的标志。 */
static bool s_first_output;

/*
 * 大型诊断投影使用 static storage，避免挤占已审查的 CANTx task stack。
 * APL CAN Tx task 是 sole caller/writer，因此无需额外 mutex，也不会产生并发 torn data。
 */
/* 静态存放的测量诊断副本，避免占用 CANTx 任务栈。 */
static BMS_DataSnapshot_t s_measurement;
/* 静态存放的 State 安全快照副本，只供格式化读取。 */
static BMS_StateSafetySnapshot_t s_state;
/* 静态存放的 Protect 安全快照副本，只供格式化读取。 */
static BMS_ProtectSafetySnapshot_t s_protect;
/* 静态存放的恢复状态副本，只供格式化读取。 */
static BMS_RecoverySnapshot_t s_recovery;
/* 静态存放的 FET 事务副本，只供格式化读取。 */
static BMS_FetManagerSnapshot_t s_fet;
/* 静态存放的七任务健康副本，只供格式化读取。 */
static BMS_HealthSnapshot_t s_health;
/* 静态存放的 SOC 估计副本，只供格式化读取。 */
static BMS_SocSnapshot_t s_soc;
/* 静态存放的均衡事务副本，只供格式化读取。 */
static BMS_BalanceSnapshot_t s_balance;
/* 静态存放的 CAN 诊断副本，只供格式化读取。 */
static BMS_CanDiagnostics_t s_can;
/* 静态存放的持久化诊断副本，只供格式化读取。 */
static BMS_PersistenceDiagnostics_t s_flash;

/* 在诊断行剩余容量内附加一个字符，避免越界写。 */
static void FML_Debug_AppendChar(char value)
{
    if (s_line_length < (BMS_DEBUG_LINE_CAPACITY - 1U))
    {
        s_line[s_line_length] = value;
        ++s_line_length;
    }
}

/* 逐字节把非空文本附加到有界诊断行。 */
static void FML_Debug_AppendText(const char *text)
{
    if (text == NULL)
    {
        return;
    }
    while (*text != '\0')
    {
        FML_Debug_AppendChar(*text);
        ++text;
    }
}

/* 把无符号 32 位整数格式化为十进制诊断文本。 */
static void FML_Debug_AppendU32(uint32_t value)
{
    /* 当前格式化数值的十进制位数。 */
    char digits[10];
    /* 当前已处理或已生成的元素数量。 */
    uint8_t count;

    count = 0U;
    do
    {
        digits[count] = (char)('0' + (char)(value % 10UL));
        value /= 10UL;
        ++count;
    } while ((value != 0UL) && (count < (uint8_t)sizeof(digits)));

    while (count > 0U)
    {
        --count;
        FML_Debug_AppendChar(digits[count]);
    }
}

/* 把有符号 32 位整数格式化为十进制诊断文本。 */
static void FML_Debug_AppendI32(int32_t value)
{
    /* 待格式化有符号数的绝对值。 */
    uint32_t magnitude;

    if (value < 0)
    {
        FML_Debug_AppendChar('-');
        magnitude = (uint32_t)(0UL - (uint32_t)value);
    }
    else
    {
        magnitude = (uint32_t)value;
    }
    FML_Debug_AppendU32(magnitude);
}

/* 以固定宽度十六进制追加 32 位诊断值。 */
static void FML_Debug_AppendHex32(uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    /* 十进制格式化时当前位数的偏移。 */
    int8_t shift;

    shift = 28;
    while (shift >= 0)
    {
        FML_Debug_AppendChar(hex[(value >> (uint8_t)shift) & 0x0FUL]);
        shift = (int8_t)(shift - 4);
    }
}

/* 以固定宽度十六进制追加 16 位诊断值。 */
static void FML_Debug_AppendHex16(uint16_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    /* 十进制格式化时当前位数的偏移。 */
    int8_t shift;

    shift = 12;
    while (shift >= 0)
    {
        FML_Debug_AppendChar(hex[(value >> (uint8_t)shift) & 0x0FU]);
        shift = (int8_t)(shift - 4);
    }
}

/* 把请求的 CHG/DSG 方向编码为诊断位，不读取硬件。 */
static uint8_t FML_Debug_FetBits(const BQ76940_FetRequest_t *request)
{
    /* 本轮处理的位集合。 */
    uint8_t bits;

    bits = 0U;
    if ((request != NULL) &&
        (request->chg == BQ76940_FET_DESIRE_ENABLE))
    {
        bits |= 0x01U;
    }
    if ((request != NULL) &&
        (request->dsg == BQ76940_FET_DESIRE_ENABLE))
    {
        bits |= 0x02U;
    }
    return bits;
}

/* 把 SYS_CTRL2 回读的 CHG/DSG 状态编码为诊断位。 */
static uint8_t FML_Debug_ObservedFetBits(
    const BQ76940_FetObserved_t *observed)
{
    /* 本轮处理的位集合。 */
    uint8_t bits;

    bits = 0U;
    if ((observed != NULL) && observed->chg_on)
    {
        bits |= 0x01U;
    }
    if ((observed != NULL) && observed->dsg_on)
    {
        bits |= 0x02U;
    }
    return bits;
}

/* 把测量有效性字段编码为紧凑诊断位。 */
static uint8_t FML_Debug_ValidityBits(const BMS_Policy_t *policy)
{
    /* 本轮处理的位集合。 */
    uint8_t bits;

    bits = 0U;
    if (s_measurement.cell_metadata.valid_bitmap == BMS_CELL_DEFINED_MASK)
    {
        bits |= 0x01U;
    }
    if (s_measurement.current_metadata.valid)
    {
        bits |= 0x02U;
    }
    if (s_measurement.temperature_metadata.valid)
    {
        bits |= 0x04U;
    }
    if ((policy != NULL) &&
        (s_measurement.cell_metadata.valid_bitmap == BMS_CELL_DEFINED_MASK) &&
        (s_measurement.cell_metadata.stale_bitmap == 0U))
    {
        bits |= 0x10U;
    }
    if ((policy != NULL) &&
        FML_Data_IsFresh(s_measurement.current_metadata.valid,
                         s_measurement.current_metadata.stale_latched,
                         s_measurement.current_metadata.age_ms,
                         policy->freshness.current_fresh_ms))
    {
        bits |= 0x20U;
    }
    if ((policy != NULL) &&
        FML_Data_IsFresh(s_measurement.temperature_metadata.valid,
                         s_measurement.temperature_metadata.stale_latched,
                         s_measurement.temperature_metadata.age_ms,
                         policy->freshness.temperature_fresh_ms))
    {
        bits |= 0x40U;
    }
    return bits;
}

/* 从完整快照中提取最低与最高电芯供诊断输出。 */
static void FML_Debug_CellRange(uint16_t *minimum_mv,
                                uint16_t *maximum_mv)
{
    /* 当前诊断字段或输出字节的索引。 */
    uint8_t index;
    /* 当前比较得到的最小值。 */
    uint16_t minimum;
    /* 当前比较得到的最大值。 */
    uint16_t maximum;

    minimum = UINT16_MAX;
    maximum = 0U;
    for (index = 0U; index < BMS_CELL_COUNT; ++index)
    {
        if (s_measurement.cell_voltage_mv[index] < minimum)
        {
            minimum = s_measurement.cell_voltage_mv[index];
        }
        if (s_measurement.cell_voltage_mv[index] > maximum)
        {
            maximum = s_measurement.cell_voltage_mv[index];
        }
    }
    *minimum_mv = minimum;
    *maximum_mv = maximum;
}

/* 读取当前诊断发送位置的字节而不推进偏移。 */
bool FML_Debug_PeekByte(uint8_t *value)
{
    if ((value == NULL) || (s_line_offset >= s_line_length))
    {
        return false;
    }
    *value = (uint8_t)s_line[s_line_offset];
    return true;
}

/* 确认一个字节已由 UART 接收并推进发送偏移。 */
void FML_Debug_ConsumeByte(void)
{
    if (s_line_offset < s_line_length)
    {
        ++s_line_offset;
    }
}

/* 清空串口诊断输出缓冲与发送位置。 */
void FML_Debug_Init(void)
{
    s_line_length = 0U;
    s_line_offset = 0U;
    s_last_output_ms = 0UL;
    s_first_output = true;
}

/* 按周期生成只读 BMS1 诊断行，不阻塞等待 UART 发送。 */
bool FML_Debug_PrepareSnapshot(uint32_t now_ms,
                               uint32_t free_heap_bytes,
                               uint32_t minimum_heap_bytes)
{
    /* 本轮处理使用的只读 BMS 策略。 */
    const BMS_Policy_t *policy;
    /* 当前仍处于激活状态的故障位图。 */
    BMS_FaultBitmap_t active_faults;
    /* 输出诊断报文的锁存故障位图。 */
    BMS_FaultBitmap_t latched_faults;
    /* 当前有效电芯的最小电压，单位 mV。 */
    uint16_t minimum_cell_mv;
    /* 当前有效电芯的最大电压，单位 mV。 */
    uint16_t maximum_cell_mv;
    /* 当前累计的 CAN 丢帧数量。 */
    uint32_t can_drops;
    /* Flash 写入或校验失败的累计次数。 */
    uint32_t flash_io_failures;
    /* 当前诊断字段或输出字节的索引。 */
    uint8_t index;
    /* 当前诊断字段的有效性标记。 */
    uint8_t validity;

    /*
     * 状态机只有两种工作：若上一行未发完，先从 offset 继续最多 8 B；若已发完
     * 且 1 s 周期到达，才捕获一组新快照并格式化下一行。这样不会在旧行中途
     * 替换 buffer，也不会因为 UART busy 反复重建昂贵诊断投影。
     */
    if (s_line_offset < s_line_length)
    {
        return false;
    }
    if (!s_first_output &&
        ((uint32_t)(now_ms - s_last_output_ms) < BMS_DEBUG_PERIOD_MS))
    {
        return false;
    }
    s_first_output = false;
    s_last_output_ms = now_ms;
    policy = FML_Policy_Get();

    if (!FML_Data_GetSnapshot(&s_measurement, now_ms))
    {
        s_line_length = 0U;
        s_line_offset = 0U;
        FML_Debug_AppendText("BMS1 t=");
        FML_Debug_AppendU32(now_ms);
        FML_Debug_AppendText(" data=UNAVAILABLE\r\n");
        return true;
    }

    s_state = FML_State_GetSafetySnapshot();
    s_protect = FML_Protect_GetSafetySnapshot();
    s_recovery = FML_Recovery_GetSnapshot();
    s_fet = FML_FetManager_GetSnapshot();
    s_health = FML_Health_GetSnapshot();
    s_soc = FML_Soc_GetSnapshot();
    s_balance = FML_Balance_GetSnapshot();
    s_can = FML_Can_GetDiagnostics();
    s_flash = FML_Persistence_TargetGetDiagnostics();

    FML_Debug_CellRange(&minimum_cell_mv, &maximum_cell_mv);
    validity = FML_Debug_ValidityBits(policy);
    active_faults = s_state.faults.active | s_protect.faults.active;
    latched_faults = s_state.faults.latched | s_protect.faults.latched;
    can_drops = s_can.tx_drop_count + s_can.target_tx_drop_count +
        s_can.target_rx_queue_drop_count;
    flash_io_failures = s_flash.load_io_failure_count +
        s_flash.save_io_failure_count;

    s_line_length = 0U;
    s_line_offset = 0U;
    FML_Debug_AppendText("BMS1 t=");
    FML_Debug_AppendU32(now_ms);
    FML_Debug_AppendText(" st=");
    FML_Debug_AppendU32((uint32_t)s_state.state);
    FML_Debug_AppendText(" cell=");
    FML_Debug_AppendU32(minimum_cell_mv);
    FML_Debug_AppendChar('/');
    FML_Debug_AppendU32(maximum_cell_mv);
    FML_Debug_AppendText(" pack=");
    FML_Debug_AppendU32(s_measurement.pack_voltage_mv);
    FML_Debug_AppendText(" cur=");
    FML_Debug_AppendI32(s_measurement.current_ma);
    FML_Debug_AppendText(" temp=");
    FML_Debug_AppendI32(s_measurement.temperature_decic);
    FML_Debug_AppendText(" vf=");
    FML_Debug_AppendU32(validity);
    FML_Debug_AppendText(" seq/gen=");
    FML_Debug_AppendU32(s_measurement.sample_sequence);
    FML_Debug_AppendChar('/');
    FML_Debug_AppendU32(s_measurement.afe_generation);
    FML_Debug_AppendText(" fault=");
    FML_Debug_AppendHex32(active_faults);
    FML_Debug_AppendChar('/');
    FML_Debug_AppendHex32(latched_faults);
    FML_Debug_AppendText(" inh=");
    FML_Debug_AppendHex32(s_fet.inhibit_chg_reasons);
    FML_Debug_AppendChar('/');
    FML_Debug_AppendHex32(s_fet.inhibit_dsg_reasons);
    FML_Debug_AppendText(" fet=");
    FML_Debug_AppendU32(FML_Debug_FetBits(&s_fet.requested));
    FML_Debug_AppendChar('/');
    FML_Debug_AppendU32(FML_Debug_FetBits(&s_fet.effective));
    FML_Debug_AppendChar('/');
    FML_Debug_AppendU32(FML_Debug_ObservedFetBits(&s_fet.observed));
    FML_Debug_AppendChar('/');
    FML_Debug_AppendU32(s_fet.register_state_confirmed ? 1UL : 0UL);
    FML_Debug_AppendChar('/');
    FML_Debug_AppendU32((uint32_t)s_fet.transaction_state);
    FML_Debug_AppendText(" rec=");
    FML_Debug_AppendU32((uint32_t)s_recovery.phase);
    FML_Debug_AppendChar('/');
    FML_Debug_AppendU32(s_recovery.technical_ready ? 1UL : 0UL);
    FML_Debug_AppendText(" hb=");
    for (index = 0U; index < (uint8_t)BMS_HEALTH_TASK_COUNT; ++index)
    {
        if (index != 0U)
        {
            FML_Debug_AppendChar('.');
        }
        FML_Debug_AppendHex32(s_health.generation[index]);
    }
    FML_Debug_AppendText(" soc=");
    FML_Debug_AppendU32(s_soc.soc_permille);
    FML_Debug_AppendChar('/');
    FML_Debug_AppendU32(s_soc.valid ? 1UL : 0UL);
    FML_Debug_AppendText(" bal=");
    FML_Debug_AppendHex16(s_balance.requested_bitmap);
    FML_Debug_AppendChar('/');
    FML_Debug_AppendHex16(s_balance.confirmed_bitmap);
    FML_Debug_AppendChar('/');
    FML_Debug_AppendU32(s_balance.register_state_confirmed ? 1UL : 0UL);
    FML_Debug_AppendText(" can=");
    FML_Debug_AppendU32(s_can.target_tx_count);
    FML_Debug_AppendChar('/');
    FML_Debug_AppendU32(can_drops);
    FML_Debug_AppendChar('/');
    FML_Debug_AppendU32(s_can.rx_valid_count);
    FML_Debug_AppendChar('/');
    FML_Debug_AppendU32(s_can.target_init_failure_count);
    FML_Debug_AppendChar('/');
    FML_Debug_AppendU32(s_can.target_bus_off_recovery_count);
    FML_Debug_AppendText(" flash=");
    FML_Debug_AppendU32(s_flash.save_success_count);
    FML_Debug_AppendChar('/');
    FML_Debug_AppendU32(flash_io_failures);
    FML_Debug_AppendChar('/');
    FML_Debug_AppendU32(s_flash.save_verify_failure_count);
    FML_Debug_AppendChar('/');
    FML_Debug_AppendU32(s_flash.both_invalid_count);
    FML_Debug_AppendText(" heap=");
    FML_Debug_AppendU32(free_heap_bytes);
    FML_Debug_AppendChar('/');
    FML_Debug_AppendU32(minimum_heap_bytes);
    FML_Debug_AppendText("\r\n");

    return true;
}
