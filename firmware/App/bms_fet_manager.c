#include "bms_fet_manager.h"

#include <stddef.h>

#include "app_rtos.h"
#include "bms_data.h"
#include "bms_protect.h"
#include "bq76940_regs.h"

#define BMS_FET_MANAGER_I2C_TIMEOUT_MS          (20U)
#define BMS_FET_MANAGER_CC_EN                   ((uint8_t)0x40U)

static BQ76940_t *s_device;
static BMS_FetManagerSnapshot_t s_snapshot;

#if defined(TEST_PHASE9_IMAGE)
static BMS_FetManagerTestHook_t s_after_read_hook;
static BMS_FetManagerTestHook_t s_after_write_hook;
#endif

static void BMS_FetManager_Publish(void)
{
    s_snapshot.publication_revision =
        (uint32_t)(s_snapshot.publication_revision + 1UL);
}

static bool BMS_FetManager_RequestHasEnable(
    const BQ76940_FetRequest_t *request)
{
    return (request->chg == BQ76940_FET_DESIRE_ENABLE) ||
        (request->dsg == BQ76940_FET_DESIRE_ENABLE);
}

static bool BMS_FetManager_SnapshotsStillCurrent(
    const BMS_ProtectSafetySnapshot_t *protect,
    const BMS_StateSafetySnapshot_t *state,
    const BMS_RecoverySnapshot_t *recovery)
{
    BMS_ProtectSafetySnapshot_t current_protect;
    BMS_StateSafetySnapshot_t current_state;
    BMS_RecoverySnapshot_t current_recovery;
    BMS_DataIdentity_t identity;

    current_protect = BMS_Protect_GetSafetySnapshot();
    current_state = BMS_State_GetSafetySnapshot();
    current_recovery = BMS_Recovery_GetSnapshot();
    return BMS_Data_GetIdentity(&identity) &&
        (current_protect.publication_revision ==
         protect->publication_revision) &&
        (current_state.publication_revision ==
         state->publication_revision) &&
        (current_recovery.publication_revision ==
         recovery->publication_revision) &&
        (identity.sample_sequence ==
         state->evaluated_sample_sequence) &&
        (identity.afe_generation ==
         state->evaluated_afe_generation);
}

static void BMS_FetManager_ComposeEffective(
    const BMS_ProtectSafetySnapshot_t *protect,
    const BMS_StateSafetySnapshot_t *state,
    const BMS_RecoverySnapshot_t *recovery,
    BQ76940_FetRequest_t *effective)
{
    s_snapshot.requested = state->operational_intent;
    s_snapshot.inhibit_chg_reasons =
        protect->inhibit_chg_reasons |
        state->inhibit_chg_reasons |
        recovery->inhibit_chg_reasons;
    s_snapshot.inhibit_dsg_reasons =
        protect->inhibit_dsg_reasons |
        state->inhibit_dsg_reasons |
        recovery->inhibit_dsg_reasons;
    if (!recovery->technical_ready)
    {
        s_snapshot.inhibit_chg_reasons |= BMS_INHIBIT_REASON_RECOVERY;
        s_snapshot.inhibit_dsg_reasons |= BMS_INHIBIT_REASON_RECOVERY;
    }
    if (s_snapshot.transaction_state ==
        BMS_FET_TRANSACTION_QUARANTINED)
    {
        if (BMS_FetManager_RequestHasEnable(&s_snapshot.requested))
        {
            s_snapshot.enable_denied_by_quarantine = true;
        }
        s_snapshot.inhibit_chg_reasons |=
            BMS_INHIBIT_REASON_FET_UNVERIFIED;
        s_snapshot.inhibit_dsg_reasons |=
            BMS_INHIBIT_REASON_FET_UNVERIFIED;
    }
    BQ76940_Control_ApplyInhibits(
        &s_snapshot.requested,
        s_snapshot.inhibit_chg_reasons != 0UL,
        s_snapshot.inhibit_dsg_reasons != 0UL,
        effective);
}

static bool BMS_FetManager_AttemptSafeOffLocked(void)
{
    BQ76940_FetRequest_t safe_off;
    BQ76940_Status_t status;
    uint8_t current;
    uint8_t expected;
    uint8_t actual;

    safe_off.chg = BQ76940_FET_DESIRE_DISABLE;
    safe_off.dsg = BQ76940_FET_DESIRE_DISABLE;
    current = 0U;
    status = BQ76940_ReadByte(s_device, BQ76940_REG_SYS_CTRL2, &current);
    if (status != BQ76940_STATUS_OK)
    {
        s_snapshot.last_transport_status = status;
        s_snapshot.register_state_confirmed = false;
        return false;
    }
    expected = BQ76940_Control_SysCtrl2WithFets(current, &safe_off);
    status = BQ76940_WriteByte(s_device, BQ76940_REG_SYS_CTRL2, expected);
    if (status != BQ76940_STATUS_OK)
    {
        s_snapshot.last_transport_status = status;
        s_snapshot.register_state_confirmed = false;
        return false;
    }
    actual = 0U;
    status = BQ76940_ReadByte(s_device, BQ76940_REG_SYS_CTRL2, &actual);
    s_snapshot.expected_sys_ctrl2 = expected;
    s_snapshot.observed_sys_ctrl2 = actual;
    s_snapshot.last_transport_status = status;
    if ((status != BQ76940_STATUS_OK) || (actual != expected))
    {
        s_snapshot.register_state_confirmed = false;
        return false;
    }
    BQ76940_Control_ObserveFets(actual, &s_snapshot.observed);
    s_snapshot.register_state_confirmed = true;
    s_snapshot.effective = safe_off;
    return true;
}

