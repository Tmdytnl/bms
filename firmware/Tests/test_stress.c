#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bms_balance.h"
#include "bms_persistence.h"
#include "bms_policy.h"
#include "bms_recovery.h"
#include "bms_soc.h"
#include "bms_state.h"

#define TEST_STRESS_ITERATIONS                   (50000UL)
#define TEST_STRESS_STATE_STEP_MS                (12000UL)
#define TEST_STRESS_PERSISTENCE_TRANSACTIONS     (512UL)

volatile uint32_t g_stress_test_completed;
volatile uint32_t g_stress_test_failures;
volatile uint32_t g_stress_iterations;
volatile uint32_t g_stress_simulated_ms;
volatile uint32_t g_stress_random_fault_events;
volatile uint32_t g_stress_persistence_transactions;
volatile uint32_t g_stress_power_cuts;

#define STRESS_CHECK(expression_)         \
    do                                    \
    {                                     \
        if (!(expression_))               \
        {                                 \
            ++g_stress_test_failures;     \
        }                                 \
    } while (0)

typedef struct
{
    uint8_t pages[2][1024];
    uint32_t program_count;
    uint32_t fail_program_ordinal;
} TestStressFlash_t;

static BMS_StateEngine_t s_stress_state_engine;
static BMS_DataSnapshot_t s_stress_measurement;
static BMS_StateSafetySnapshot_t s_stress_decision;
static BMS_ProtectSafetySnapshot_t s_stress_protect;
static BMS_RecoverySnapshot_t s_stress_recovery;
static BMS_SocEngine_t s_stress_soc_engine;
static BMS_BalanceEngine_t s_stress_balance_engine;
static TestStressFlash_t s_stress_flash;
static BMS_PersistenceStore_t s_stress_store;
static BMS_PersistenceStore_t s_stress_reboot;

static uint32_t TestStress_Random(uint32_t *state)
{
    uint32_t value;

    value = *state;
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    *state = value;
    return value;
}

static uint8_t TestStress_Popcount16(uint16_t value)
{
    uint8_t count;

    count = 0U;
    while (value != 0U)
    {
        count = (uint8_t)(count + (uint8_t)(value & 1U));
        value >>= 1U;
    }
    return count;
}

static void TestStress_InitStateEngine(uint32_t now_ms)
{
    (void)memset(&s_stress_state_engine, 0,
                 sizeof(s_stress_state_engine));
    s_stress_state_engine.state = BMS_STATE_INIT;
    s_stress_state_engine.transition_candidate = BMS_STATE_STANDBY;
    s_stress_state_engine.init_started_ms = now_ms;
    s_stress_state_engine.data_stale_active = true;
    s_stress_state_engine.initialized = true;
}

static void TestStress_Measurement(uint32_t random_value,
                                   uint32_t sequence,
                                   uint32_t generation)
{
    uint8_t index;
    uint16_t cell_mv;

    (void)memset(&s_stress_measurement, 0,
                 sizeof(s_stress_measurement));
    cell_mv = (uint16_t)(3600U + (random_value % 201UL));
    for (index = 0U; index < BMS_CELL_COUNT; ++index)
    {
        s_stress_measurement.cell_voltage_mv[index] = cell_mv;
        s_stress_measurement.cell_metadata.age_ms[index] = 0UL;
    }
    s_stress_measurement.cell_metadata.valid_bitmap =
        BMS_CELL_DEFINED_MASK;
    s_stress_measurement.cell_metadata.in_range_bitmap =
        BMS_CELL_DEFINED_MASK;
    s_stress_measurement.pack_metadata.valid = true;
    s_stress_measurement.pack_metadata.in_range = true;
    s_stress_measurement.current_metadata.valid = true;
    s_stress_measurement.current_metadata.in_range = true;
    s_stress_measurement.temperature_metadata.valid = true;
    s_stress_measurement.temperature_metadata.in_range = true;
    s_stress_measurement.current_ma =
        (int32_t)(random_value % 24001UL) - 12000;
    s_stress_measurement.temperature_decic =
        (int16_t)((int32_t)((random_value >> 8U) % 901UL) - 200);
    s_stress_measurement.sample_sequence = sequence;
    s_stress_measurement.afe_generation = generation;

    if ((random_value & 0x0FU) == 0U)
    {
        s_stress_measurement.cell_voltage_mv[random_value % BMS_CELL_COUNT] =
            (random_value & 0x10U) != 0U ? 4250U : 2950U;
        ++g_stress_random_fault_events;
    }
    if ((random_value & 0x3FU) == 1U)
    {
        s_stress_measurement.current_metadata.age_ms = 2000UL;
        ++g_stress_random_fault_events;
    }
    if ((random_value & 0x7FU) == 2U)
    {
        s_stress_measurement.cell_metadata.valid_bitmap ^= 1U;
        ++g_stress_random_fault_events;
    }
}

