#include "test_phase3.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bsp_bq76940.h"
#include "bsp_bq76940_regs.h"

#define TRACE_CAPACITY              (128U)
#define READ_CAPACITY               (32U)
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

static bool Mock_TraceEquals(const uint16_t *expected, uint8_t count)
{
    uint8_t index;

    if (s_mock.trace_count != count)
    {
        return false;
    }
    for (index = 0U; index < count; ++index)
    {
        if (s_mock.trace[index] != expected[index])
        {
            return false;
        }
    }
    return true;
}

bool BSP_SoftI2C_IsInitialized(const SoftI2C_t *bus)
{
    return (bus != NULL) && bus->initialized;
}

SoftI2C_Status_t BSP_SoftI2C_Start(SoftI2C_t *bus)
{
    (void)bus;
    return Mock_Record(TRACE_START, 0U);
}

SoftI2C_Status_t BSP_SoftI2C_RepeatedStart(SoftI2C_t *bus)
{
    (void)bus;
    return Mock_Record(TRACE_RESTART, 0U);
}

SoftI2C_Status_t BSP_SoftI2C_Stop(SoftI2C_t *bus)
{
    (void)bus;
    return Mock_Record(TRACE_STOP, 0U);
}

SoftI2C_Status_t BSP_SoftI2C_WriteAddress(SoftI2C_t *bus,
                                      uint8_t address_byte)
{
    (void)bus;
    return Mock_Record(TRACE_WRITE_ADDRESS, address_byte);
}

SoftI2C_Status_t BSP_SoftI2C_WriteByte(SoftI2C_t *bus, uint8_t value)
{
    (void)bus;
    return Mock_Record(TRACE_WRITE_BYTE, value);
}

SoftI2C_Status_t BSP_SoftI2C_ReadByteBegin(SoftI2C_t *bus, uint8_t *value)
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

SoftI2C_Status_t BSP_SoftI2C_SendReadResponse(
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
    (void)BSP_BQ76940_Init(device, bus);
}

