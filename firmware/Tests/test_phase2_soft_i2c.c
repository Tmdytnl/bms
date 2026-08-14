#include "test_phase2.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bsp_timer.h"
#include "soft_i2c.h"

#define MOCK_SCRIPT_CAPACITY       (32U)
#define MOCK_NEVER_RELEASE         (0xFFFFFFFFUL)

#define TEST_CHECK(expr_)          \
    do                             \
    {                              \
        if (!(expr_))              \
        {                          \
            ++failures;            \
        }                          \
    } while (0)

typedef struct
{
    bool scl_master_low;
    bool sda_master_low;
    bool scl_forced_low;
    bool sda_forced_low;
    bool delay_ok;
    bool sda_low_at_last_scl_release;
    uint16_t now_us;
    uint16_t stretch_reads_remaining;
    uint16_t stretch_next_release_reads;
    uint32_t scl_rising_edges;
    uint32_t release_sda_after_edges;
    bool sda_script[MOCK_SCRIPT_CAPACITY];
    uint8_t sda_script_length;
    uint8_t sda_script_index;
} MockBus_t;

static MockBus_t s_mock;

static void Mock_Reset(void)
{
    uint8_t index;

    s_mock.scl_master_low = false;
    s_mock.sda_master_low = false;
    s_mock.scl_forced_low = false;
    s_mock.sda_forced_low = false;
    s_mock.delay_ok = true;
    s_mock.sda_low_at_last_scl_release = false;
    s_mock.now_us = 0U;
    s_mock.stretch_reads_remaining = 0U;
    s_mock.stretch_next_release_reads = 0U;
    s_mock.scl_rising_edges = 0UL;
    s_mock.release_sda_after_edges = MOCK_NEVER_RELEASE;
    s_mock.sda_script_length = 0U;
    s_mock.sda_script_index = 0U;
    for (index = 0U; index < MOCK_SCRIPT_CAPACITY; ++index)
    {
        s_mock.sda_script[index] = true;
    }
}

static void Mock_SetSdaScript(const bool *values, uint8_t length)
{
    uint8_t index;

    s_mock.sda_script_length = length;
    s_mock.sda_script_index = 0U;
    for (index = 0U; index < length; ++index)
    {
        s_mock.sda_script[index] = values[index];
    }
}

static void Mock_SclDriveLow(void)
{
    s_mock.scl_master_low = true;
}

static void Mock_SclRelease(void)
{
    if (s_mock.scl_master_low)
    {
        ++s_mock.scl_rising_edges;
    }
    s_mock.scl_master_low = false;
    s_mock.sda_low_at_last_scl_release = s_mock.sda_master_low;
    if (s_mock.stretch_next_release_reads != 0U)
    {
        s_mock.stretch_reads_remaining = s_mock.stretch_next_release_reads;
        s_mock.stretch_next_release_reads = 0U;
    }
}

static bool Mock_SclRead(void)
{
    if (s_mock.scl_master_low || s_mock.scl_forced_low)
    {
        return false;
    }
    if (s_mock.stretch_reads_remaining != 0U)
    {
        --s_mock.stretch_reads_remaining;
        return false;
    }
    return true;
}

static void Mock_SdaDriveLow(void)
{
    s_mock.sda_master_low = true;
}

static void Mock_SdaRelease(void)
{
    s_mock.sda_master_low = false;
}

static bool Mock_SdaRead(void)
{
    bool value;

    if (s_mock.sda_master_low)
    {
        return false;
    }
    if (s_mock.sda_forced_low &&
        (s_mock.scl_rising_edges >= s_mock.release_sda_after_edges))
    {
        s_mock.sda_forced_low = false;
    }
    if (s_mock.sda_forced_low)
    {
        return false;
    }
    if (s_mock.sda_script_index < s_mock.sda_script_length)
    {
        value = s_mock.sda_script[s_mock.sda_script_index];
        ++s_mock.sda_script_index;
        return value;
    }
    return true;
}

static uint16_t Mock_TimeUs16(void)
{
    return s_mock.now_us;
}

static bool Mock_DelayUs(uint32_t delay_us)
{
    if (!s_mock.delay_ok)
    {
        return false;
    }
    s_mock.now_us = (uint16_t)(s_mock.now_us + (uint16_t)delay_us);
    return true;
}

static SoftI2C_LineOps_t Mock_Ops(void)
{
    SoftI2C_LineOps_t ops;

    ops.scl_drive_low = Mock_SclDriveLow;
    ops.scl_release = Mock_SclRelease;
    ops.scl_read = Mock_SclRead;
    ops.sda_drive_low = Mock_SdaDriveLow;
    ops.sda_release = Mock_SdaRelease;
    ops.sda_read = Mock_SdaRead;
    ops.time_us16 = Mock_TimeUs16;
    ops.delay_us = Mock_DelayUs;
    return ops;
}

