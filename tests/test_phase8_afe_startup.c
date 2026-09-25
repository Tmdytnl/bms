#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "fml_afe_startup.h"
#include "fml_config.h"
#include "bsp_bq76940_regs.h"

#define TEST_AFE_MAX_WRITES              (32U)
#define TEST_AFE_MAX_SYS_STAT_READS       (8U)
#define TEST_AFE_NO_REGISTER              ((uint8_t)0xFFU)

typedef struct
{
    uint8_t address;
    uint8_t value;
} TestAfeWrite_t;

typedef struct
{
    uint8_t registers[256];
    TestAfeWrite_t writes[TEST_AFE_MAX_WRITES];
    BQ76940_Status_t sys_stat_status[TEST_AFE_MAX_SYS_STAT_READS];
    uint8_t sys_stat_value[TEST_AFE_MAX_SYS_STAT_READS];
    uint32_t operation_count;
    uint32_t wake_count;
    uint32_t read_count;
    uint32_t write_count;
    uint32_t decode_count;
    uint32_t sys_stat_script_count;
    uint32_t sys_stat_read_count;
    uint32_t wake_failures_remaining;
    uint32_t write_fault_match_target;
    uint32_t write_fault_match_count;
    uint32_t mismatch_match_target;
    uint32_t mismatch_match_count;
    uint8_t write_fault_address;
    BQ76940_Status_t write_fault_status;
    uint8_t mismatch_address;
    BQ76940_Status_t decode_status;
    bool write_fault_enabled;
    bool write_fault_consumed;
    bool mismatch_enabled;
    bool mismatch_consumed;
    bool reset_on_xready_clear;
    bool decode_input_mismatch;
} TestAfeFake_t;

static TestAfeFake_t s_test_afe_fake;

volatile uint32_t g_phase8_afe_startup_test_failures;
volatile uint32_t g_phase8_afe_startup_test_completed;

#define TEST_AFE_CHECK(condition)                    \
    do                                               \
    {                                                \
        if (!(condition))                            \
        {                                            \
            ++g_phase8_afe_startup_test_failures;    \
        }                                            \
    } while (0)

static void TestAfe_ResetFake(void)
{
    (void)memset(&s_test_afe_fake, 0, sizeof(s_test_afe_fake));
    s_test_afe_fake.registers[BQ76940_REG_SYS_CTRL2] = 0x03U;
    s_test_afe_fake.registers[BQ76940_REG_CELLBAL1] = 0x1FU;
    s_test_afe_fake.registers[BQ76940_REG_CELLBAL2] = 0x1FU;
    s_test_afe_fake.registers[BQ76940_REG_CELLBAL3] = 0x1FU;
    s_test_afe_fake.registers[BQ76940_REG_ADCGAIN1] = 0x08U;
    s_test_afe_fake.registers[BQ76940_REG_ADCOFFSET] = 0x00U;
    s_test_afe_fake.registers[BQ76940_REG_ADCGAIN2] = 0xA0U;
    s_test_afe_fake.write_fault_address = TEST_AFE_NO_REGISTER;
    s_test_afe_fake.write_fault_match_target = 1U;
    s_test_afe_fake.mismatch_address = TEST_AFE_NO_REGISTER;
    s_test_afe_fake.mismatch_match_target = 1U;
    s_test_afe_fake.decode_status = BQ76940_STATUS_OK;
}

static void TestAfe_AddSysStat(BQ76940_Status_t status, uint8_t value)
{
    uint32_t index;

    index = s_test_afe_fake.sys_stat_script_count;
    if (index >= TEST_AFE_MAX_SYS_STAT_READS)
    {
        ++g_phase8_afe_startup_test_failures;
        return;
    }
    s_test_afe_fake.sys_stat_status[index] = status;
    s_test_afe_fake.sys_stat_value[index] = value;
    s_test_afe_fake.sys_stat_script_count = index + 1U;
}

static BMS_AfeStartupConfig_t TestAfe_ValidConfig(void)
{
    BMS_AfeStartupConfig_t config;

    (void)memset(&config, 0, sizeof(config));
    config.ov_uv_trip_present = true;
    config.ov_trip_mv = 4250U;
    config.uv_trip_mv = 2800U;
    config.protect1_present = true;
    config.protect1_rsns = true;
    config.protect1_scd_delay_code = 1U;
    config.protect1_scd_threshold_code = 3U;
    config.protect2_present = true;
    config.protect2_ocd_delay_code = 5U;
    config.protect2_ocd_threshold_code = 10U;
    config.protect3_present = true;
    config.protect3_uv_delay_code = 1U;
    config.protect3_ov_delay_code = 1U;
    return config;
}

static void TestAfe_PrepareDevice(BQ76940_t *device)
{
    (void)memset(device, 0, sizeof(*device));
    device->initialized = true;
}

bool BSP_BQ76940_IsInitialized(const BQ76940_t *device)
{
    return (device != NULL) && device->initialized;
}

BQ76940_Status_t BSP_BQ76940_WriteByte(BQ76940_t *device,
                                    uint8_t register_address,
                                    uint8_t value)
{
    uint32_t trace_index;

    if ((device == NULL) || !device->initialized)
    {
        return BQ76940_STATUS_NOT_INITIALIZED;
    }

    ++s_test_afe_fake.operation_count;
    trace_index = s_test_afe_fake.write_count;
    if (trace_index < TEST_AFE_MAX_WRITES)
    {
        s_test_afe_fake.writes[trace_index].address = register_address;
        s_test_afe_fake.writes[trace_index].value = value;
    }
    else
    {
        ++g_phase8_afe_startup_test_failures;
    }
    ++s_test_afe_fake.write_count;

    if (s_test_afe_fake.write_fault_enabled &&
        !s_test_afe_fake.write_fault_consumed &&
        (register_address == s_test_afe_fake.write_fault_address))
    {
        ++s_test_afe_fake.write_fault_match_count;
        if (s_test_afe_fake.write_fault_match_count ==
            s_test_afe_fake.write_fault_match_target)
        {
            s_test_afe_fake.write_fault_consumed = true;
            return s_test_afe_fake.write_fault_status;
        }
    }

    if (register_address == BQ76940_REG_SYS_STAT)
    {
        s_test_afe_fake.registers[register_address] =
            (uint8_t)(s_test_afe_fake.registers[register_address] &
                      (uint8_t)(~value));
        if (s_test_afe_fake.reset_on_xready_clear &&
            ((value & BMS_AFE_STARTUP_STAT_DEVICE_XREADY) != 0U))
        {
            /* 模拟 reset epoch：所有 pre-clear configuration evidence 失效，
             * factory calibration register 保持。 */
            s_test_afe_fake.registers[BQ76940_REG_SYS_CTRL2] = 0x03U;
            s_test_afe_fake.registers[BQ76940_REG_CELLBAL1] = 0x1FU;
            s_test_afe_fake.registers[BQ76940_REG_CELLBAL2] = 0x1FU;
            s_test_afe_fake.registers[BQ76940_REG_CELLBAL3] = 0x1FU;
            s_test_afe_fake.registers[BQ76940_REG_CC_CFG] = 0U;
            s_test_afe_fake.registers[BQ76940_REG_OV_TRIP] = 0U;
            s_test_afe_fake.registers[BQ76940_REG_UV_TRIP] = 0U;
            s_test_afe_fake.registers[BQ76940_REG_PROTECT1] = 0U;
            s_test_afe_fake.registers[BQ76940_REG_PROTECT2] = 0U;
            s_test_afe_fake.registers[BQ76940_REG_PROTECT3] = 0U;
            s_test_afe_fake.registers[BQ76940_REG_SYS_CTRL1] = 0U;
        }
    }
    else
    {
        s_test_afe_fake.registers[register_address] = value;
    }
    return BQ76940_STATUS_OK;
}

