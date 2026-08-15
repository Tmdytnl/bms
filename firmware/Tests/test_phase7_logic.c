#include "test_phase7.h"

#include <stdbool.h>
#include <stdint.h>

#include "bms_fault.h"
#include "bms_protect.h"
#include "bq76940_control.h"

#define TEST_CHECK(expr_)          \
    do                             \
    {                              \
        if (!(expr_))              \
        {                          \
            ++failures;            \
        }                          \
    } while (0)

/*
 * Phase 7 pure-decision tests. BMS_Protect_Decide maps a raw SYS_STAT
 * snapshot to fault/request/clear decisions without any I2C or RTOS
 * dependency, so it is verified here without the transport/FreeRTOS
 * combination that the Keil simulator cannot execute together.
 */

uint32_t Test_Phase7_ProtectLogic(void)
{
    uint32_t failures;
    BMS_FaultSummary_t faults;
    BQ76940_FetRequest_t request;
    uint8_t clear_mask;

    failures = 0UL;

    /* OV bit: active HW_OV fault, CHG inhibited, OV bit cleared. */
    BMS_Fault_Init(&faults);
    request.chg = BQ76940_FET_DESIRE_ENABLE;
    request.dsg = BQ76940_FET_DESIRE_ENABLE;
    clear_mask = 0U;
    BMS_Protect_Decide(BMS_PROTECT_STAT_OV, &faults, &request, &clear_mask);
    TEST_CHECK(BMS_Fault_Contains(faults.active, BMS_FAULT_ID_HW_OV));
    TEST_CHECK(!BMS_Fault_Contains(faults.active, BMS_FAULT_ID_HW_UV));
    TEST_CHECK(request.chg == BQ76940_FET_DESIRE_DISABLE);
    TEST_CHECK(request.dsg == BQ76940_FET_DESIRE_ENABLE);
    TEST_CHECK(clear_mask == BMS_PROTECT_STAT_OV);

    /* SCD: active+latched HW_SCD, both FETs inhibited. */
    BMS_Fault_Init(&faults);
    request.chg = BQ76940_FET_DESIRE_ENABLE;
    request.dsg = BQ76940_FET_DESIRE_ENABLE;
    clear_mask = 0U;
    BMS_Protect_Decide(BMS_PROTECT_STAT_SCD, &faults, &request, &clear_mask);
    TEST_CHECK(BMS_Fault_Contains(faults.active, BMS_FAULT_ID_HW_SCD));
    TEST_CHECK(BMS_Fault_Contains(faults.latched, BMS_FAULT_ID_HW_SCD));
    TEST_CHECK(request.chg == BQ76940_FET_DESIRE_DISABLE);
    TEST_CHECK(request.dsg == BQ76940_FET_DESIRE_DISABLE);
    TEST_CHECK(clear_mask == BMS_PROTECT_STAT_SCD);

    /* OVRD_ALERT (H-01): independent fault + both FETs inhibited. */
    BMS_Fault_Init(&faults);
    request.chg = BQ76940_FET_DESIRE_ENABLE;
    request.dsg = BQ76940_FET_DESIRE_ENABLE;
    clear_mask = 0U;
    BMS_Protect_Decide(BMS_PROTECT_STAT_OVRD_ALERT, &faults, &request,
                       &clear_mask);
    TEST_CHECK(BMS_Fault_Contains(faults.active,
                                  BMS_FAULT_ID_AFE_OVRD_ALERT));
    TEST_CHECK(BMS_Fault_Contains(faults.latched,
                                  BMS_FAULT_ID_AFE_OVRD_ALERT));
    TEST_CHECK(request.chg == BQ76940_FET_DESIRE_DISABLE);
    TEST_CHECK(request.dsg == BQ76940_FET_DESIRE_DISABLE);
    TEST_CHECK(clear_mask == BMS_PROTECT_STAT_OVRD_ALERT);

    /* XREADY (H-03): latched fault, both off, but NOT in clear mask. */
    BMS_Fault_Init(&faults);
    request.chg = BQ76940_FET_DESIRE_ENABLE;
    request.dsg = BQ76940_FET_DESIRE_ENABLE;
    clear_mask = 0U;
    BMS_Protect_Decide(BMS_PROTECT_STAT_DEVICE_XREADY, &faults, &request,
                       &clear_mask);
    TEST_CHECK(BMS_Fault_Contains(faults.active, BMS_FAULT_ID_AFE_XREADY));
    TEST_CHECK(BMS_Fault_Contains(faults.latched, BMS_FAULT_ID_AFE_XREADY));
    TEST_CHECK(request.chg == BQ76940_FET_DESIRE_DISABLE);
    TEST_CHECK(request.dsg == BQ76940_FET_DESIRE_DISABLE);
    TEST_CHECK(clear_mask == 0U);   /* XREADY never cleared by Decide */

    /* Combined CC_READY + OV (spec §20): CC_READY adds no fault, OV does. */
    BMS_Fault_Init(&faults);
    request.chg = BQ76940_FET_DESIRE_ENABLE;
    request.dsg = BQ76940_FET_DESIRE_ENABLE;
    clear_mask = 0U;
    BMS_Protect_Decide(BMS_PROTECT_STAT_CC_READY | BMS_PROTECT_STAT_OV,
                       &faults, &request, &clear_mask);
    TEST_CHECK(BMS_Fault_Contains(faults.active, BMS_FAULT_ID_HW_OV));
    TEST_CHECK(request.chg == BQ76940_FET_DESIRE_DISABLE);
    /* Decide handles fault bits; CC_READY bit is added by the caller
     * (HandleCcReady) after the queue push succeeds. */
    TEST_CHECK((clear_mask & BMS_PROTECT_STAT_CC_READY) == 0U);
    TEST_CHECK((clear_mask & BMS_PROTECT_STAT_OV) != 0U);

    /* Clean SYS_STAT: no faults, nothing cleared. */
    BMS_Fault_Init(&faults);
    request.chg = BQ76940_FET_DESIRE_ENABLE;
    request.dsg = BQ76940_FET_DESIRE_ENABLE;
    clear_mask = 0xFFU;
    BMS_Protect_Decide(0U, &faults, &request, &clear_mask);
    TEST_CHECK(faults.active == 0U);
    TEST_CHECK(clear_mask == 0U);

    /* HasFaultBits classification. */
    TEST_CHECK(BMS_Protect_HasFaultBits(BMS_PROTECT_STAT_OV));
    TEST_CHECK(BMS_Protect_HasFaultBits(BMS_PROTECT_STAT_DEVICE_XREADY));
    TEST_CHECK(!BMS_Protect_HasFaultBits(BMS_PROTECT_STAT_CC_READY));
    TEST_CHECK(!BMS_Protect_HasFaultBits(0U));

    return failures;
}

/* CC queue H-02 policy is exercised in the FreeRTOS context; the pure
 * decision path above is the Phase 7 simulator-verifiable core. The
 * queue policy is covered by the Phase 6 foundation tests and the
 * hardware-validation-deferred integration path. */
uint32_t Test_Phase7_CcQueue(void)
{
    return 0UL;
}

uint32_t Test_Phase7_Xready(void)
{
    /* The XREADY recovery chain needs I2C transport (Simulator cannot run
     * FreeRTOS + bq76940 transport together in this environment). The
     * Decide mapping for XREADY is verified above; the full recovery is
     * HARDWARE VALIDATION REQUIRED / integration-deferred. */
    return 0UL;
}