void BMS_FetManager_Init(BQ76940_t *device)
{
    s_device = device;
    s_snapshot.transaction_state = BMS_FET_TRANSACTION_UNVERIFIED;
    s_snapshot.requested.chg = BQ76940_FET_DESIRE_DISABLE;
    s_snapshot.requested.dsg = BQ76940_FET_DESIRE_DISABLE;
    s_snapshot.effective = s_snapshot.requested;
    s_snapshot.observed.chg_on = false;
    s_snapshot.observed.dsg_on = false;
    s_snapshot.inhibit_chg_reasons = BMS_INHIBIT_REASON_FET_UNVERIFIED;
    s_snapshot.inhibit_dsg_reasons = BMS_INHIBIT_REASON_FET_UNVERIFIED;
    s_snapshot.publication_revision = 0UL;
    s_snapshot.protect_revision = 0UL;
    s_snapshot.state_revision = 0UL;
    s_snapshot.recovery_revision = 0UL;
    s_snapshot.expected_sys_ctrl2 = 0U;
    s_snapshot.observed_sys_ctrl2 = 0U;
    s_snapshot.last_transport_status = BQ76940_STATUS_NOT_INITIALIZED;
    s_snapshot.register_state_confirmed = false;
    s_snapshot.enable_denied_by_quarantine = false;
#if defined(TEST_PHASE9_IMAGE)
    s_after_read_hook = NULL;
    s_after_write_hook = NULL;
#endif
}

