#include "test_phase4.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bq76940.h"
#include "bq76940_measurement.h"
#include "bq76940_regs.h"
#include "crc8_bq76940.h"
#include "soft_i2c.h"

/*
 * trace-recording SoftI2C mock：正式 bq76940 transport/measurement 与该 mock
 * 链接，记录每个 bus operation，并按 wire 顺序提供 caller 指定 data/CRC byte。
 */

#define TRACE_CAPACITY              (192U)
#define READ_CAPACITY               (64U)
#define NO_FAILURE                  (-1)

#define EVT(type_, value_)          \
    ((uint16_t)(((uint16_t)(type_) << 8) | (uint8_t)(value_)))

enum
{
    TRACE_START = 1,
    TRACE_RESTART,
    TRACE_STOP,
    TRACE_WRITE_ADDRESS,
    TRACE_WRITE_BYTE,
    TRACE_READ_BYTE,
    TRACE_READ_RESPONSE
};

typedef struct
{
    uint16_t trace[TRACE_CAPACITY];
    uint8_t trace_count;
    uint8_t reads[READ_CAPACITY];
    uint8_t read_count;
    uint8_t read_index;
    int16_t fail_trace_index;
    SoftI2C_Status_t fail_status;
} TransportMock_t;

static TransportMock_t s_mock;

#define TEST_CHECK(expr_)          \
    do                             \
    {                              \
        if (!(expr_))              \
        {                          \
            ++failures;            \
        }                          \
    } while (0)

static void Mock_Reset(void)
{
    uint8_t index;

    s_mock.trace_count = 0U;
    s_mock.read_count = 0U;
    s_mock.read_index = 0U;
    s_mock.fail_trace_index = NO_FAILURE;
    s_mock.fail_status = SOFT_I2C_STATUS_OK;
    for (index = 0U; index < TRACE_CAPACITY; ++index)
    {
        s_mock.trace[index] = 0U;
    }
    for (index = 0U; index < READ_CAPACITY; ++index)
    {
        s_mock.reads[index] = 0U;
    }
}

static void Mock_SetReads(const uint8_t *values, uint8_t count)
{
    uint8_t index;

    s_mock.read_count = count;
    s_mock.read_index = 0U;
    for (index = 0U; index < count; ++index)
    {
        s_mock.reads[index] = values[index];
    }
}

static void Mock_FailAt(int16_t trace_index, SoftI2C_Status_t status)
{
    s_mock.fail_trace_index = trace_index;
    s_mock.fail_status = status;
}

static SoftI2C_Status_t Mock_Record(uint8_t type, uint8_t value)
{
    int16_t current;

    current = (int16_t)s_mock.trace_count;
    if (s_mock.trace_count < TRACE_CAPACITY)
    {
        s_mock.trace[s_mock.trace_count] = EVT(type, value);
        ++s_mock.trace_count;
    }
    if (current == s_mock.fail_trace_index)
    {
        return s_mock.fail_status;
    }
    return SOFT_I2C_STATUS_OK;
}

static uint32_t Mock_CountEvent(uint16_t type)
{
    uint8_t index;
    uint32_t count;

    count = 0UL;
    for (index = 0U; index < s_mock.trace_count; ++index)
    {
        if ((s_mock.trace[index] >> 8) == type)
        {
            ++count;
        }
    }
    return count;
}

/* 正式 transport 跨 translation unit 引用这些 mock symbol，禁止编译器 localize。 */
__attribute__((used))
bool SoftI2C_IsInitialized(const SoftI2C_t *bus)
{
    return (bus != NULL) && bus->initialized;
}

SoftI2C_Status_t SoftI2C_Start(SoftI2C_t *bus)
{
    (void)bus;
    return Mock_Record(TRACE_START, 0U);
}

SoftI2C_Status_t SoftI2C_RepeatedStart(SoftI2C_t *bus)
{
    (void)bus;
    return Mock_Record(TRACE_RESTART, 0U);
}

SoftI2C_Status_t SoftI2C_Stop(SoftI2C_t *bus)
{
    (void)bus;
    return Mock_Record(TRACE_STOP, 0U);
}