BQ76940_Status_t BSP_BQ76940_ReadByte(BQ76940_t *device,
                                   uint8_t register_address,
                                   uint8_t *value)
{
    uint32_t script_index;
    BQ76940_Status_t status;

    if ((device == NULL) || !device->initialized)
    {
        return BQ76940_STATUS_NOT_INITIALIZED;
    }
    if (value == NULL)
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }

    ++s_test_afe_fake.operation_count;
    ++s_test_afe_fake.read_count;
    if (register_address == BQ76940_REG_SYS_STAT)
    {
        script_index = s_test_afe_fake.sys_stat_read_count;
        ++s_test_afe_fake.sys_stat_read_count;
        if (script_index < s_test_afe_fake.sys_stat_script_count)
        {
            status = s_test_afe_fake.sys_stat_status[script_index];
            if (status == BQ76940_STATUS_OK)
            {
                *value = s_test_afe_fake.sys_stat_value[script_index];
            }
            return status;
        }
    }

    *value = s_test_afe_fake.registers[register_address];
    if (s_test_afe_fake.mismatch_enabled &&
        !s_test_afe_fake.mismatch_consumed &&
        (register_address == s_test_afe_fake.mismatch_address))
    {
        ++s_test_afe_fake.mismatch_match_count;
        if (s_test_afe_fake.mismatch_match_count ==
            s_test_afe_fake.mismatch_match_target)
        {
            s_test_afe_fake.mismatch_consumed = true;
            *value ^= 0x01U;
            s_test_afe_fake.registers[register_address] = *value;
        }
    }
    return BQ76940_STATUS_OK;
}

BQ76940_Status_t BSP_BQ76940_DecodeCalibration(
    uint8_t adc_gain1,
    uint8_t adc_offset,
    uint8_t adc_gain2,
    BQ76940_Calibration_t *calibration)
{
    ++s_test_afe_fake.decode_count;
    if ((adc_gain1 != 0x08U) || (adc_offset != 0x00U) ||
        (adc_gain2 != 0xA0U))
    {
        s_test_afe_fake.decode_input_mismatch = true;
    }
    if (calibration == NULL)
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    if (s_test_afe_fake.decode_status != BQ76940_STATUS_OK)
    {
        return s_test_afe_fake.decode_status;
    }

    calibration->gain_uv_per_lsb = 386U;
    calibration->offset_mv = 0;
    calibration->valid = true;
    return BQ76940_STATUS_OK;
}

static bool TestAfe_Wake(void *context)
{
    (void)context;
    ++s_test_afe_fake.operation_count;
    ++s_test_afe_fake.wake_count;
    if (s_test_afe_fake.wake_failures_remaining > 0U)
    {
        --s_test_afe_fake.wake_failures_remaining;
        return false;
    }
    return true;
}

static BMS_AfeStartupResult_t TestAfe_StepChecked(
    BMS_AfeStartup_t *startup,
    uint32_t now_ms)
{
    uint32_t before;
    BMS_AfeStartupResult_t result;

    before = s_test_afe_fake.operation_count;
    result = FML_AfeStartup_Step(startup, now_ms);
    TEST_AFE_CHECK((s_test_afe_fake.operation_count - before) <= 1U);
    return result;
}

static BMS_AfeStartupResult_t TestAfe_DriveTerminal(
    BMS_AfeStartup_t *startup,
    uint32_t *now_ms)
{
    uint32_t iteration;
    BMS_AfeStartupResult_t result;

    result = BMS_AFE_STARTUP_RESULT_PENDING;
    for (iteration = 0U; iteration < 128U; ++iteration)
    {
        result = TestAfe_StepChecked(startup, *now_ms);
        if (result != BMS_AFE_STARTUP_RESULT_PENDING)
        {
            return result;
        }
        if (startup->state == BMS_AFE_STARTUP_STATE_WAIT_WAKE_SETTLE)
        {
            *now_ms = startup->wait_started_ms +
                      (uint32_t)BMS_AFE_WAKE_SETTLE_MS;
        }
        else if (startup->state ==
                 BMS_AFE_STARTUP_STATE_WAIT_INITIAL_DATA)
        {
            *now_ms = startup->wait_started_ms +
                      (uint32_t)BMS_AFE_INITIAL_DATA_SETTLE_MS;
        }
    }
    TEST_AFE_CHECK(false);
    return result;
}

static bool TestAfe_DriveToInitialWait(BMS_AfeStartup_t *startup,
                                        uint32_t *now_ms)
{
    uint32_t iteration;
    BMS_AfeStartupResult_t result;

    for (iteration = 0U; iteration < 96U; ++iteration)
    {
        if (startup->state == BMS_AFE_STARTUP_STATE_WAIT_INITIAL_DATA)
        {
            return true;
        }
        result = TestAfe_StepChecked(startup, *now_ms);
        if (result != BMS_AFE_STARTUP_RESULT_PENDING)
        {
            return false;
        }
        if (startup->state == BMS_AFE_STARTUP_STATE_WAIT_WAKE_SETTLE)
        {
            *now_ms = startup->wait_started_ms +
                      (uint32_t)BMS_AFE_WAKE_SETTLE_MS;
        }
    }
    return false;
}