static void TestStress_StateSocBalance(void)
{
    const BMS_Policy_t *policy;
    uint32_t random_state;
    uint32_t random_value;
    uint32_t state_now;
    uint32_t soc_now;
    uint32_t sequence;
    uint32_t generation;
    uint32_t index;
    int32_t soc_current;
    bool technical_ready;
    bool health_fault;
    uint16_t selected;
    BMS_SocSnapshot_t soc;

    policy = BMS_Policy_Get();
    random_state = 0xB5F103C8UL;
    state_now = UINT32_MAX - 300000000UL;
    soc_now = UINT32_MAX - 25000000UL;
    sequence = UINT32_MAX - 25000UL;
    generation = 0UL;
    TestStress_InitStateEngine(state_now);
    (void)memset(&s_stress_balance_engine, 0,
                 sizeof(s_stress_balance_engine));
    (void)memset(&s_stress_protect, 0, sizeof(s_stress_protect));
    (void)memset(&s_stress_recovery, 0, sizeof(s_stress_recovery));
    TestStress_Measurement(random_state, sequence, generation);
    STRESS_CHECK(BMS_Soc_EngineInit(&s_stress_soc_engine,
        &policy->soc, &s_stress_measurement, soc_now));

    for (index = 0UL; index < TEST_STRESS_ITERATIONS; ++index)
    {
        random_value = TestStress_Random(&random_state);
        ++sequence;
        if ((random_value & 0x3FFUL) == 0x155UL)
        {
            ++generation;
            ++g_stress_random_fault_events;
        }
        TestStress_Measurement(random_value, sequence, generation);
        technical_ready = (random_value & 0x1FUL) != 3UL;
        health_fault = (random_value & 0x3FUL) == 5UL;
        if (!technical_ready || health_fault)
        {
            ++g_stress_random_fault_events;
        }
        state_now += TEST_STRESS_STATE_STEP_MS;
        STRESS_CHECK(BMS_State_Evaluate(&s_stress_state_engine, policy,
            &s_stress_measurement, state_now, technical_ready,
            health_fault, &s_stress_decision));
        STRESS_CHECK(s_stress_decision.state < BMS_STATE_COUNT);
        STRESS_CHECK((s_stress_decision.faults.active &
                      ~BMS_FAULT_DEFINED_MASK) == 0UL);
        STRESS_CHECK(s_stress_decision.evaluated_sample_sequence ==
                     sequence);
        STRESS_CHECK(s_stress_decision.evaluated_afe_generation ==
                     generation);
        if (!technical_ready)
        {
            STRESS_CHECK(s_stress_decision.operational_intent.chg ==
                         BQ76940_FET_DESIRE_DISABLE);
            STRESS_CHECK(s_stress_decision.operational_intent.dsg ==
                         BQ76940_FET_DESIRE_DISABLE);
        }
        if (BMS_Fault_Contains(s_stress_decision.faults.active,
                              BMS_FAULT_ID_DATA_STALE))
        {
            STRESS_CHECK(s_stress_decision.inhibit_chg_reasons != 0UL);
            STRESS_CHECK(s_stress_decision.inhibit_dsg_reasons != 0UL);
        }
        if (BMS_Fault_Contains(s_stress_decision.faults.active,
                              BMS_FAULT_ID_SW_OV))
        {
            STRESS_CHECK(s_stress_decision.inhibit_chg_reasons != 0UL);
        }
        if (BMS_Fault_Contains(s_stress_decision.faults.active,
                              BMS_FAULT_ID_SW_UV))
        {
            STRESS_CHECK(s_stress_decision.inhibit_dsg_reasons != 0UL);
        }

        s_stress_recovery.technical_ready = technical_ready;
        selected = BMS_Balance_Evaluate(&s_stress_balance_engine,
            &policy->balance, &s_stress_measurement,
            &s_stress_decision, &s_stress_protect,
            &s_stress_recovery, state_now);
        STRESS_CHECK((selected & ~BMS_CELL_DEFINED_MASK) == 0U);
        STRESS_CHECK(TestStress_Popcount16(selected) <=
                     policy->balance.max_parallel_cells);
        STRESS_CHECK(policy->balance.adjacent_cells_permitted ||
                     ((selected & (uint16_t)(selected << 1U)) == 0U));
        if ((s_stress_decision.inhibit_chg_reasons != 0UL) ||
            (s_stress_decision.inhibit_dsg_reasons != 0UL) ||
            !technical_ready)
        {
            STRESS_CHECK(selected == 0U);
        }

        soc_current = (int32_t)((random_value >> 4U) % 6001UL) - 3000;
        soc_now += 1000UL;
        (void)BMS_Soc_IntegrateCurrent(&s_stress_soc_engine,
            &policy->soc, soc_current, soc_now, generation);
        soc = BMS_Soc_GetEngineSnapshot(
            &s_stress_soc_engine, &policy->soc);
        STRESS_CHECK(soc.soc_permille <= 1000U);
        STRESS_CHECK(soc.remaining_capacity_mah <=
                     policy->soc.capacity_mah);
    }
    g_stress_iterations = TEST_STRESS_ITERATIONS;
    g_stress_simulated_ms =
        TEST_STRESS_ITERATIONS * TEST_STRESS_STATE_STEP_MS;
}