SoftI2C_Status_t SoftI2C_WriteAddress(SoftI2C_t *bus,
                                      uint8_t address_byte)
{
    (void)bus;
    return Mock_Record(TRACE_WRITE_ADDRESS, address_byte);
}

SoftI2C_Status_t SoftI2C_WriteByte(SoftI2C_t *bus, uint8_t value)
{
    (void)bus;
    return Mock_Record(TRACE_WRITE_BYTE, value);
}

SoftI2C_Status_t SoftI2C_ReadByteBegin(SoftI2C_t *bus, uint8_t *value)
{
    uint8_t next_value;
    SoftI2C_Status_t status;

    (void)bus;
    next_value = (s_mock.read_index < s_mock.read_count) ?
                 s_mock.reads[s_mock.read_index] : 0U;
    status = Mock_Record(TRACE_READ_BYTE, next_value);
    if (status == SOFT_I2C_STATUS_OK)
    {
        if (s_mock.read_index < s_mock.read_count)
        {
            ++s_mock.read_index;
        }
        *value = next_value;
    }
    return status;
}

SoftI2C_Status_t SoftI2C_SendReadResponse(
    SoftI2C_t *bus,
    SoftI2C_MasterResponse_t response)
{
    (void)bus;
    return Mock_Record(TRACE_READ_RESPONSE, (uint8_t)response);
}

static void Mock_ReadyDevice(BQ76940_t *device, SoftI2C_t *bus)
{
    Mock_Reset();
    bus->initialized = true;
    (void)SoftI2C_IsInitialized(bus);
    (void)BQ76940_Init(device, bus);
}

/* 下列 golden constant 来自独立 Python oracle，不由被测 C 自己生成。 */

/* Calibration：GAIN=380 uV/LSB（trim 0x0F），OFFSET=+30 mV。 */
static const BQ76940_Calibration_t GOLD_CAL = { 380U, 30, true };

/* 30-byte VC window 的 raw14 依次为 0x1800、0x1900…；wire read 顺序是
 * data0/crc0/data1/crc1…，首 CRC 覆盖 read address+data，后续各覆盖单 data。 */
static const uint8_t GOLD_VC_WINDOW_READS[60] =
{
    0x18, 0x0A, 0x00, 0x00, 0x19, 0x4F, 0x00, 0x00,
    0x1A, 0x46, 0x00, 0x00, 0x1B, 0x41, 0x00, 0x00,
    0x1C, 0x54, 0x00, 0x00, 0x1D, 0x53, 0x00, 0x00,
    0x1E, 0x5A, 0x00, 0x00, 0x1F, 0x5D, 0x00, 0x00,
    0x20, 0xE0, 0x00, 0x00, 0x21, 0xE7, 0x00, 0x00,
    0x22, 0xEE, 0x00, 0x00, 0x23, 0xE9, 0x00, 0x00,
    0x24, 0xFC, 0x00, 0x00, 0x25, 0xFB, 0x00, 0x00,
    0x26, 0xF2, 0x00, 0x00
};

/* 13 节 logical cell 的 expected mV。 */
static const uint16_t GOLD_CELL_MV[13] =
{
    2365U, 2462U, 2559U, 2657U, 2754U, 2851U, 2948U,
    3046U, 3240U, 3338U, 3435U, 3532U, 3727U
};

static const uint8_t GOLD_BAT_READS[4] =
    { 0x4EU, 0xAFU, 0x20U, 0xE0U };   /* BAT raw 0x4E20→30790 mV */

static const uint8_t GOLD_BAT_ZERO_READS[4] =
    { 0x00U, 0x42U, 0x00U, 0x00U };   /* BAT raw 0x0000→390 mV */

static const uint8_t GOLD_BAT_MAX_READS[4] =
    { 0xFFU, 0xB1U, 0xFFU, 0xF3U };   /* BAT raw 0xFFFF→100003 mV */

static const uint8_t GOLD_CC_READS[4] =
    { 0x7FU, 0x38U, 0xFFU, 0xF3U };   /* CC raw 0x7FFF */