static bool TestAfe_DriveToFinalCtrlWrite(BMS_AfeStartup_t *startup,
                                           uint32_t *now_ms)
{
    uint32_t iteration;
    BMS_AfeStartupResult_t result;

    for (iteration = 0U; iteration < 96U; ++iteration)
    {
        if ((startup->state == BMS_AFE_STARTUP_STATE_WRITE_REGISTER) &&
            (startup->register_index ==
             (BMS_AFE_STARTUP_REGISTER_COUNT - 1U)))
        {
            return true;
        }
        result = TestAfe_StepChecked(startup, *now_ms);
        if (result != BMS_AFE_STARTUP_RESULT_PENDING)
        {
            return false;
        }
        if (startup->state == BMS_AFE_STARTUP_STATE_WAIT_WAKE_SETTLE)
        {
            *now_ms = startup->wait_started_ms +
                      (uint32_t)BMS_AFE_WAKE_SETTLE_MS;
        }
    }
    return false;
}

static bool TestAfe_Start(BMS_AfeStartup_t *startup,
                          BQ76940_t *device,
                          const BMS_AfeStartupConfig_t *config)
{
    TestAfe_PrepareDevice(device);
    return FML_AfeStartup_Init(startup, device, config,
                               TestAfe_Wake, NULL);
}

static void TestAfe_RejectsIncompleteConfig(void)
{
    BMS_AfeStartup_t startup;
    BQ76940_t device;
    BMS_AfeStartupConfig_t config;

    TestAfe_ResetFake();
    TestAfe_PrepareDevice(&device);
    config = TestAfe_ValidConfig();
    config.protect3_present = false;
    TEST_AFE_CHECK(!FML_AfeStartup_Init(&startup, &device, &config,
                                        TestAfe_Wake, NULL));
    TEST_AFE_CHECK(startup.failure ==
                   BMS_AFE_STARTUP_FAILURE_INVALID_CONFIG);
    TEST_AFE_CHECK(s_test_afe_fake.operation_count == 0U);

    config = TestAfe_ValidConfig();
    config.protect3_uv_delay_code = 4U;
    TEST_AFE_CHECK(!FML_AfeStartup_Init(&startup, &device, &config,
                                        TestAfe_Wake, NULL));
    TEST_AFE_CHECK(s_test_afe_fake.operation_count == 0U);

    config = TestAfe_ValidConfig();
    config.uv_trip_mv = config.ov_trip_mv;
    TEST_AFE_CHECK(!FML_AfeStartup_Init(&startup, &device, &config,
                                        TestAfe_Wake, NULL));
    TEST_AFE_CHECK(s_test_afe_fake.operation_count == 0U);

    config = TestAfe_ValidConfig();
    device.initialized = false;
    TEST_AFE_CHECK(!FML_AfeStartup_Init(&startup, &device, &config,
                                        TestAfe_Wake, NULL));
    TEST_AFE_CHECK(startup.failure ==
                   BMS_AFE_STARTUP_FAILURE_DEVICE_NOT_READY);
}

static void TestAfe_BoundsWakeAndProbe(void)
{
    BMS_AfeStartup_t startup;
    BQ76940_t device;
    BMS_AfeStartupConfig_t config;
    BMS_AfeStartupResult_t result;
    uint32_t now_ms;

    TestAfe_ResetFake();
    config = TestAfe_ValidConfig();
    s_test_afe_fake.wake_failures_remaining =
        BMS_AFE_STARTUP_MAX_PROBE_ATTEMPTS;
    TEST_AFE_CHECK(TestAfe_Start(&startup, &device, &config));
    now_ms = 0U;
    result = TestAfe_DriveTerminal(&startup, &now_ms);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_FAILED);
    TEST_AFE_CHECK(startup.failure == BMS_AFE_STARTUP_FAILURE_WAKE);
    TEST_AFE_CHECK(s_test_afe_fake.wake_count ==
                   BMS_AFE_STARTUP_MAX_PROBE_ATTEMPTS);
    TEST_AFE_CHECK(s_test_afe_fake.read_count == 0U);
    TEST_AFE_CHECK(s_test_afe_fake.write_count == 0U);

    TestAfe_ResetFake();
    TestAfe_AddSysStat(BQ76940_STATUS_I2C_TIMEOUT, 0U);
    TestAfe_AddSysStat(BQ76940_STATUS_I2C_NACK, 0U);
    TestAfe_AddSysStat(BQ76940_STATUS_I2C_TIMEOUT, 0U);
    TEST_AFE_CHECK(TestAfe_Start(&startup, &device, &config));
    now_ms = 0U;
    result = TestAfe_DriveTerminal(&startup, &now_ms);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_FAILED);
    TEST_AFE_CHECK(startup.failure == BMS_AFE_STARTUP_FAILURE_PROBE);
    TEST_AFE_CHECK(s_test_afe_fake.wake_count ==
                   BMS_AFE_STARTUP_MAX_PROBE_ATTEMPTS);
    TEST_AFE_CHECK(s_test_afe_fake.sys_stat_read_count ==
                   BMS_AFE_STARTUP_MAX_PROBE_ATTEMPTS);
    TEST_AFE_CHECK(s_test_afe_fake.write_count == 0U);
}

static void TestAfe_UnsafeProbeConfirmsAllSafeOutputs(void)
{
    BMS_AfeStartup_t startup;
    BQ76940_t device;
    BMS_AfeStartupConfig_t config;
    BMS_AfeStartupResult_t result;
    uint32_t now_ms;
    uint8_t unsafe_status;

    TestAfe_ResetFake();
    unsafe_status = (uint8_t)(BMS_AFE_STARTUP_STAT_CC_READY | 0x14U);
    TestAfe_AddSysStat(BQ76940_STATUS_OK, unsafe_status);
    config = TestAfe_ValidConfig();
    TEST_AFE_CHECK(TestAfe_Start(&startup, &device, &config));
    now_ms = 0U;
    result = TestAfe_DriveTerminal(&startup, &now_ms);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_FAILED);
    TEST_AFE_CHECK(startup.failure ==
                   BMS_AFE_STARTUP_FAILURE_UNSAFE_STATUS);
    TEST_AFE_CHECK(startup.unsafe_sys_stat == unsafe_status);
    TEST_AFE_CHECK(FML_AfeStartup_IsFetOffConfirmed(&startup));
    TEST_AFE_CHECK(startup.safe_outputs_confirmed);
    TEST_AFE_CHECK(s_test_afe_fake.write_count == 4U);
    TEST_AFE_CHECK(s_test_afe_fake.writes[0].address ==
                   BQ76940_REG_SYS_CTRL2);
    TEST_AFE_CHECK(s_test_afe_fake.writes[0].value == 0U);
    TEST_AFE_CHECK(s_test_afe_fake.writes[1].address ==
                   BQ76940_REG_CELLBAL1);
    TEST_AFE_CHECK(s_test_afe_fake.writes[2].address ==
                   BQ76940_REG_CELLBAL2);
    TEST_AFE_CHECK(s_test_afe_fake.writes[3].address ==
                   BQ76940_REG_CELLBAL3);
    TEST_AFE_CHECK(s_test_afe_fake.writes[1].value == 0U);
    TEST_AFE_CHECK(s_test_afe_fake.writes[2].value == 0U);
    TEST_AFE_CHECK(s_test_afe_fake.writes[3].value == 0U);
    TEST_AFE_CHECK(s_test_afe_fake.sys_stat_read_count == 1U);
    TEST_AFE_CHECK(s_test_afe_fake.read_count == 5U);
    TEST_AFE_CHECK(s_test_afe_fake.registers[BQ76940_REG_SYS_CTRL2] == 0U);
    TEST_AFE_CHECK(s_test_afe_fake.registers[BQ76940_REG_CELLBAL1] == 0U);
    TEST_AFE_CHECK(s_test_afe_fake.registers[BQ76940_REG_CELLBAL2] == 0U);
    TEST_AFE_CHECK(s_test_afe_fake.registers[BQ76940_REG_CELLBAL3] == 0U);
}