static SoftI2C_Config_t Mock_Config(void)
{
    SoftI2C_Config_t config;

    config.half_cycle_us = 5U;
    config.scl_high_timeout_us = 20U;
    config.bus_free_timeout_us = 20U;
    return config;
}

static SoftI2C_Status_t Mock_Init(SoftI2C_t *bus)
{
    SoftI2C_LineOps_t ops;
    SoftI2C_Config_t config;

    ops = Mock_Ops();
    config = Mock_Config();
    return SoftI2C_Init(bus, &ops, &config);
}

uint32_t Test_Phase2_SoftI2C(void)
{
    static const bool address_ack_script[] = { true, false };
    static const bool address_nack_script[] = { true, true };
    static const bool data_ack_script[] = { true, false, false };
    static const bool data_nack_script[] = { true, false, true };
    static const bool read_a5_script[] =
        { true, true, false, true, false, false, true, false, true, true };
    SoftI2C_t bus;
    SoftI2C_LineOps_t ops;
    SoftI2C_Config_t config;
    SoftI2C_Status_t status;
    uint32_t failures;
    uint8_t value;

    failures = 0UL;
    Mock_Reset();
    ops = Mock_Ops();
    config = Mock_Config();
    TEST_CHECK(SoftI2C_Init(NULL, &ops, &config) ==
               SOFT_I2C_STATUS_INVALID_ARGUMENT);
    config.half_cycle_us = 0U;
    TEST_CHECK(SoftI2C_Init(&bus, &ops, &config) ==
               SOFT_I2C_STATUS_INVALID_ARGUMENT);

    Mock_Reset();
    TEST_CHECK(Mock_Init(&bus) == SOFT_I2C_STATUS_OK);
    TEST_CHECK(SoftI2C_IsInitialized(&bus));
    Mock_SetSdaScript(address_ack_script, 2U);
    TEST_CHECK(SoftI2C_Start(&bus) == SOFT_I2C_STATUS_OK);
    TEST_CHECK(SoftI2C_WriteAddress(&bus, 0x10U) == SOFT_I2C_STATUS_OK);
    TEST_CHECK(SoftI2C_Stop(&bus) == SOFT_I2C_STATUS_OK);

    Mock_Reset();
    TEST_CHECK(Mock_Init(&bus) == SOFT_I2C_STATUS_OK);
    Mock_SetSdaScript(address_nack_script, 2U);
    TEST_CHECK(SoftI2C_Start(&bus) == SOFT_I2C_STATUS_OK);
    TEST_CHECK(SoftI2C_WriteAddress(&bus, 0x10U) ==
               SOFT_I2C_STATUS_NACK_ADDRESS);
    TEST_CHECK(SoftI2C_Stop(&bus) == SOFT_I2C_STATUS_OK);

    Mock_Reset();
    TEST_CHECK(Mock_Init(&bus) == SOFT_I2C_STATUS_OK);
    Mock_SetSdaScript(data_ack_script, 3U);
    TEST_CHECK(SoftI2C_Start(&bus) == SOFT_I2C_STATUS_OK);
    TEST_CHECK(SoftI2C_WriteAddress(&bus, 0x10U) == SOFT_I2C_STATUS_OK);
    TEST_CHECK(SoftI2C_WriteByte(&bus, 0x55U) == SOFT_I2C_STATUS_OK);
    TEST_CHECK(SoftI2C_Stop(&bus) == SOFT_I2C_STATUS_OK);

    Mock_Reset();
    TEST_CHECK(Mock_Init(&bus) == SOFT_I2C_STATUS_OK);
    Mock_SetSdaScript(data_nack_script, 3U);
    TEST_CHECK(SoftI2C_Start(&bus) == SOFT_I2C_STATUS_OK);
    TEST_CHECK(SoftI2C_WriteAddress(&bus, 0x10U) == SOFT_I2C_STATUS_OK);
    TEST_CHECK(SoftI2C_WriteByte(&bus, 0x55U) == SOFT_I2C_STATUS_NACK_DATA);
    TEST_CHECK(SoftI2C_Stop(&bus) == SOFT_I2C_STATUS_OK);

    Mock_Reset();
    TEST_CHECK(Mock_Init(&bus) == SOFT_I2C_STATUS_OK);
    Mock_SetSdaScript(read_a5_script, 10U);
    TEST_CHECK(SoftI2C_Start(&bus) == SOFT_I2C_STATUS_OK);
    TEST_CHECK(SoftI2C_ReadByteBegin(&bus, &value) == SOFT_I2C_STATUS_OK);
    TEST_CHECK(value == 0xA5U);
    TEST_CHECK(SoftI2C_SendReadResponse(&bus, SOFT_I2C_MASTER_ACK) ==
               SOFT_I2C_STATUS_OK);
    TEST_CHECK(s_mock.sda_low_at_last_scl_release);
    TEST_CHECK(SoftI2C_Stop(&bus) == SOFT_I2C_STATUS_OK);

    Mock_Reset();
    TEST_CHECK(Mock_Init(&bus) == SOFT_I2C_STATUS_OK);
    Mock_SetSdaScript(read_a5_script, 10U);
    TEST_CHECK(SoftI2C_Start(&bus) == SOFT_I2C_STATUS_OK);
    TEST_CHECK(SoftI2C_ReadByteBegin(&bus, &value) == SOFT_I2C_STATUS_OK);
    TEST_CHECK(SoftI2C_SendReadResponse(&bus, SOFT_I2C_MASTER_NACK) ==
               SOFT_I2C_STATUS_OK);
    TEST_CHECK(!s_mock.sda_low_at_last_scl_release);
    TEST_CHECK(SoftI2C_Stop(&bus) == SOFT_I2C_STATUS_OK);

    Mock_Reset();
    TEST_CHECK(Mock_Init(&bus) == SOFT_I2C_STATUS_OK);
    s_mock.stretch_next_release_reads = 3U;
    TEST_CHECK(SoftI2C_Start(&bus) == SOFT_I2C_STATUS_OK);
    TEST_CHECK(SoftI2C_RepeatedStart(&bus) == SOFT_I2C_STATUS_OK);
    TEST_CHECK(SoftI2C_Stop(&bus) == SOFT_I2C_STATUS_OK);

    Mock_Reset();
    s_mock.scl_forced_low = true;
    TEST_CHECK(Mock_Init(&bus) == SOFT_I2C_STATUS_SCL_STUCK_LOW);

    Mock_Reset();
    s_mock.sda_forced_low = true;
    s_mock.release_sda_after_edges = 3UL;
    TEST_CHECK(Mock_Init(&bus) == SOFT_I2C_STATUS_SDA_STUCK_LOW);
    TEST_CHECK(SoftI2C_RecoverBus(&bus) == SOFT_I2C_STATUS_OK);
    TEST_CHECK(s_mock.scl_rising_edges == 3UL);

    Mock_Reset();
    s_mock.sda_forced_low = true;
    TEST_CHECK(Mock_Init(&bus) == SOFT_I2C_STATUS_SDA_STUCK_LOW);
    TEST_CHECK(SoftI2C_RecoverBus(&bus) == SOFT_I2C_STATUS_RECOVERY_FAILED);
    TEST_CHECK(s_mock.scl_rising_edges == 9UL);

    Mock_Reset();
    TEST_CHECK(Mock_Init(&bus) == SOFT_I2C_STATUS_OK);
    s_mock.delay_ok = false;
    status = SoftI2C_Start(&bus);
    TEST_CHECK(status == SOFT_I2C_STATUS_TIMEOUT);

    Mock_Reset();
    TEST_CHECK(Mock_Init(&bus) == SOFT_I2C_STATUS_OK);
    TEST_CHECK(SoftI2C_Start(&bus) == SOFT_I2C_STATUS_OK);
    s_mock.delay_ok = false;
    TEST_CHECK(SoftI2C_Stop(&bus) == SOFT_I2C_STATUS_TIMEOUT);
    TEST_CHECK(!s_mock.scl_master_low && !s_mock.sda_master_low);
    TEST_CHECK(!bus.started && !bus.read_response_pending);

    Mock_Reset();
    TEST_CHECK(Mock_Init(&bus) == SOFT_I2C_STATUS_OK);
    s_mock.sda_forced_low = true;
    s_mock.delay_ok = false;
    TEST_CHECK(SoftI2C_RecoverBus(&bus) == SOFT_I2C_STATUS_TIMEOUT);
    TEST_CHECK(!s_mock.scl_master_low && !s_mock.sda_master_low);
    TEST_CHECK(!bus.started && !bus.read_response_pending);

    TEST_CHECK(BSP_TIME_DELTA_US16(0x0003U, 0xFFFEU) == 5U);
    TEST_CHECK(BSP_TIME_DELTA_US16(105U, 100U) == 5U);
    return failures;
}