static const uint8_t GOLD_CC_ZERO_READS[4] =
    { 0x00U, 0x42U, 0x00U, 0x00U };   /* CC raw 0x0000 */

static const uint8_t GOLD_CC_MIN_READS[4] =
    { 0x80U, 0xCBU, 0x00U, 0x00U };   /* CC raw 0x8000 */

static const uint8_t GOLD_CC_NEG_READS[4] =
    { 0xFFU, 0xB1U, 0xFFU, 0xF3U };   /* CC raw 0xFFFF */

static const uint8_t GOLD_TS_READS[4] =
    { 0x10U, 0x32U, 0x00U, 0x00U };   /* TS1 raw 0x1000 -> 9016 ohm */

static uint32_t Test_CellWindowGolden(void)
{
    BQ76940_t device;
    SoftI2C_t bus;
    uint16_t cell_mv[BQ76940_MEASUREMENT_CELL_COUNT];
    uint32_t failures;
    uint8_t index;

    failures = 0UL;

    /* 成功路径：完整 30-byte window，全部 CRC 有效。 */
    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(GOLD_VC_WINDOW_READS, 60U);
    for (index = 0U; index < 13U; ++index)
    {
        cell_mv[index] = 0xFFFFU;
    }
    TEST_CHECK(BQ76940_ReadCellVoltages13(&device, &GOLD_CAL, cell_mv) ==
               BQ76940_STATUS_OK);
    for (index = 0U; index < 13U; ++index)
    {
        TEST_CHECK(cell_mv[index] == GOLD_CELL_MV[index]);
    }

    /* 恰好一个 block transaction：一次 START、RESTART、STOP。 */
    TEST_CHECK(Mock_CountEvent(TRACE_START) == 1UL);
    TEST_CHECK(Mock_CountEvent(TRACE_RESTART) == 1UL);
    TEST_CHECK(Mock_CountEvent(TRACE_STOP) == 1UL);
    /* pointer 写 VC1_HI，随后使用 wire-read address。 */
    TEST_CHECK(s_mock.trace[2] == EVT(TRACE_WRITE_BYTE, BQ76940_REG_VC1_HI));
    TEST_CHECK(s_mock.trace[4] == EVT(TRACE_WRITE_ADDRESS, 0x11U));

    /* 30 data+30 CRC=60 个 read-byte event。 */
    TEST_CHECK(Mock_CountEvent(TRACE_READ_BYTE) == 60UL);

    return failures;
}