static void TestAfe_SuccessSequenceAndSettle(void)
{
    static const uint8_t expected_addresses[BMS_AFE_STARTUP_REGISTER_COUNT] =
    {
        BQ76940_REG_SYS_CTRL2,
        BQ76940_REG_CELLBAL1,
        BQ76940_REG_CELLBAL2,
        BQ76940_REG_CELLBAL3,
        BQ76940_REG_CC_CFG,
        BQ76940_REG_OV_TRIP,
        BQ76940_REG_UV_TRIP,
        BQ76940_REG_PROTECT3,
        BQ76940_REG_PROTECT1,
        BQ76940_REG_PROTECT2,
        BQ76940_REG_SYS_CTRL1,
        BQ76940_REG_SYS_CTRL2
    };
    static const uint8_t expected_values[BMS_AFE_STARTUP_REGISTER_COUNT] =
    {
        0x00U, 0x00U, 0x00U, 0x00U, 0x19U, 0xB0U,
        0xC5U, 0x50U, 0x8BU, 0x5AU, 0x18U, 0x40U
    };
    BMS_AfeStartup_t startup;
    BQ76940_t device;
    BMS_AfeStartupConfig_t config;
    BQ76940_Calibration_t calibration;
    BMS_AfeStartupResult_t result;
    uint32_t now_ms;
    uint32_t before;
    uint32_t index;

    TestAfe_ResetFake();
    TestAfe_AddSysStat(BQ76940_STATUS_OK, 0U);
    TestAfe_AddSysStat(BQ76940_STATUS_OK,
                       BMS_AFE_STARTUP_STAT_CC_READY);
    config = TestAfe_ValidConfig();
    TEST_AFE_CHECK(TestAfe_Start(&startup, &device, &config));
    now_ms = 0U;
    TEST_AFE_CHECK(TestAfe_DriveToInitialWait(&startup, &now_ms));
    TEST_AFE_CHECK(!FML_AfeStartup_GetCalibration(&startup, &calibration));
    TEST_AFE_CHECK(s_test_afe_fake.write_count ==
                   BMS_AFE_STARTUP_REGISTER_COUNT);

    before = s_test_afe_fake.operation_count;
    result = TestAfe_StepChecked(
        &startup,
        startup.wait_started_ms +
        (uint32_t)BMS_AFE_INITIAL_DATA_SETTLE_MS - 1U);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_PENDING);
    TEST_AFE_CHECK(startup.state ==
                   BMS_AFE_STARTUP_STATE_WAIT_INITIAL_DATA);
    TEST_AFE_CHECK(s_test_afe_fake.operation_count == before);

    result = TestAfe_StepChecked(
        &startup,
        startup.wait_started_ms +
        (uint32_t)BMS_AFE_INITIAL_DATA_SETTLE_MS);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_PENDING);
    TEST_AFE_CHECK(startup.state ==
                   BMS_AFE_STARTUP_STATE_READ_FINAL_STATUS);
    TEST_AFE_CHECK(s_test_afe_fake.operation_count == before);

    result = TestAfe_StepChecked(
        &startup,
        startup.wait_started_ms +
        (uint32_t)BMS_AFE_INITIAL_DATA_SETTLE_MS);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_COMPLETE);
    TEST_AFE_CHECK(startup.state == BMS_AFE_STARTUP_STATE_COMPLETE);
    TEST_AFE_CHECK(startup.final_sys_stat ==
                   BMS_AFE_STARTUP_STAT_CC_READY);
    TEST_AFE_CHECK(s_test_afe_fake.write_count ==
                   BMS_AFE_STARTUP_REGISTER_COUNT);
    TEST_AFE_CHECK(FML_AfeStartup_IsFetOffConfirmed(&startup));
    TEST_AFE_CHECK(FML_AfeStartup_GetCalibration(&startup, &calibration));
    TEST_AFE_CHECK(calibration.valid);
    TEST_AFE_CHECK(calibration.gain_uv_per_lsb == 386U);
    TEST_AFE_CHECK(s_test_afe_fake.decode_count == 1U);
    TEST_AFE_CHECK(!s_test_afe_fake.decode_input_mismatch);

    for (index = 0U; index < BMS_AFE_STARTUP_REGISTER_COUNT; ++index)
    {
        TEST_AFE_CHECK(s_test_afe_fake.writes[index].address ==
                       expected_addresses[index]);
        TEST_AFE_CHECK(s_test_afe_fake.writes[index].value ==
                       expected_values[index]);
    }

    before = s_test_afe_fake.operation_count;
    result = TestAfe_StepChecked(&startup, now_ms);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_COMPLETE);
    TEST_AFE_CHECK(s_test_afe_fake.operation_count == before);
}