static bool TestStress_FlashMap(uint32_t address, uint16_t length,
                                uint8_t **mapped)
{
    const BMS_FlashPolicy_t *policy;
    uint32_t offset;
    uint8_t page;

    policy = &BMS_Policy_Get()->flash;
    if ((address >= policy->slot_a_address) &&
        (address < policy->slot_a_address + policy->page_size_bytes))
    {
        page = 0U;
        offset = address - policy->slot_a_address;
    }
    else if ((address >= policy->slot_b_address) &&
             (address < policy->slot_b_address + policy->page_size_bytes))
    {
        page = 1U;
        offset = address - policy->slot_b_address;
    }
    else
    {
        return false;
    }
    if ((offset + length) > policy->page_size_bytes)
    {
        return false;
    }
    *mapped = &s_stress_flash.pages[page][offset];
    return true;
}

static bool TestStress_FlashRead(void *context, uint32_t address,
                                 uint8_t *destination, uint16_t length)
{
    uint8_t *mapped;

    (void)context;
    if ((destination == NULL) ||
        !TestStress_FlashMap(address, length, &mapped))
    {
        return false;
    }
    (void)memcpy(destination, mapped, length);
    return true;
}

static bool TestStress_FlashErase(void *context, uint32_t page_address)
{
    uint8_t *mapped;
    uint16_t page_size;

    (void)context;
    page_size = BMS_Policy_Get()->flash.page_size_bytes;
    if (!TestStress_FlashMap(page_address, page_size, &mapped) ||
        ((page_address != BMS_Policy_Get()->flash.slot_a_address) &&
         (page_address != BMS_Policy_Get()->flash.slot_b_address)))
    {
        return false;
    }
    (void)memset(mapped, 0xFF, page_size);
    return true;
}

static bool TestStress_FlashProgram(void *context, uint32_t address,
                                    uint16_t value)
{
    uint8_t *mapped;

    (void)context;
    ++s_stress_flash.program_count;
    if ((s_stress_flash.fail_program_ordinal != 0UL) &&
        (s_stress_flash.program_count ==
         s_stress_flash.fail_program_ordinal))
    {
        return false;
    }
    if (((address & 1UL) != 0UL) ||
        !TestStress_FlashMap(address, 2U, &mapped) ||
        (mapped[0] != 0xFFU) || (mapped[1] != 0xFFU))
    {
        return false;
    }
    mapped[0] = (uint8_t)value;
    mapped[1] = (uint8_t)(value >> 8U);
    return true;
}