static uint32_t Test_CellTransactional(void)
{
    BQ76940_t device;
    SoftI2C_t bus;
    uint16_t cell_mv[BQ76940_MEASUREMENT_CELL_COUNT];
    uint16_t expected[BQ76940_MEASUREMENT_CELL_COUNT];
    BQ76940_Calibration_t invalid_cal;
    BQ76940_Calibration_t negative_cal;
    uint8_t zero_window_reads[60];
    uint32_t failures;
    uint8_t index;

    failures = 0UL;

    /* 第 20 个 data CRC 失败返回 CRC_MISMATCH，caller array 逐 byte 不变。 */
    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(GOLD_VC_WINDOW_READS, 60U);
    for (index = 0U; index < 13U; ++index)
    {
        expected[index] = (uint16_t)(0x1000U + index);
        cell_mv[index] = expected[index];
    }
    /* reads[41] 是 data reads[40] 的 CRC，故意破坏该 byte。 */
    s_mock.reads[41] = (uint8_t)(s_mock.reads[41] ^ 0xFFU);
    TEST_CHECK(BQ76940_ReadCellVoltages13(&device, &GOLD_CAL, cell_mv) ==
               BQ76940_STATUS_CRC_MISMATCH);
    for (index = 0U; index < 13U; ++index)
    {
        TEST_CHECK(cell_mv[index] == expected[index]);
    }

    /* window 中途 I2C timeout，caller array 不变。 */
    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(GOLD_VC_WINDOW_READS, 60U);
    Mock_FailAt(50, SOFT_I2C_STATUS_TIMEOUT);
    for (index = 0U; index < 13U; ++index)
    {
        cell_mv[index] = expected[index];
    }
    TEST_CHECK(BQ76940_ReadCellVoltages13(&device, &GOLD_CAL, cell_mv) ==
               BQ76940_STATUS_I2C_TIMEOUT);
    for (index = 0U; index < 13U; ++index)
    {
        TEST_CHECK(cell_mv[index] == expected[index]);
    }

    /* calibration 非法返回 CALIBRATION_INVALID，array 不变。 */
    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(GOLD_VC_WINDOW_READS, 60U);
    invalid_cal.gain_uv_per_lsb = 100U;
    invalid_cal.offset_mv = 0;
    invalid_cal.valid = false;
    for (index = 0U; index < 13U; ++index)
    {
        cell_mv[index] = expected[index];
    }
    TEST_CHECK(BQ76940_ReadCellVoltages13(&device, &invalid_cal, cell_mv) ==
               BQ76940_STATUS_CALIBRATION_INVALID);
    for (index = 0U; index < 13U; ++index)
    {
        TEST_CHECK(cell_mv[index] == expected[index]);
    }

    /* 合法 negative offset 仍可能使某 raw cell 不可表示，此时整组 13-cell 保持 transaction。 */
    zero_window_reads[0] = 0U;
    zero_window_reads[1] = BQ76940_CRC8_FirstRead(0x11U, 0U);
    for (index = 1U; index < 30U; ++index)
    {
        zero_window_reads[index * 2U] = 0U;
        zero_window_reads[(index * 2U) + 1U] =
            BQ76940_CRC8_NextByte(0U);
    }
    negative_cal.gain_uv_per_lsb = 365U;
    negative_cal.offset_mv = -128;
    negative_cal.valid = true;
    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(zero_window_reads, 60U);
    for (index = 0U; index < 13U; ++index)
    {
        cell_mv[index] = expected[index];
    }
    TEST_CHECK(BQ76940_ReadCellVoltages13(&device, &negative_cal, cell_mv) ==
               BQ76940_STATUS_RANGE_ERROR);
    for (index = 0U; index < 13U; ++index)
    {
        TEST_CHECK(cell_mv[index] == expected[index]);
    }

    /* NULL output。 */
    Mock_ReadyDevice(&device, &bus);
    TEST_CHECK(BQ76940_ReadCellVoltages13(&device, &GOLD_CAL, NULL) ==
               BQ76940_STATUS_INVALID_ARGUMENT);

    /* device 尚未初始化。 */
    bus.initialized = false;
    TEST_CHECK(BQ76940_ReadCellVoltages13(&device, &GOLD_CAL, cell_mv) ==
               BQ76940_STATUS_NOT_INITIALIZED);

    return failures;
}