static void TestAfe_FinalFetReadbackUsesBoundedSafeOff(void)
{
    BMS_AfeStartup_t startup;
    BQ76940_t device;
    BMS_AfeStartupConfig_t config;
    BMS_AfeStartupResult_t result;
    uint32_t now_ms;

    TestAfe_ResetFake();
    TestAfe_AddSysStat(BQ76940_STATUS_OK, 0U);
    TestAfe_AddSysStat(BQ76940_STATUS_OK, 0U);
    s_test_afe_fake.mismatch_enabled = true;
    s_test_afe_fake.mismatch_address = BQ76940_REG_SYS_CTRL2;
    s_test_afe_fake.mismatch_match_target = 2U;
    config = TestAfe_ValidConfig();
    TEST_AFE_CHECK(TestAfe_Start(&startup, &device, &config));
    now_ms = 0U;
    TEST_AFE_CHECK(TestAfe_DriveToFinalCtrlWrite(&startup, &now_ms));
    TEST_AFE_CHECK(FML_AfeStartup_IsFetOffConfirmed(&startup));

    result = TestAfe_StepChecked(&startup, now_ms);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_PENDING);
    TEST_AFE_CHECK(startup.state ==
                   BMS_AFE_STARTUP_STATE_VERIFY_REGISTER);
    TEST_AFE_CHECK(!FML_AfeStartup_IsFetOffConfirmed(&startup));
    TEST_AFE_CHECK(!startup.safe_outputs_confirmed);

    result = TestAfe_StepChecked(&startup, now_ms);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_PENDING);
    TEST_AFE_CHECK(startup.state ==
                   BMS_AFE_STARTUP_STATE_SAFE_OFF_WRITE);
    TEST_AFE_CHECK(startup.failed_readback_value == 0x41U);
    TEST_AFE_CHECK(!FML_AfeStartup_IsFetOffConfirmed(&startup));
    TEST_AFE_CHECK(!startup.safe_outputs_confirmed);
    TEST_AFE_CHECK(startup.safe_off_recovery_attempted);

    result = TestAfe_StepChecked(&startup, now_ms);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_PENDING);
    TEST_AFE_CHECK(startup.state ==
                   BMS_AFE_STARTUP_STATE_SAFE_OFF_VERIFY);
    TEST_AFE_CHECK(!FML_AfeStartup_IsFetOffConfirmed(&startup));

    result = TestAfe_StepChecked(&startup, now_ms);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_PENDING);
    TEST_AFE_CHECK(startup.state ==
                   BMS_AFE_STARTUP_STATE_WAIT_INITIAL_DATA);
    TEST_AFE_CHECK(FML_AfeStartup_IsFetOffConfirmed(&startup));
    TEST_AFE_CHECK(startup.safe_outputs_confirmed);
    TEST_AFE_CHECK(startup.safe_off_readback_value == 0x40U);
    TEST_AFE_CHECK(s_test_afe_fake.write_count ==
                   (BMS_AFE_STARTUP_REGISTER_COUNT + 1U));
    TEST_AFE_CHECK(s_test_afe_fake.writes[
                       BMS_AFE_STARTUP_REGISTER_COUNT].address ==
                   BQ76940_REG_SYS_CTRL2);
    TEST_AFE_CHECK(s_test_afe_fake.writes[
                       BMS_AFE_STARTUP_REGISTER_COUNT].value == 0x40U);

    result = TestAfe_DriveTerminal(&startup, &now_ms);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_COMPLETE);
    TEST_AFE_CHECK(FML_AfeStartup_IsFetOffConfirmed(&startup));
}

static void TestAfe_SafeOffFailureModesAreTerminal(void)
{
    BMS_AfeStartup_t startup;
    BQ76940_t device;
    BMS_AfeStartupConfig_t config;
    BMS_AfeStartupResult_t result;
    uint32_t now_ms;
    uint32_t before;

    config = TestAfe_ValidConfig();

    TestAfe_ResetFake();
    TestAfe_AddSysStat(BQ76940_STATUS_OK, 0U);
    s_test_afe_fake.mismatch_enabled = true;
    s_test_afe_fake.mismatch_address = BQ76940_REG_SYS_CTRL2;
    s_test_afe_fake.mismatch_match_target = 2U;
    s_test_afe_fake.write_fault_enabled = true;
    s_test_afe_fake.write_fault_address = BQ76940_REG_SYS_CTRL2;
    s_test_afe_fake.write_fault_match_target = 3U;
    s_test_afe_fake.write_fault_status =
        BQ76940_STATUS_WRITE_FINALIZATION_AMBIGUOUS;
    TEST_AFE_CHECK(TestAfe_Start(&startup, &device, &config));
    now_ms = 0U;
    result = TestAfe_DriveTerminal(&startup, &now_ms);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_FAILED);
    TEST_AFE_CHECK(startup.failure ==
                   BMS_AFE_STARTUP_FAILURE_WRITE_FINALIZATION_AMBIGUOUS);
    TEST_AFE_CHECK(!FML_AfeStartup_IsFetOffConfirmed(&startup));
    before = s_test_afe_fake.operation_count;
    result = TestAfe_StepChecked(&startup, now_ms);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_FAILED);
    TEST_AFE_CHECK(s_test_afe_fake.operation_count == before);

    TestAfe_ResetFake();
    TestAfe_AddSysStat(BQ76940_STATUS_OK, 0U);
    s_test_afe_fake.mismatch_enabled = true;
    s_test_afe_fake.mismatch_address = BQ76940_REG_SYS_CTRL2;
    s_test_afe_fake.mismatch_match_target = 2U;
    s_test_afe_fake.write_fault_enabled = true;
    s_test_afe_fake.write_fault_address = BQ76940_REG_SYS_CTRL2;
    s_test_afe_fake.write_fault_match_target = 3U;
    s_test_afe_fake.write_fault_status = BQ76940_STATUS_I2C_TIMEOUT;
    TEST_AFE_CHECK(TestAfe_Start(&startup, &device, &config));
    now_ms = 0U;
    result = TestAfe_DriveTerminal(&startup, &now_ms);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_FAILED);
    TEST_AFE_CHECK(startup.failure == BMS_AFE_STARTUP_FAILURE_TRANSPORT);
    TEST_AFE_CHECK(!FML_AfeStartup_IsFetOffConfirmed(&startup));

    TestAfe_ResetFake();
    TestAfe_AddSysStat(BQ76940_STATUS_OK, 0U);
    s_test_afe_fake.mismatch_enabled = true;
    s_test_afe_fake.mismatch_address = BQ76940_REG_SYS_CTRL2;
    s_test_afe_fake.mismatch_match_target = 2U;
    s_test_afe_fake.write_fault_enabled = true;
    s_test_afe_fake.write_fault_address = BQ76940_REG_SYS_CTRL2;
    s_test_afe_fake.write_fault_match_target = 3U;
    s_test_afe_fake.write_fault_status = BQ76940_STATUS_OK;
    TEST_AFE_CHECK(TestAfe_Start(&startup, &device, &config));
    now_ms = 0U;
    result = TestAfe_DriveTerminal(&startup, &now_ms);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_FAILED);
    TEST_AFE_CHECK(startup.failure ==
                   BMS_AFE_STARTUP_FAILURE_SAFE_OFF_UNCONFIRMED);
    TEST_AFE_CHECK(startup.safe_off_readback_value == 0x41U);
    TEST_AFE_CHECK(!FML_AfeStartup_IsFetOffConfirmed(&startup));
    TEST_AFE_CHECK(s_test_afe_fake.write_count ==
                   (BMS_AFE_STARTUP_REGISTER_COUNT + 1U));
}