uint32_t Test_Phase3_Transport(void)
{
    static const uint16_t write_one_trace[] =
    {
        EVT(TRACE_START, 0), EVT(TRACE_WRITE_ADDRESS, 0x10),
        EVT(TRACE_WRITE_BYTE, 0x0B), EVT(TRACE_WRITE_BYTE, 0x19),
        EVT(TRACE_WRITE_BYTE, 0x7A), EVT(TRACE_STOP, 0)
    };
    static const uint16_t write_three_trace[] =
    {
        EVT(TRACE_START, 0), EVT(TRACE_WRITE_ADDRESS, 0x10),
        EVT(TRACE_WRITE_BYTE, 0x04), EVT(TRACE_WRITE_BYTE, 0x10),
        EVT(TRACE_WRITE_BYTE, 0x86), EVT(TRACE_WRITE_BYTE, 0x40),
        EVT(TRACE_WRITE_BYTE, 0xC7), EVT(TRACE_WRITE_BYTE, 0x00),
        EVT(TRACE_WRITE_BYTE, 0x00), EVT(TRACE_STOP, 0)
    };
    static const uint16_t read_one_trace[] =
    {
        EVT(TRACE_START, 0), EVT(TRACE_WRITE_ADDRESS, 0x10),
        EVT(TRACE_WRITE_BYTE, 0x0C), EVT(TRACE_RESTART, 0),
        EVT(TRACE_WRITE_ADDRESS, 0x11), EVT(TRACE_READ_BYTE, 0x12),
        EVT(TRACE_READ_RESPONSE, SOFT_I2C_MASTER_ACK),
        EVT(TRACE_READ_BYTE, 0x3C),
        EVT(TRACE_READ_RESPONSE, SOFT_I2C_MASTER_NACK), EVT(TRACE_STOP, 0)
    };
    static const uint16_t read_three_trace[] =
    {
        EVT(TRACE_START, 0), EVT(TRACE_WRITE_ADDRESS, 0x10),
        EVT(TRACE_WRITE_BYTE, 0x0C), EVT(TRACE_RESTART, 0),
        EVT(TRACE_WRITE_ADDRESS, 0x11),
        EVT(TRACE_READ_BYTE, 0x12), EVT(TRACE_READ_RESPONSE, 0),
        EVT(TRACE_READ_BYTE, 0x3C), EVT(TRACE_READ_RESPONSE, 0),
        EVT(TRACE_READ_BYTE, 0x34), EVT(TRACE_READ_RESPONSE, 0),
        EVT(TRACE_READ_BYTE, 0x8C), EVT(TRACE_READ_RESPONSE, 0),
        EVT(TRACE_READ_BYTE, 0x56), EVT(TRACE_READ_RESPONSE, 0),
        EVT(TRACE_READ_BYTE, 0xA5), EVT(TRACE_READ_RESPONSE, 1),
        EVT(TRACE_STOP, 0)
    };
    static const uint16_t read_two_trace[] =
    {
        EVT(TRACE_START, 0), EVT(TRACE_WRITE_ADDRESS, 0x10),
        EVT(TRACE_WRITE_BYTE, 0x0C), EVT(TRACE_RESTART, 0),
        EVT(TRACE_WRITE_ADDRESS, 0x11),
        EVT(TRACE_READ_BYTE, 0x12), EVT(TRACE_READ_RESPONSE, 0),
        EVT(TRACE_READ_BYTE, 0x3C), EVT(TRACE_READ_RESPONSE, 0),
        EVT(TRACE_READ_BYTE, 0x34), EVT(TRACE_READ_RESPONSE, 0),
        EVT(TRACE_READ_BYTE, 0x8C), EVT(TRACE_READ_RESPONSE, 1),
        EVT(TRACE_STOP, 0)
    };
    static const uint8_t write_three_data[] = { 0x10U, 0x40U, 0x00U };
    static const uint8_t read_one_data[] = { 0x12U, 0x3CU };
    static const uint8_t read_three_data[] =
        { 0x12U, 0x3CU, 0x34U, 0x8CU, 0x56U, 0xA5U };
    static const uint8_t read_bad_crc[] =
        { 0x12U, 0x3CU, 0x34U, 0x00U, 0x56U, 0xA5U };
    static const uint8_t calibration_reads[] =
        { 0x04U, 0x5EU, 0x81U, 0xCCU, 0xA0U, 0x2BU };
    static const uint8_t calibration_bad_third[] =
        { 0x04U, 0x5EU, 0x81U, 0xCCU, 0xA0U, 0x00U };
    BQ76940_Calibration_t calibration;
    BQ76940_Calibration_t original_calibration;
    BQ76940_Status_t status;
    BQ76940_t device;
    SoftI2C_t bus;
    uint32_t failures;
    uint16_t adjacent;
    uint8_t output[3];
    uint8_t value;

    failures = 0UL;
    Mock_Reset();
    bus.initialized = false;
    TEST_CHECK(BSP_BQ76940_Init(NULL, &bus) == BQ76940_STATUS_INVALID_ARGUMENT);
    TEST_CHECK(BSP_BQ76940_Init(&device, &bus) == BQ76940_STATUS_NOT_INITIALIZED);
    TEST_CHECK(s_mock.trace_count == 0U);

    Mock_ReadyDevice(&device, &bus);
    TEST_CHECK(BSP_BQ76940_WriteByte(&device, BQ76940_REG_CC_CFG, 0x19U) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(Mock_TraceEquals(write_one_trace, 6U));

    Mock_ReadyDevice(&device, &bus);
    TEST_CHECK(BSP_BQ76940_WriteBlock(&device, BQ76940_REG_SYS_CTRL1,
                                 write_three_data, 3U) == BQ76940_STATUS_OK);
    TEST_CHECK(Mock_TraceEquals(write_three_trace, 10U));

    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(read_one_data, 2U);
    value = 0xEEU;
    TEST_CHECK(BSP_BQ76940_ReadByte(&device, BQ76940_REG_VC1_HI, &value) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(value == 0x12U);
    TEST_CHECK(Mock_TraceEquals(read_one_trace, 10U));

    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(read_three_data, 6U);
    output[0] = 0xEEU;
    output[1] = 0xEEU;
    output[2] = 0xEEU;
    TEST_CHECK(BSP_BQ76940_ReadBlock(&device, BQ76940_REG_VC1_HI,
                                output, 3U) == BQ76940_STATUS_OK);
    TEST_CHECK((output[0] == 0x12U) && (output[1] == 0x34U) &&
               (output[2] == 0x56U));
    TEST_CHECK(Mock_TraceEquals(read_three_trace, 18U));

    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(read_bad_crc, 6U);
    output[0] = 0xA1U;
    output[1] = 0xB2U;
    output[2] = 0xC3U;
    status = BSP_BQ76940_ReadBlock(&device, BQ76940_REG_VC1_HI, output, 3U);
    TEST_CHECK(status == BQ76940_STATUS_CRC_MISMATCH);
    TEST_CHECK((output[0] == 0xA1U) && (output[1] == 0xB2U) &&
               (output[2] == 0xC3U));
    TEST_CHECK(s_mock.trace[s_mock.trace_count - 2U] ==
               EVT(TRACE_READ_RESPONSE, SOFT_I2C_MASTER_NACK));
    TEST_CHECK(s_mock.trace[s_mock.trace_count - 1U] == EVT(TRACE_STOP, 0));
    TEST_CHECK((s_mock.trace_count == 14U) && (s_mock.read_index == 4U));

    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(read_bad_crc, 4U);
    output[0] = 0xA1U;
    output[1] = 0xB2U;
    s_mock.fail_trace_index = 12;
    s_mock.fail_status = SOFT_I2C_STATUS_TIMEOUT;
    TEST_CHECK(BSP_BQ76940_ReadBlock(&device, 0x0CU, output, 2U) ==
               BQ76940_STATUS_CRC_MISMATCH);
    TEST_CHECK((output[0] == 0xA1U) && (output[1] == 0xB2U));
    TEST_CHECK(s_mock.trace[s_mock.trace_count - 1U] == EVT(TRACE_STOP, 0));

    Mock_ReadyDevice(&device, &bus);
    s_mock.fail_trace_index = 1;
    s_mock.fail_status = SOFT_I2C_STATUS_NACK_ADDRESS;
    TEST_CHECK(BSP_BQ76940_WriteByte(&device, 0x04U, 0x18U) ==
               BQ76940_STATUS_I2C_NACK);
    TEST_CHECK((s_mock.trace_count == 3U) &&
               (s_mock.trace[2] == EVT(TRACE_STOP, 0)));

    Mock_ReadyDevice(&device, &bus);
    s_mock.fail_trace_index = 2;
    s_mock.fail_status = SOFT_I2C_STATUS_NACK_DATA;
    TEST_CHECK(BSP_BQ76940_WriteByte(&device, 0x04U, 0x18U) ==
               BQ76940_STATUS_I2C_NACK);
    TEST_CHECK(s_mock.trace[s_mock.trace_count - 1U] == EVT(TRACE_STOP, 0));

    Mock_ReadyDevice(&device, &bus);
    s_mock.fail_trace_index = 3;
    s_mock.fail_status = SOFT_I2C_STATUS_NACK_DATA;
    TEST_CHECK(BSP_BQ76940_WriteByte(&device, 0x04U, 0x18U) ==
               BQ76940_STATUS_I2C_NACK);

    Mock_ReadyDevice(&device, &bus);
    s_mock.fail_trace_index = 4;
    s_mock.fail_status = SOFT_I2C_STATUS_NACK_DATA;
    TEST_CHECK(BSP_BQ76940_WriteByte(&device, 0x04U, 0x18U) ==
               BQ76940_STATUS_CRC_REJECTED);

    Mock_ReadyDevice(&device, &bus);
    s_mock.fail_trace_index = 5;
    s_mock.fail_status = SOFT_I2C_STATUS_NACK_DATA;
    TEST_CHECK(BSP_BQ76940_WriteBlock(&device, BQ76940_REG_SYS_CTRL1,
                                 write_three_data, 3U) ==
               BQ76940_STATUS_I2C_NACK);
    TEST_CHECK(s_mock.trace[s_mock.trace_count - 1U] == EVT(TRACE_STOP, 0));

    Mock_ReadyDevice(&device, &bus);
    s_mock.fail_trace_index = 6;
    s_mock.fail_status = SOFT_I2C_STATUS_NACK_DATA;
    TEST_CHECK(BSP_BQ76940_WriteBlock(&device, BQ76940_REG_SYS_CTRL1,
                                 write_three_data, 3U) ==
               BQ76940_STATUS_CRC_REJECTED);
    TEST_CHECK(s_mock.trace[s_mock.trace_count - 1U] == EVT(TRACE_STOP, 0));

    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(read_three_data, 6U);
    output[0] = 0x11U;
    output[1] = 0x22U;
    output[2] = 0x33U;
    s_mock.fail_trace_index = 9;
    s_mock.fail_status = SOFT_I2C_STATUS_TIMEOUT;
    TEST_CHECK(BSP_BQ76940_ReadBlock(&device, 0x0CU, output, 3U) ==
               BQ76940_STATUS_I2C_TIMEOUT);
    TEST_CHECK((output[0] == 0x11U) && (output[1] == 0x22U) &&
               (output[2] == 0x33U));
    TEST_CHECK(s_mock.trace[s_mock.trace_count - 1U] == EVT(TRACE_STOP, 0));

    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(read_one_data, 2U);
    s_mock.fail_trace_index = 4;
    s_mock.fail_status = SOFT_I2C_STATUS_NACK_ADDRESS;
    value = 0x5AU;
    TEST_CHECK(BSP_BQ76940_ReadByte(&device, 0x0CU, &value) ==
               BQ76940_STATUS_I2C_NACK);
    TEST_CHECK(value == 0x5AU);
    TEST_CHECK(s_mock.trace[s_mock.trace_count - 1U] == EVT(TRACE_STOP, 0));

    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(read_one_data, 2U);
    s_mock.fail_trace_index = 3;
    s_mock.fail_status = SOFT_I2C_STATUS_STATE_ERROR;
    value = 0x5AU;
    TEST_CHECK(BSP_BQ76940_ReadByte(&device, 0x0CU, &value) ==
               BQ76940_STATUS_I2C_ERROR);
    TEST_CHECK(value == 0x5AU);
    TEST_CHECK(s_mock.trace[s_mock.trace_count - 1U] == EVT(TRACE_STOP, 0));

    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(read_one_data, 2U);
    s_mock.fail_trace_index = 5;
    s_mock.fail_status = SOFT_I2C_STATUS_STATE_ERROR;
    value = 0x5AU;
    TEST_CHECK(BSP_BQ76940_ReadByte(&device, 0x0CU, &value) ==
               BQ76940_STATUS_I2C_ERROR);
    TEST_CHECK(value == 0x5AU);
    TEST_CHECK(s_mock.trace[s_mock.trace_count - 1U] == EVT(TRACE_STOP, 0));

    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(read_one_data, 2U);
    s_mock.fail_trace_index = 6;
    s_mock.fail_status = SOFT_I2C_STATUS_TIMEOUT;
    value = 0x5AU;
    TEST_CHECK(BSP_BQ76940_ReadByte(&device, 0x0CU, &value) ==
               BQ76940_STATUS_I2C_TIMEOUT);
    TEST_CHECK(value == 0x5AU);
    TEST_CHECK(s_mock.trace[s_mock.trace_count - 1U] == EVT(TRACE_STOP, 0));

    Mock_ReadyDevice(&device, &bus);
    TEST_CHECK(BSP_BQ76940_ReadBlock(&device, 0U, NULL, 1U) ==
               BQ76940_STATUS_INVALID_ARGUMENT);
    TEST_CHECK(BSP_BQ76940_ReadBlock(&device, 0U, output, 0U) ==
               BQ76940_STATUS_INVALID_ARGUMENT);
    TEST_CHECK(BSP_BQ76940_ReadBlock(&device, 0U, output,
                                BQ76940_MAX_BLOCK_LENGTH + 1U) ==
               BQ76940_STATUS_RANGE_ERROR);
    TEST_CHECK(s_mock.trace_count == 0U);

    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(read_one_data, 2U);
    output[0] = 0x77U;
    s_mock.fail_trace_index = 9;
    s_mock.fail_status = SOFT_I2C_STATUS_TIMEOUT;
    TEST_CHECK(BSP_BQ76940_ReadByte(&device, 0x0CU, output) ==
               BQ76940_STATUS_I2C_TIMEOUT);
    TEST_CHECK(output[0] == 0x77U);

    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(read_bad_crc, 4U);
    output[0] = 0xA1U;
    output[1] = 0xB2U;
    s_mock.fail_trace_index = 13;
    s_mock.fail_status = SOFT_I2C_STATUS_TIMEOUT;
    TEST_CHECK(BSP_BQ76940_ReadBlock(&device, 0x0CU, output, 2U) ==
               BQ76940_STATUS_CRC_MISMATCH);
    TEST_CHECK((output[0] == 0xA1U) && (output[1] == 0xB2U));

    Mock_ReadyDevice(&device, &bus);
    s_mock.fail_trace_index = 0;
    s_mock.fail_status = SOFT_I2C_STATUS_STATE_ERROR;
    TEST_CHECK(BSP_BQ76940_WriteByte(&device, 0x04U, 0x18U) ==
               BQ76940_STATUS_I2C_ERROR);
    TEST_CHECK((s_mock.trace_count == 2U) &&
               (s_mock.trace[1] == EVT(TRACE_STOP, 0)));

    Mock_ReadyDevice(&device, &bus);
    TEST_CHECK(BSP_BQ76940_ReadBlock(&device, 0xF0U, output, 32U) ==
               BQ76940_STATUS_RANGE_ERROR);
    TEST_CHECK(BSP_BQ76940_WriteBlock(&device, 0xF0U,
                                  write_three_data, 32U) ==
               BQ76940_STATUS_RANGE_ERROR);
    TEST_CHECK(s_mock.trace_count == 0U);

    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(read_three_data, 4U);
    adjacent = 0xEEEEU;
    TEST_CHECK(BSP_BQ76940_ReadAdjacentU16(&device, 0x0CU, &adjacent) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(adjacent == 0x1234U);
    TEST_CHECK(Mock_TraceEquals(read_two_trace, 14U));
    {
        uint8_t index;
        uint8_t starts;
        uint8_t restarts;
        uint8_t stops;

        starts = 0U;
        restarts = 0U;
        stops = 0U;
        for (index = 0U; index < s_mock.trace_count; ++index)
        {
            if ((s_mock.trace[index] >> 8) == TRACE_START) { ++starts; }
            if ((s_mock.trace[index] >> 8) == TRACE_RESTART) { ++restarts; }
            if ((s_mock.trace[index] >> 8) == TRACE_STOP) { ++stops; }
        }
        TEST_CHECK((starts == 1U) && (restarts == 1U) && (stops == 1U));
    }

    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(calibration_reads, 6U);
    calibration.gain_uv_per_lsb = 999U;
    calibration.offset_mv = 99;
    calibration.valid = false;
    TEST_CHECK(BSP_BQ76940_ReadCalibration(&device, &calibration) ==
               BQ76940_STATUS_OK);
    TEST_CHECK((calibration.gain_uv_per_lsb == 378U) &&
               (calibration.offset_mv == -127) && calibration.valid);

    Mock_ReadyDevice(&device, &bus);
    Mock_SetReads(calibration_bad_third, 6U);
    original_calibration.gain_uv_per_lsb = 380U;
    original_calibration.offset_mv = 30;
    original_calibration.valid = true;
    calibration = original_calibration;
    TEST_CHECK(BSP_BQ76940_ReadCalibration(&device, &calibration) ==
               BQ76940_STATUS_CRC_MISMATCH);
    TEST_CHECK((calibration.gain_uv_per_lsb ==
                original_calibration.gain_uv_per_lsb) &&
               (calibration.offset_mv == original_calibration.offset_mv) &&
               (calibration.valid == original_calibration.valid));

    return failures;
}