static uint32_t Test_PackVoltage(void)
{
    BQ76940_t device;
    SoftI2C_t bus;
    uint32_t pack_mv;
    uint32_t failures;
    uint32_t original;
    BQ76940_Calibration_t negative_cal;

    failures = 0UL;

    /* nominal：BAT=0x4E20、GAIN=380、OFFSET=+30、13 cells→30790 mV。 */
    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(GOLD_BAT_READS, 4U);
    original = 0xDEADBEEFUL;
    pack_mv = original;
    TEST_CHECK(BQ76940_ReadPackVoltageMv(&device, &GOLD_CAL, &pack_mv) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(pack_mv == 30790UL);
    TEST_CHECK(Mock_CountEvent(TRACE_START) == 1UL);
    TEST_CHECK(Mock_CountEvent(TRACE_STOP) == 1UL);

    /* BAT=0 时只剩 offset floor：13×30 mV=390 mV。 */
    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(GOLD_BAT_ZERO_READS, 4U);
    pack_mv = original;
    TEST_CHECK(BQ76940_ReadPackVoltageMv(&device, &GOLD_CAL, &pack_mv) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(pack_mv == 390UL);

    /* calibration 后 pack 为负则失败，不提交输出。 */
    negative_cal.gain_uv_per_lsb = 365U;
    negative_cal.offset_mv = -128;
    negative_cal.valid = true;
    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(GOLD_BAT_ZERO_READS, 4U);
    pack_mv = original;
    TEST_CHECK(BQ76940_ReadPackVoltageMv(&device, &negative_cal, &pack_mv) ==
               BQ76940_STATUS_RANGE_ERROR);
    TEST_CHECK(pack_mv == original);

    /* BAT=0xFFFF→100003 mV，可装入 uint32。 */
    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(GOLD_BAT_MAX_READS, 4U);
    pack_mv = original;
    TEST_CHECK(BQ76940_ReadPackVoltageMv(&device, &GOLD_CAL, &pack_mv) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(pack_mv == 100003UL);

    /* CRC failure 保持 caller value。 */
    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(GOLD_BAT_READS, 4U);
    s_mock.reads[1] = 0x00U;   /* 破坏 data[0] 的 CRC */
    pack_mv = original;
    TEST_CHECK(BQ76940_ReadPackVoltageMv(&device, &GOLD_CAL, &pack_mv) ==
               BQ76940_STATUS_CRC_MISMATCH);
    TEST_CHECK(pack_mv == original);

    /* NULL output。 */
    Mock_ReadyDevice(&device, &bus);
    TEST_CHECK(BQ76940_ReadPackVoltageMv(&device, &GOLD_CAL, NULL) ==
               BQ76940_STATUS_INVALID_ARGUMENT);

    return failures;
}

static uint32_t Test_Cc(void)
{
    BQ76940_t device;
    SoftI2C_t bus;
    int16_t cc_raw;
    int32_t current_ma;
    uint32_t failures;
    int16_t original_raw;
    int32_t original_ma;

    failures = 0UL;

    /* signed16 边界：0x7FFF→32767，0x8000→-32768，0xFFFF→-1。 */
    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(GOLD_CC_READS, 4U);
    original_raw = 0x7A7AU;
    cc_raw = original_raw;
    TEST_CHECK(BQ76940_ReadCcRaw(&device, &cc_raw) == BQ76940_STATUS_OK);
    TEST_CHECK(cc_raw == 32767);

    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(GOLD_CC_ZERO_READS, 4U);
    cc_raw = original_raw;
    TEST_CHECK(BQ76940_ReadCcRaw(&device, &cc_raw) == BQ76940_STATUS_OK);
    TEST_CHECK(cc_raw == 0);

    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(GOLD_CC_MIN_READS, 4U);
    cc_raw = original_raw;
    TEST_CHECK(BQ76940_ReadCcRaw(&device, &cc_raw) == BQ76940_STATUS_OK);
    TEST_CHECK(cc_raw == -32768);

    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(GOLD_CC_NEG_READS, 4U);
    cc_raw = original_raw;
    TEST_CHECK(BQ76940_ReadCcRaw(&device, &cc_raw) == BQ76940_STATUS_OK);
    TEST_CHECK(cc_raw == -1);

    /* CRC failure 保持 caller raw。 */
    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(GOLD_CC_READS, 4U);
    s_mock.reads[1] = 0x00U;
    cc_raw = original_raw;
    TEST_CHECK(BQ76940_ReadCcRaw(&device, &cc_raw) ==
               BQ76940_STATUS_CRC_MISMATCH);
    TEST_CHECK(cc_raw == original_raw);

    /* 换算 golden：Rsense=4000 uΩ（4 mΩ）。 */
    original_ma = 0x12345678L;

    current_ma = original_ma;
    TEST_CHECK(BQ76940_ConvertCcRawToCurrentMa(
                   0, 4000UL, 1, &current_ma) == BQ76940_STATUS_OK);
    TEST_CHECK(current_ma == 0);

    current_ma = original_ma;
    TEST_CHECK(BQ76940_ConvertCcRawToCurrentMa(
                   1, 4000UL, 1, &current_ma) == BQ76940_STATUS_OK);
    TEST_CHECK(current_ma == 2);

    current_ma = original_ma;
    TEST_CHECK(BQ76940_ConvertCcRawToCurrentMa(
                   32767, 4000UL, 1, &current_ma) == BQ76940_STATUS_OK);
    TEST_CHECK(current_ma == 69138);

    current_ma = original_ma;
    TEST_CHECK(BQ76940_ConvertCcRawToCurrentMa(
                   -32768, 4000UL, 1, &current_ma) == BQ76940_STATUS_OK);
    TEST_CHECK(current_ma == -69140);

    current_ma = original_ma;
    TEST_CHECK(BQ76940_ConvertCcRawToCurrentMa(
                   -1, 4000UL, 1, &current_ma) == BQ76940_STATUS_OK);
    TEST_CHECK(current_ma == -2);

    /* polarity 反向。 */
    current_ma = original_ma;
    TEST_CHECK(BQ76940_ConvertCcRawToCurrentMa(
                   10000, 4000UL, -1, &current_ma) == BQ76940_STATUS_OK);
    TEST_CHECK(current_ma == -21100);

    current_ma = original_ma;
    TEST_CHECK(BQ76940_ConvertCcRawToCurrentMa(
                   -10000, 4000UL, -1, &current_ma) == BQ76940_STATUS_OK);
    TEST_CHECK(current_ma == 21100);

    /* polarity 非法。 */
    TEST_CHECK(BQ76940_ConvertCcRawToCurrentMa(
                   1, 4000UL, 0, &current_ma) ==
               BQ76940_STATUS_INVALID_ARGUMENT);

    /* Rsense=0 返回 RANGE_ERROR。 */
    TEST_CHECK(BQ76940_ConvertCcRawToCurrentMa(
                   1, 0UL, 1, &current_ma) == BQ76940_STATUS_RANGE_ERROR);

    /* NULL output。 */
    TEST_CHECK(BQ76940_ConvertCcRawToCurrentMa(
                   1, 4000UL, 1, NULL) == BQ76940_STATUS_INVALID_ARGUMENT);

    return failures;
}

static uint32_t Test_Ts1(void)
{
    BQ76940_t device;
    SoftI2C_t bus;
    uint16_t ts_raw;
    uint32_t resistance;
    uint32_t failures;
    uint16_t original_raw;
    uint32_t original_res;

    failures = 0UL;

    /* 0x1000→9016 Ω；0x0A00→4211 Ω。 */
    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(GOLD_TS_READS, 4U);
    original_raw = 0x5A5AU;
    ts_raw = original_raw;
    TEST_CHECK(BQ76940_ReadTs1Raw(&device, &ts_raw) == BQ76940_STATUS_OK);
    TEST_CHECK(ts_raw == 0x1000U);
    TEST_CHECK(Mock_CountEvent(TRACE_START) == 1UL);

    original_res = 0xDEADBEEFUL;
    resistance = original_res;
    TEST_CHECK(BQ76940_ConvertTs1RawToResistanceOhm(0x1000U, &resistance) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(resistance == 9016UL);

    resistance = original_res;
    TEST_CHECK(BQ76940_ConvertTs1RawToResistanceOhm(0x0A00U, &resistance) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(resistance == 4211UL);

    /* raw=0→resistance=0。 */
    resistance = original_res;
    TEST_CHECK(BQ76940_ConvertTs1RawToResistanceOhm(0x0000U, &resistance) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(resistance == 0UL);

    /* denominator 相邻边界：8638 低于 3.3 V，8639 超出；失败保持输出不变。 */
    resistance = original_res;
    TEST_CHECK(BQ76940_ConvertTs1RawToResistanceOhm(8638U, &resistance) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(resistance != original_res);
    resistance = original_res;
    TEST_CHECK(BQ76940_ConvertTs1RawToResistanceOhm(8639U, &resistance) ==
               BQ76940_STATUS_RANGE_ERROR);
    TEST_CHECK(resistance == original_res);

    /* 0x27DC：VTS>=3.3 V 返回 RANGE_ERROR，输出不变。 */
    resistance = original_res;
    TEST_CHECK(BQ76940_ConvertTs1RawToResistanceOhm(0x27DCU, &resistance) ==
               BQ76940_STATUS_RANGE_ERROR);
    TEST_CHECK(resistance == original_res);

    /* raw>0x3FFF 返回 RANGE_ERROR。 */
    resistance = original_res;
    TEST_CHECK(BQ76940_ConvertTs1RawToResistanceOhm(0x4000U, &resistance) ==
               BQ76940_STATUS_RANGE_ERROR);

    /* NULL output。 */
    TEST_CHECK(BQ76940_ConvertTs1RawToResistanceOhm(0x1000U, NULL) ==
               BQ76940_STATUS_INVALID_ARGUMENT);

    /* TS1 read CRC failure 保持 caller raw。 */
    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(GOLD_TS_READS, 4U);
    s_mock.reads[1] = 0x00U;
    ts_raw = original_raw;
    TEST_CHECK(BQ76940_ReadTs1Raw(&device, &ts_raw) ==
               BQ76940_STATUS_CRC_MISMATCH);
    TEST_CHECK(ts_raw == original_raw);

    return failures;
}

static uint32_t Test_WriteCommitBoundary(void)
{
    BQ76940_t device;
    SoftI2C_t bus;
    BQ76940_Status_t status;
    uint32_t failures;

    failures = 0UL;

    /* 正常 single-byte write 顺序：START/address/register/data/CRC/STOP。 */
    Mock_ReadyDevice(&device, &bus);
    status = BQ76940_WriteByte(&device, BQ76940_REG_SYS_STAT, 0x80U);
    TEST_CHECK(status == BQ76940_STATUS_OK);
    TEST_CHECK(s_mock.trace_count == 6U);

    /* payload/CRC ACK 后 STOP 失败，无法证明 register side effect 是否 commit。 */
    Mock_ReadyDevice(&device, &bus);
    Mock_FailAt(5, SOFT_I2C_STATUS_TIMEOUT);
    status = BQ76940_WriteByte(&device, BQ76940_REG_SYS_STAT, 0x80U);
    TEST_CHECK(status == BQ76940_STATUS_WRITE_FINALIZATION_AMBIGUOUS);
    TEST_CHECK(s_mock.trace_count == 6U);

    /* data NACK 发生在完整 payload/CRC ACK 前，属于 definite rejection。 */
    Mock_ReadyDevice(&device, &bus);
    Mock_FailAt(3, SOFT_I2C_STATUS_NACK_DATA);
    status = BQ76940_WriteByte(&device, BQ76940_REG_SYS_STAT, 0x80U);
    TEST_CHECK(status == BQ76940_STATUS_I2C_NACK);

    /* CRC NACK 同样是明确拒绝，并保持独立状态。 */
    Mock_ReadyDevice(&device, &bus);
    Mock_FailAt(4, SOFT_I2C_STATUS_NACK_DATA);
    status = BQ76940_WriteByte(&device, BQ76940_REG_SYS_STAT, 0x80U);
    TEST_CHECK(status == BQ76940_STATUS_CRC_REJECTED);

    return failures;
}

volatile uint32_t g_p4_cell_window_failures;
volatile uint32_t g_p4_cell_trans_failures;
volatile uint32_t g_p4_pack_failures;
volatile uint32_t g_p4_cc_failures;
volatile uint32_t g_p4_ts_failures;
volatile uint32_t g_p4_write_commit_failures;

uint32_t Test_Phase4_Measurement(void)
{
    uint32_t failures;

    failures = 0UL;
    g_p4_cell_window_failures = Test_CellWindowGolden();
    g_p4_cell_trans_failures = Test_CellTransactional();
    g_p4_pack_failures = Test_PackVoltage();
    g_p4_cc_failures = Test_Cc();
    g_p4_ts_failures = Test_Ts1();
    g_p4_write_commit_failures = Test_WriteCommitBoundary();
    failures = g_p4_cell_window_failures + g_p4_cell_trans_failures +
               g_p4_pack_failures + g_p4_cc_failures + g_p4_ts_failures +
               g_p4_write_commit_failures;
    return failures;
}