static void TestAfe_InitialXreadyRetiredIsNotBlindCleared(void)
{
    BMS_AfeStartup_t startup;
    BQ76940_t device;
    BMS_AfeStartupConfig_t config;
    BMS_AfeStartupResult_t result;
    uint32_t now_ms;

    TestAfe_ResetFake();
    TestAfe_AddSysStat(BQ76940_STATUS_OK,
                       BMS_AFE_STARTUP_STAT_DEVICE_XREADY);
    TestAfe_AddSysStat(BQ76940_STATUS_OK, 0U);
    config = TestAfe_ValidConfig();
    TEST_AFE_CHECK(TestAfe_Start(&startup, &device, &config));
    now_ms = 0U;
    result = TestAfe_DriveTerminal(&startup, &now_ms);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_COMPLETE);
    TEST_AFE_CHECK((startup.initial_sys_stat &
                    BMS_AFE_STARTUP_STAT_DEVICE_XREADY) != 0U);
    TEST_AFE_CHECK((startup.final_sys_stat &
                    BMS_AFE_STARTUP_STAT_DEVICE_XREADY) == 0U);
    TEST_AFE_CHECK(!startup.xready_clear_required);
    TEST_AFE_CHECK(s_test_afe_fake.write_count ==
                   BMS_AFE_STARTUP_REGISTER_COUNT);
}

static void TestAfe_XreadyClearForcesFullReconfiguration(void)
{
    BMS_AfeStartup_t startup;
    BQ76940_t device;
    BMS_AfeStartupConfig_t config;
    BMS_AfeStartupResult_t result;
    uint32_t now_ms;
    uint32_t before;
    uint32_t index;

    TestAfe_ResetFake();
    s_test_afe_fake.reset_on_xready_clear = true;
    TestAfe_AddSysStat(BQ76940_STATUS_OK,
                       BMS_AFE_STARTUP_STAT_DEVICE_XREADY);
    TestAfe_AddSysStat(
        BQ76940_STATUS_OK,
        (uint8_t)(BMS_AFE_STARTUP_STAT_DEVICE_XREADY |
                  BMS_AFE_STARTUP_STAT_CC_READY));
    TestAfe_AddSysStat(BQ76940_STATUS_OK,
                       BMS_AFE_STARTUP_STAT_CC_READY);
    config = TestAfe_ValidConfig();
    TEST_AFE_CHECK(TestAfe_Start(&startup, &device, &config));
    now_ms = 0U;
    TEST_AFE_CHECK(TestAfe_DriveToInitialWait(&startup, &now_ms));
    TEST_AFE_CHECK(s_test_afe_fake.write_count ==
                   BMS_AFE_STARTUP_REGISTER_COUNT);

    result = TestAfe_StepChecked(
        &startup,
        startup.wait_started_ms +
        (uint32_t)BMS_AFE_INITIAL_DATA_SETTLE_MS);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_PENDING);
    TEST_AFE_CHECK(startup.state ==
                   BMS_AFE_STARTUP_STATE_READ_FINAL_STATUS);
    result = TestAfe_StepChecked(
        &startup,
        startup.wait_started_ms +
        (uint32_t)BMS_AFE_INITIAL_DATA_SETTLE_MS);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_PENDING);
    TEST_AFE_CHECK(startup.state == BMS_AFE_STARTUP_STATE_CLEAR_XREADY);
    TEST_AFE_CHECK(FML_AfeStartup_IsFetOffConfirmed(&startup));

    result = TestAfe_StepChecked(
        &startup,
        startup.wait_started_ms +
        (uint32_t)BMS_AFE_INITIAL_DATA_SETTLE_MS);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_PENDING);
    TEST_AFE_CHECK(startup.state == BMS_AFE_STARTUP_STATE_WRITE_REGISTER);
    TEST_AFE_CHECK(startup.register_index == 0U);
    TEST_AFE_CHECK(startup.xready_clear_attempted);
    TEST_AFE_CHECK(startup.xready_clear_completed);
    TEST_AFE_CHECK(!FML_AfeStartup_IsFetOffConfirmed(&startup));
    TEST_AFE_CHECK(!startup.safe_outputs_confirmed);
    TEST_AFE_CHECK(!startup.calibration.valid);
    TEST_AFE_CHECK(s_test_afe_fake.write_count ==
                   (BMS_AFE_STARTUP_REGISTER_COUNT + 1U));
    TEST_AFE_CHECK(s_test_afe_fake.writes[
                       BMS_AFE_STARTUP_REGISTER_COUNT].address ==
                   BQ76940_REG_SYS_STAT);
    TEST_AFE_CHECK(s_test_afe_fake.writes[
                       BMS_AFE_STARTUP_REGISTER_COUNT].value ==
                   BMS_AFE_STARTUP_STAT_DEVICE_XREADY);

    TEST_AFE_CHECK(TestAfe_DriveToInitialWait(&startup, &now_ms));
    TEST_AFE_CHECK(s_test_afe_fake.write_count ==
                   ((2U * BMS_AFE_STARTUP_REGISTER_COUNT) + 1U));
    TEST_AFE_CHECK(s_test_afe_fake.decode_count == 2U);
    TEST_AFE_CHECK(FML_AfeStartup_IsFetOffConfirmed(&startup));
    TEST_AFE_CHECK(startup.safe_outputs_confirmed);
    for (index = 0U; index < BMS_AFE_STARTUP_REGISTER_COUNT; ++index)
    {
        TEST_AFE_CHECK(
            s_test_afe_fake.writes[index].address ==
            s_test_afe_fake.writes[
                BMS_AFE_STARTUP_REGISTER_COUNT + 1U + index].address);
        TEST_AFE_CHECK(
            s_test_afe_fake.writes[index].value ==
            s_test_afe_fake.writes[
                BMS_AFE_STARTUP_REGISTER_COUNT + 1U + index].value);
    }

    before = s_test_afe_fake.operation_count;
    result = TestAfe_StepChecked(
        &startup,
        startup.wait_started_ms +
        (uint32_t)BMS_AFE_INITIAL_DATA_SETTLE_MS - 1U);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_PENDING);
    TEST_AFE_CHECK(startup.state ==
                   BMS_AFE_STARTUP_STATE_WAIT_INITIAL_DATA);
    TEST_AFE_CHECK(s_test_afe_fake.operation_count == before);
    result = TestAfe_StepChecked(
        &startup,
        startup.wait_started_ms +
        (uint32_t)BMS_AFE_INITIAL_DATA_SETTLE_MS);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_PENDING);
    TEST_AFE_CHECK(startup.state ==
                   BMS_AFE_STARTUP_STATE_READ_FINAL_STATUS);
    TEST_AFE_CHECK(s_test_afe_fake.operation_count == before);
    result = TestAfe_StepChecked(
        &startup,
        startup.wait_started_ms +
        (uint32_t)BMS_AFE_INITIAL_DATA_SETTLE_MS);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_COMPLETE);
    TEST_AFE_CHECK(startup.state == BMS_AFE_STARTUP_STATE_COMPLETE);
    TEST_AFE_CHECK(s_test_afe_fake.sys_stat_read_count == 3U);
    TEST_AFE_CHECK(startup.final_sys_stat ==
                   BMS_AFE_STARTUP_STAT_CC_READY);
    TEST_AFE_CHECK(s_test_afe_fake.registers[BQ76940_REG_CELLBAL1] == 0U);
    TEST_AFE_CHECK(s_test_afe_fake.registers[BQ76940_REG_CELLBAL2] == 0U);
    TEST_AFE_CHECK(s_test_afe_fake.registers[BQ76940_REG_CELLBAL3] == 0U);
    TEST_AFE_CHECK(s_test_afe_fake.registers[BQ76940_REG_CC_CFG] == 0x19U);
    TEST_AFE_CHECK(s_test_afe_fake.registers[BQ76940_REG_PROTECT1] == 0x8BU);
    TEST_AFE_CHECK(s_test_afe_fake.registers[BQ76940_REG_PROTECT2] == 0x5AU);
    TEST_AFE_CHECK(s_test_afe_fake.registers[BQ76940_REG_PROTECT3] == 0x50U);
    TEST_AFE_CHECK(s_test_afe_fake.registers[BQ76940_REG_SYS_CTRL1] == 0x18U);
    TEST_AFE_CHECK(s_test_afe_fake.registers[BQ76940_REG_SYS_CTRL2] == 0x40U);
}