void BMS_FetManager_Service(void)
{
    BMS_ProtectSafetySnapshot_t protect;
    BMS_StateSafetySnapshot_t state;
    BMS_RecoverySnapshot_t recovery;
    BQ76940_FetRequest_t effective;
    BQ76940_Status_t status;
    uint8_t current;
    uint8_t expected;
    uint8_t actual;
    bool enabling;
    bool quarantined;

    protect = BMS_Protect_GetSafetySnapshot();
    state = BMS_State_GetSafetySnapshot();
    recovery = BMS_Recovery_GetSnapshot();
    quarantined = s_snapshot.transaction_state ==
        BMS_FET_TRANSACTION_QUARANTINED;
    BMS_FetManager_ComposeEffective(&protect, &state, &recovery, &effective);
    enabling = BMS_FetManager_RequestHasEnable(&effective);
    s_snapshot.protect_revision = protect.publication_revision;
    s_snapshot.state_revision = state.publication_revision;
    s_snapshot.recovery_revision = recovery.publication_revision;
    s_snapshot.effective = effective;
    s_snapshot.register_state_confirmed = false;

    if ((s_device == NULL) || (xI2CMutex == NULL) ||
        (xSemaphoreTake(xI2CMutex,
                       pdMS_TO_TICKS(BMS_FET_MANAGER_I2C_TIMEOUT_MS)) !=
         pdTRUE))
    {
        if (!quarantined)
        {
            s_snapshot.transaction_state = BMS_FET_TRANSACTION_UNVERIFIED;
        }
        s_snapshot.last_transport_status = BQ76940_STATUS_I2C_TIMEOUT;
        BMS_FetManager_Publish();
        return;
    }

    if (enabling &&
        !BMS_FetManager_SnapshotsStillCurrent(&protect, &state, &recovery))
    {
        (void)BMS_FetManager_AttemptSafeOffLocked();
        if (!quarantined)
        {
            s_snapshot.transaction_state =
                s_snapshot.register_state_confirmed ?
                BMS_FET_TRANSACTION_CONFIRMED_SAFE :
                BMS_FET_TRANSACTION_UNVERIFIED;
        }
        (void)xSemaphoreGive(xI2CMutex);
        BMS_FetManager_Publish();
        return;
    }

    current = 0U;
    status = BQ76940_ReadByte(s_device, BQ76940_REG_SYS_CTRL2, &current);
    s_snapshot.last_transport_status = status;
    if (status != BQ76940_STATUS_OK)
    {
        if (!quarantined)
        {
            s_snapshot.transaction_state = BMS_FET_TRANSACTION_UNVERIFIED;
        }
        (void)xSemaphoreGive(xI2CMutex);
        BMS_FetManager_Publish();
        return;
    }
#if defined(TEST_PHASE9_IMAGE)
    if (s_after_read_hook != NULL)
    {
        s_after_read_hook();
    }
#endif
    if (enabling &&
        (((current & BMS_FET_MANAGER_CC_EN) == 0U) ||
         !BMS_FetManager_SnapshotsStillCurrent(&protect, &state, &recovery)))
    {
        (void)BMS_FetManager_AttemptSafeOffLocked();
        if (!quarantined)
        {
            s_snapshot.transaction_state =
                s_snapshot.register_state_confirmed ?
                BMS_FET_TRANSACTION_CONFIRMED_SAFE :
                BMS_FET_TRANSACTION_UNVERIFIED;
        }
        (void)xSemaphoreGive(xI2CMutex);
        BMS_FetManager_Publish();
        return;
    }

    expected = BQ76940_Control_SysCtrl2WithFets(current, &effective);
    s_snapshot.expected_sys_ctrl2 = expected;
    if (current != expected)
    {
        status = BQ76940_WriteByte(s_device, BQ76940_REG_SYS_CTRL2, expected);
        s_snapshot.last_transport_status = status;
        if (status == BQ76940_STATUS_WRITE_FINALIZATION_AMBIGUOUS)
        {
            if (enabling)
            {
                s_snapshot.transaction_state =
                    BMS_FET_TRANSACTION_QUARANTINED;
                (void)BMS_FetManager_AttemptSafeOffLocked();
                s_snapshot.last_transport_status =
                    BQ76940_STATUS_WRITE_FINALIZATION_AMBIGUOUS;
            }
            else if (!quarantined)
            {
                s_snapshot.transaction_state =
                    BMS_FET_TRANSACTION_UNVERIFIED;
            }
            (void)xSemaphoreGive(xI2CMutex);
            BMS_FetManager_Publish();
            return;
        }
        if (status != BQ76940_STATUS_OK)
        {
            if (!quarantined)
            {
                s_snapshot.transaction_state =
                    BMS_FET_TRANSACTION_UNVERIFIED;
            }
            (void)xSemaphoreGive(xI2CMutex);
            BMS_FetManager_Publish();
            return;
        }
    }
#if defined(TEST_PHASE9_IMAGE)
    if (s_after_write_hook != NULL)
    {
        s_after_write_hook();
    }
#endif
    actual = 0U;
    status = BQ76940_ReadByte(s_device, BQ76940_REG_SYS_CTRL2, &actual);
    s_snapshot.observed_sys_ctrl2 = actual;
    s_snapshot.last_transport_status = status;
    if ((status != BQ76940_STATUS_OK) || (actual != expected))
    {
        if (enabling)
        {
            s_snapshot.transaction_state = BMS_FET_TRANSACTION_QUARANTINED;
            (void)BMS_FetManager_AttemptSafeOffLocked();
        }
        else if (!quarantined)
        {
            s_snapshot.transaction_state = BMS_FET_TRANSACTION_UNVERIFIED;
        }
        (void)xSemaphoreGive(xI2CMutex);
        BMS_FetManager_Publish();
        return;
    }
    BQ76940_Control_ObserveFets(actual, &s_snapshot.observed);
    s_snapshot.register_state_confirmed = true;

    if (enabling &&
        !BMS_FetManager_SnapshotsStillCurrent(&protect, &state, &recovery))
    {
        (void)BMS_FetManager_AttemptSafeOffLocked();
        if (!quarantined)
        {
            s_snapshot.transaction_state =
                s_snapshot.register_state_confirmed ?
                BMS_FET_TRANSACTION_CONFIRMED_SAFE :
                BMS_FET_TRANSACTION_UNVERIFIED;
        }
    }
    else if (!quarantined)
    {
        s_snapshot.transaction_state = enabling ?
            BMS_FET_TRANSACTION_CONFIRMED_APPLIED :
            BMS_FET_TRANSACTION_CONFIRMED_SAFE;
    }
    (void)xSemaphoreGive(xI2CMutex);
    BMS_FetManager_Publish();
}

BMS_FetManagerSnapshot_t BMS_FetManager_GetSnapshot(void)
{
    BMS_FetManagerSnapshot_t snapshot;

    vTaskSuspendAll();
    snapshot = s_snapshot;
    (void)xTaskResumeAll();
    return snapshot;
}

#if defined(TEST_PHASE9_IMAGE)
void BMS_FetManager_TestSetAfterReadHook(BMS_FetManagerTestHook_t hook)
{
    s_after_read_hook = hook;
}

void BMS_FetManager_TestSetAfterWriteHook(BMS_FetManagerTestHook_t hook)
{
    s_after_write_hook = hook;
}
#endif