static void TestStress_Persistence(void)
{
    const BMS_FlashPolicy_t *policy;
    BMS_PersistenceStorageOps_t storage;
    BMS_PersistencePayload_t latest;
    BMS_PersistenceStoreResult_t result;
    uint32_t random_state;
    uint32_t random_value;
    uint32_t expected_sequence;
    uint32_t index;
    uint16_t expected_soc;
    uint16_t candidate_soc;
    bool expected_valid;
    bool cut;

    policy = &BMS_Policy_Get()->flash;
    (void)memset(&s_stress_flash, 0xFF, sizeof(s_stress_flash));
    s_stress_flash.program_count = 0UL;
    s_stress_flash.fail_program_ordinal = 0UL;
    storage.read = TestStress_FlashRead;
    storage.erase_page = TestStress_FlashErase;
    storage.program_halfword = TestStress_FlashProgram;
    storage.context = NULL;
    random_state = 0x5A17F00DUL;
    expected_sequence = 0UL;
    expected_soc = 0U;
    expected_valid = false;

    for (index = 0UL; index < TEST_STRESS_PERSISTENCE_TRANSACTIONS;
         ++index)
    {
        STRESS_CHECK(BMS_Persistence_StoreInit(
            &s_stress_store, policy, &storage));
        if (expected_valid)
        {
            STRESS_CHECK(BMS_Persistence_StoreGetLatest(
                &s_stress_store, &latest));
            STRESS_CHECK(latest.sequence == expected_sequence);
            STRESS_CHECK(latest.soc_permille == expected_soc);
        }
        else
        {
            STRESS_CHECK(!BMS_Persistence_StoreGetLatest(
                &s_stress_store, &latest));
        }

        candidate_soc = expected_valid ?
            (expected_soc >= 990U ? 0U :
             (uint16_t)(expected_soc + 10U)) : 500U;
        random_value = TestStress_Random(&random_state);
        cut = (random_value & 3UL) != 0UL;
        s_stress_flash.program_count = 0UL;
        s_stress_flash.fail_program_ordinal = cut ?
            (random_value %
             ((BMS_PERSISTENCE_BODY_BYTES / 2UL) + 1UL)) + 1UL : 0UL;
        if (cut)
        {
            ++g_stress_power_cuts;
        }
        result = BMS_Persistence_StoreSocIfDue(
            &s_stress_store, candidate_soc,
            (uint32_t)candidate_soc * 20UL,
            index, random_value, true, 60000UL);
        if (cut)
        {
            STRESS_CHECK(result == BMS_PERSISTENCE_STORE_IO_ERROR);
        }
        else
        {
            STRESS_CHECK(result == BMS_PERSISTENCE_STORE_SAVED);
            expected_sequence = expected_valid ?
                expected_sequence + 1UL : 1UL;
            expected_soc = candidate_soc;
            expected_valid = true;
        }

        STRESS_CHECK(BMS_Persistence_StoreInit(
            &s_stress_reboot, policy, &storage));
        if (expected_valid)
        {
            STRESS_CHECK(BMS_Persistence_StoreGetLatest(
                &s_stress_reboot, &latest));
            STRESS_CHECK(latest.sequence == expected_sequence);
            STRESS_CHECK(latest.soc_permille == expected_soc);
        }
        else
        {
            STRESS_CHECK(!BMS_Persistence_StoreGetLatest(
                &s_stress_reboot, &latest));
        }
    }
    g_stress_persistence_transactions =
        TEST_STRESS_PERSISTENCE_TRANSACTIONS;
}

uint32_t Test_Stress(void)
{
    g_stress_test_completed = 0UL;
    g_stress_test_failures = 0UL;
    g_stress_iterations = 0UL;
    g_stress_simulated_ms = 0UL;
    g_stress_random_fault_events = 0UL;
    g_stress_persistence_transactions = 0UL;
    g_stress_power_cuts = 0UL;
    TestStress_StateSocBalance();
    TestStress_Persistence();
    g_stress_test_completed = 1UL;
    return g_stress_test_failures;
}