static void TestAfe_XreadyAmbiguityIsTerminal(void)
{
    BMS_AfeStartup_t startup;
    BQ76940_t device;
    BMS_AfeStartupConfig_t config;
    BMS_AfeStartupResult_t result;
    uint32_t now_ms;
    uint32_t before;

    TestAfe_ResetFake();
    TestAfe_AddSysStat(BQ76940_STATUS_OK,
                       BMS_AFE_STARTUP_STAT_DEVICE_XREADY);
    TestAfe_AddSysStat(BQ76940_STATUS_OK,
                       BMS_AFE_STARTUP_STAT_DEVICE_XREADY);
    s_test_afe_fake.write_fault_enabled = true;
    s_test_afe_fake.write_fault_address = BQ76940_REG_SYS_STAT;
    s_test_afe_fake.write_fault_status =
        BQ76940_STATUS_WRITE_FINALIZATION_AMBIGUOUS;
    config = TestAfe_ValidConfig();
    TEST_AFE_CHECK(TestAfe_Start(&startup, &device, &config));
    now_ms = 0U;
    result = TestAfe_DriveTerminal(&startup, &now_ms);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_FAILED);
    TEST_AFE_CHECK(startup.failure ==
                   BMS_AFE_STARTUP_FAILURE_WRITE_FINALIZATION_AMBIGUOUS);
    TEST_AFE_CHECK(!FML_AfeStartup_IsFetOffConfirmed(&startup));
    TEST_AFE_CHECK(!startup.safe_outputs_confirmed);
    TEST_AFE_CHECK(!startup.calibration.valid);
    TEST_AFE_CHECK(s_test_afe_fake.sys_stat_read_count == 2U);
    TEST_AFE_CHECK(s_test_afe_fake.write_count ==
                   (BMS_AFE_STARTUP_REGISTER_COUNT + 1U));
    before = s_test_afe_fake.operation_count;
    result = TestAfe_StepChecked(&startup, now_ms);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_FAILED);
    TEST_AFE_CHECK(s_test_afe_fake.operation_count == before);
}

static void TestAfe_XreadyMustReadLowAfterClear(void)
{
    BMS_AfeStartup_t startup;
    BQ76940_t device;
    BMS_AfeStartupConfig_t config;
    BMS_AfeStartupResult_t result;
    uint32_t now_ms;

    TestAfe_ResetFake();
    TestAfe_AddSysStat(BQ76940_STATUS_OK,
                       BMS_AFE_STARTUP_STAT_DEVICE_XREADY);
    TestAfe_AddSysStat(BQ76940_STATUS_OK,
                       BMS_AFE_STARTUP_STAT_DEVICE_XREADY);
    TestAfe_AddSysStat(BQ76940_STATUS_OK,
                       BMS_AFE_STARTUP_STAT_DEVICE_XREADY);
    config = TestAfe_ValidConfig();
    TEST_AFE_CHECK(TestAfe_Start(&startup, &device, &config));
    now_ms = 0U;
    result = TestAfe_DriveTerminal(&startup, &now_ms);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_FAILED);
    TEST_AFE_CHECK(startup.failure ==
                   BMS_AFE_STARTUP_FAILURE_UNSAFE_STATUS);
    TEST_AFE_CHECK((startup.unsafe_sys_stat &
                    BMS_AFE_STARTUP_STAT_DEVICE_XREADY) != 0U);
    TEST_AFE_CHECK(startup.xready_clear_completed);
    TEST_AFE_CHECK(s_test_afe_fake.write_count ==
                   ((2U * BMS_AFE_STARTUP_REGISTER_COUNT) + 1U));
    TEST_AFE_CHECK(s_test_afe_fake.sys_stat_read_count == 3U);
}

static void TestAfe_NewFaultBlocksCompletion(void)
{
    BMS_AfeStartup_t startup;
    BQ76940_t device;
    BMS_AfeStartupConfig_t config;
    BMS_AfeStartupResult_t result;
    uint32_t now_ms;

    TestAfe_ResetFake();
    TestAfe_AddSysStat(BQ76940_STATUS_OK, 0U);
    TestAfe_AddSysStat(BQ76940_STATUS_OK, 0x04U);
    config = TestAfe_ValidConfig();
    TEST_AFE_CHECK(TestAfe_Start(&startup, &device, &config));
    now_ms = 0U;
    result = TestAfe_DriveTerminal(&startup, &now_ms);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_FAILED);
    TEST_AFE_CHECK(startup.failure ==
                   BMS_AFE_STARTUP_FAILURE_UNSAFE_STATUS);
    TEST_AFE_CHECK(startup.unsafe_sys_stat == 0x04U);
    TEST_AFE_CHECK(s_test_afe_fake.write_count ==
                   BMS_AFE_STARTUP_REGISTER_COUNT);

    TestAfe_ResetFake();
    TestAfe_AddSysStat(BQ76940_STATUS_OK,
                       BMS_AFE_STARTUP_STAT_DEVICE_XREADY);
    TestAfe_AddSysStat(BQ76940_STATUS_OK,
                       BMS_AFE_STARTUP_STAT_DEVICE_XREADY);
    TestAfe_AddSysStat(BQ76940_STATUS_OK, 0x10U);
    TEST_AFE_CHECK(TestAfe_Start(&startup, &device, &config));
    now_ms = 0U;
    result = TestAfe_DriveTerminal(&startup, &now_ms);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_FAILED);
    TEST_AFE_CHECK(startup.failure ==
                   BMS_AFE_STARTUP_FAILURE_UNSAFE_STATUS);
    TEST_AFE_CHECK(startup.unsafe_sys_stat == 0x10U);
    TEST_AFE_CHECK(s_test_afe_fake.write_count ==
                   ((2U * BMS_AFE_STARTUP_REGISTER_COUNT) + 1U));
}

static void TestAfe_ConfigWriteAmbiguityIsNotReplayed(void)
{
    BMS_AfeStartup_t startup;
    BQ76940_t device;
    BMS_AfeStartupConfig_t config;
    BMS_AfeStartupResult_t result;
    uint32_t now_ms;
    uint32_t before;

    TestAfe_ResetFake();
    TestAfe_AddSysStat(BQ76940_STATUS_OK, 0U);
    s_test_afe_fake.write_fault_enabled = true;
    s_test_afe_fake.write_fault_address = BQ76940_REG_CC_CFG;
    s_test_afe_fake.write_fault_status =
        BQ76940_STATUS_WRITE_FINALIZATION_AMBIGUOUS;
    config = TestAfe_ValidConfig();
    TEST_AFE_CHECK(TestAfe_Start(&startup, &device, &config));
    now_ms = 0U;
    result = TestAfe_DriveTerminal(&startup, &now_ms);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_FAILED);
    TEST_AFE_CHECK(startup.failure ==
                   BMS_AFE_STARTUP_FAILURE_WRITE_FINALIZATION_AMBIGUOUS);
    TEST_AFE_CHECK(s_test_afe_fake.write_count == 5U);
    TEST_AFE_CHECK(s_test_afe_fake.writes[4].address ==
                   BQ76940_REG_CC_CFG);
    before = s_test_afe_fake.operation_count;
    result = TestAfe_StepChecked(&startup, now_ms);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_FAILED);
    TEST_AFE_CHECK(s_test_afe_fake.operation_count == before);
}

static void TestAfe_ReadbackAndCalibrationFailuresStop(void)
{
    BMS_AfeStartup_t startup;
    BQ76940_t device;
    BMS_AfeStartupConfig_t config;
    BMS_AfeStartupResult_t result;
    uint32_t now_ms;

    TestAfe_ResetFake();
    TestAfe_AddSysStat(BQ76940_STATUS_OK, 0U);
    s_test_afe_fake.mismatch_enabled = true;
    s_test_afe_fake.mismatch_address = BQ76940_REG_CELLBAL2;
    config = TestAfe_ValidConfig();
    TEST_AFE_CHECK(TestAfe_Start(&startup, &device, &config));
    now_ms = 0U;
    result = TestAfe_DriveTerminal(&startup, &now_ms);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_FAILED);
    TEST_AFE_CHECK(startup.failure ==
                   BMS_AFE_STARTUP_FAILURE_READBACK_MISMATCH);
    TEST_AFE_CHECK(s_test_afe_fake.write_count == 3U);
    TEST_AFE_CHECK(startup.failed_register_address ==
                   BQ76940_REG_CELLBAL2);

    TestAfe_ResetFake();
    TestAfe_AddSysStat(BQ76940_STATUS_OK, 0U);
    s_test_afe_fake.decode_status =
        BQ76940_STATUS_CALIBRATION_INVALID;
    TEST_AFE_CHECK(TestAfe_Start(&startup, &device, &config));
    now_ms = 0U;
    result = TestAfe_DriveTerminal(&startup, &now_ms);
    TEST_AFE_CHECK(result == BMS_AFE_STARTUP_RESULT_FAILED);
    TEST_AFE_CHECK(startup.failure ==
                   BMS_AFE_STARTUP_FAILURE_CALIBRATION);
    TEST_AFE_CHECK(FML_AfeStartup_IsFetOffConfirmed(&startup));
    TEST_AFE_CHECK(s_test_afe_fake.write_count == 5U);
    TEST_AFE_CHECK(s_test_afe_fake.decode_count == 1U);
}

uint32_t Test_Phase8_AfeStartup(void)
{
    g_phase8_afe_startup_test_failures = 0U;
    g_phase8_afe_startup_test_completed = 0U;

    TestAfe_RejectsIncompleteConfig();
    TestAfe_BoundsWakeAndProbe();
    TestAfe_UnsafeProbeConfirmsAllSafeOutputs();
    TestAfe_SuccessSequenceAndSettle();
    TestAfe_FinalFetReadbackUsesBoundedSafeOff();
    TestAfe_SafeOffFailureModesAreTerminal();
    TestAfe_InitialXreadyRetiredIsNotBlindCleared();
    TestAfe_XreadyClearForcesFullReconfiguration();
    TestAfe_XreadyAmbiguityIsTerminal();
    TestAfe_XreadyMustReadLowAfterClear();
    TestAfe_NewFaultBlocksCompletion();
    TestAfe_ConfigWriteAmbiguityIsNotReplayed();
    TestAfe_ReadbackAndCalibrationFailuresStop();

    g_phase8_afe_startup_test_completed = 1U;
    return g_phase8_afe_startup_test_failures;
}

#if defined(BMS_PHASE8_AFE_STANDALONE)
#include <stdio.h>

int main(void)
{
    uint32_t failures;

    failures = Test_Phase8_AfeStartup();
    if (failures == 0U)
    {
        (void)printf("PHASE8_AFE_STARTUP_TESTS_PASS\n");
        return 0;
    }
    (void)printf("PHASE8_AFE_STARTUP_TESTS_FAIL=%lu\n",
                 (unsigned long)failures);
    return 1;
}
#endif
