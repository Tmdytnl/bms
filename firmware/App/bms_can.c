#include "bms_can.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#if !defined(TEST_PHASE9_IMAGE)
#include "bsp_can.h"
#endif

static const BMS_Policy_t *s_policy;
static BMS_CanDiagnostics_t s_diagnostics;
#if !defined(TEST_PHASE9_IMAGE)
static uint32_t s_target_last_init_attempt_ms;
static bool s_target_init_attempted;
static bool s_target_rx_enabled;
#endif

#if !defined(TEST_PHASE9_IMAGE)
#define BMS_CAN_TARGET_RETRY_MS                  (1000UL)
#endif

static void BMS_Can_Increment(uint32_t *value)
{
    vTaskSuspendAll();
    if (*value < UINT32_MAX)
    {
        ++(*value);
    }
    (void)xTaskResumeAll();
}

static void BMS_Can_PutU16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value & 0xFFU);
    destination[1] = (uint8_t)(value >> 8U);
}

static void BMS_Can_PutU32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value & 0xFFUL);
    destination[1] = (uint8_t)((value >> 8U) & 0xFFUL);
    destination[2] = (uint8_t)((value >> 16U) & 0xFFUL);
    destination[3] = (uint8_t)((value >> 24U) & 0xFFUL);
}

static uint32_t BMS_Can_GetU32(const uint8_t *source)
{
    return (uint32_t)source[0] |
        ((uint32_t)source[1] << 8U) |
        ((uint32_t)source[2] << 16U) |
        ((uint32_t)source[3] << 24U);
}

static uint8_t BMS_Can_EncodeCellMv(uint16_t cell_mv)
{
    uint32_t encoded;

    if (cell_mv <= 2000U)
    {
        return 0U;
    }
    encoded = ((uint32_t)cell_mv - 2000UL) / 10UL;
    return encoded > 255UL ? 255U : (uint8_t)encoded;
}

static void BMS_Can_InitFrame(BMS_CanFrame_t *frame, uint16_t id)
{
    frame->ext_id = id;
    frame->dlc = 8U;
    (void)memset(frame->data, 0, sizeof(frame->data));
    frame->received_tick = 0U;
}

uint8_t BMS_Can_BuildTxFrames(
    const BMS_DataSnapshot_t *measurement,
    const BMS_StateSafetySnapshot_t *state,
    const BMS_ProtectSafetySnapshot_t *protect,
    const BMS_RecoverySnapshot_t *recovery,
    const BMS_FetManagerSnapshot_t *fet,
    BMS_CanFrame_t frames[BMS_CAN_TX_FRAME_COUNT])
{
    BMS_FaultBitmap_t active;
    BMS_FaultBitmap_t latched;
    uint16_t minimum;
    uint16_t maximum;
    int32_t current_10ma;
    uint8_t min_index;
    uint8_t max_index;
    uint8_t index;

    if ((measurement == NULL) || (state == NULL) || (protect == NULL) ||
        (recovery == NULL) || (fet == NULL) || (frames == NULL))
    {
        return 0U;
    }
    active = state->faults.active | protect->faults.active;
    latched = state->faults.latched | protect->faults.latched;
    minimum = measurement->cell_voltage_mv[0];
    maximum = minimum;
    min_index = 0U;
    max_index = 0U;
    for (index = 1U; index < BMS_CELL_COUNT; ++index)
    {
        if (measurement->cell_voltage_mv[index] < minimum)
        {
            minimum = measurement->cell_voltage_mv[index];
            min_index = index;
        }
        if (measurement->cell_voltage_mv[index] > maximum)
        {
            maximum = measurement->cell_voltage_mv[index];
            max_index = index;
        }
    }

    BMS_Can_InitFrame(&frames[0], 0x180U);
    frames[0].data[0] = BMS_CAN_PROTOCOL_VERSION;
    frames[0].data[1] = (uint8_t)state->state;
    frames[0].data[2] = (uint8_t)(
        (fet->effective.chg == BQ76940_FET_DESIRE_ENABLE ? 0x01U : 0U) |
        (fet->effective.dsg == BQ76940_FET_DESIRE_ENABLE ? 0x02U : 0U) |
        (recovery->technical_ready ? 0x04U : 0U) |
        (fet->register_state_confirmed ? 0x08U : 0U));
    frames[0].data[3] = active != 0UL ? 1U : 0U;
    frames[0].data[4] = (uint8_t)recovery->phase;
    frames[0].data[5] = (uint8_t)fet->transaction_state;
    BMS_Can_PutU16(&frames[0].data[6],
                   (uint16_t)measurement->sample_sequence);

    BMS_Can_InitFrame(&frames[1], 0x181U);
    BMS_Can_PutU16(&frames[1].data[0],
                   (uint16_t)measurement->pack_voltage_mv);
    current_10ma = measurement->current_ma / 10;
    if (current_10ma > INT16_MAX)
    {
        current_10ma = INT16_MAX;
    }
    else if (current_10ma < INT16_MIN)
    {
        current_10ma = INT16_MIN;
    }
    BMS_Can_PutU16(&frames[1].data[2], (uint16_t)(int16_t)current_10ma);
    BMS_Can_PutU16(&frames[1].data[4], measurement->soc_permille);
    BMS_Can_PutU16(&frames[1].data[6],
                   (uint16_t)measurement->remaining_capacity_mah);

    BMS_Can_InitFrame(&frames[2], 0x182U);
    BMS_Can_PutU16(&frames[2].data[0], minimum);
    BMS_Can_PutU16(&frames[2].data[2], maximum);
    BMS_Can_PutU16(&frames[2].data[4],
                   (uint16_t)measurement->temperature_decic);
    frames[2].data[6] = (uint8_t)(min_index + 1U);
    frames[2].data[7] = (uint8_t)(max_index + 1U);

    BMS_Can_InitFrame(&frames[3], 0x183U);
    BMS_Can_PutU32(&frames[3].data[0], active);
    BMS_Can_PutU32(&frames[3].data[4], latched);

    BMS_Can_InitFrame(&frames[4], 0x184U);
    for (index = 0U; index < 7U; ++index)
    {
        frames[4].data[index] = BMS_Can_EncodeCellMv(
            measurement->cell_voltage_mv[index]);
    }
    frames[4].data[7] = (uint8_t)measurement->sample_sequence;

    BMS_Can_InitFrame(&frames[5], 0x185U);
    for (index = 0U; index < 6U; ++index)
    {
        frames[5].data[index] = BMS_Can_EncodeCellMv(
            measurement->cell_voltage_mv[index + 7U]);
    }
    frames[5].data[6] = (uint8_t)measurement->afe_generation;
    frames[5].data[7] = (uint8_t)measurement->sample_sequence;
    return BMS_CAN_TX_FRAME_COUNT;
}

bool BMS_Can_DecodeServiceReset(
    const BMS_CanFrame_t *frame,
    uint32_t received_ms,
    uint32_t now_ms,
    const BMS_Policy_t *policy,
    const BMS_DataIdentity_t *identity,
    uint32_t qualification_revision,
    BMS_ServiceResetRequest_t *request)
{
    uint32_t request_id;

    if ((frame == NULL) || (policy == NULL) || (identity == NULL) ||
        (request == NULL) || (frame->ext_id != policy->can.service_rx_id) ||
        (frame->ext_id > 0x7FFUL) || (frame->dlc != 8U) ||
        (frame->data[0] != BMS_CAN_SERVICE_MAGIC) ||
        (frame->data[1] != BMS_CAN_SERVICE_RESET_COMMAND) ||
        (frame->data[2] >= (uint8_t)BMS_SERVICE_RESET_SOURCE_COUNT) ||
        (frame->data[3] != 0U) ||
        ((uint32_t)(now_ms - received_ms) >
         policy->can.rx_command_timeout_ms))
    {
        return false;
    }
    request_id = BMS_Can_GetU32(&frame->data[4]);
    if (request_id == 0UL)
    {
        return false;
    }
    request->source = (BMS_ServiceResetSource_t)frame->data[2];
    request->request_id = request_id;
    request->evaluated_sample_sequence = identity->sample_sequence;
    request->evaluated_afe_generation = identity->afe_generation;
    request->qualification_revision = qualification_revision;
    request->expiry_ms = (uint32_t)(now_ms +
        policy->service_reset_qualify_ms);
    request->valid = true;
    return true;
}

void BMS_Can_Init(const BMS_Policy_t *policy)
{
    s_policy = BMS_Policy_Validate(policy) ? policy : NULL;
    (void)memset(&s_diagnostics, 0, sizeof(s_diagnostics));
#if !defined(TEST_PHASE9_IMAGE)
    s_target_last_init_attempt_ms = 0UL;
    s_target_init_attempted = false;
    s_target_rx_enabled = false;
#endif
}

bool BMS_Can_BindTarget(const BMS_Policy_t *policy)
{
#if defined(TEST_PHASE9_IMAGE)
    (void)policy;
    return false;
#else
    s_target_init_attempted = true;
    s_target_last_init_attempt_ms = 0UL;
    s_target_rx_enabled = false;
    if ((policy == NULL) || !BMS_Policy_Validate(policy) ||
        !policy->can.standard_11_bit_ids ||
        !BSP_CAN_Init500K(policy->can.service_rx_id))
    {
        BMS_Can_Increment(&s_diagnostics.target_init_failure_count);
        return false;
    }
    return true;
#endif
}

bool BMS_Can_EnableTargetRx(void)
{
#if defined(TEST_PHASE9_IMAGE)
    return false;
#else
    if (!BSP_CAN_EnableRxInterrupt())
    {
        BMS_Can_Increment(&s_diagnostics.target_init_failure_count);
        return false;
    }
    s_target_rx_enabled = true;
    return true;
#endif
}

void BMS_Can_TxHardwareService(uint32_t now_ms)
{
#if defined(TEST_PHASE9_IMAGE)
    (void)now_ms;
#else
    BMS_CanFrame_t queued;
    BSP_CanFrame_t target;
    BSP_CanTxResult_t result;

    if (!BSP_CAN_IsInitialized())
    {
        if (s_target_init_attempted &&
            ((uint32_t)(now_ms - s_target_last_init_attempt_ms) <
             BMS_CAN_TARGET_RETRY_MS))
        {
            return;
        }
        s_target_init_attempted = true;
        s_target_last_init_attempt_ms = now_ms;
        s_target_rx_enabled = false;
        if ((s_policy == NULL) ||
            !BSP_CAN_Init500K(s_policy->can.service_rx_id))
        {
            BMS_Can_Increment(&s_diagnostics.target_init_failure_count);
            return;
        }
    }
    if (!s_target_rx_enabled && !BMS_Can_EnableTargetRx())
    {
        return;
    }

    if (BSP_CAN_IsBusOff())
    {
        if (BSP_CAN_Recover())
        {
            BMS_Can_Increment(
                &s_diagnostics.target_bus_off_recovery_count);
        }
        else
        {
            s_target_rx_enabled = false;
            return;
        }
    }
    while ((xCanTxQueue != NULL) &&
           (xQueueReceive(xCanTxQueue, &queued, 0U) == pdPASS))
    {
        target.id = queued.ext_id;
        target.extended = s_policy != NULL &&
            !s_policy->can.standard_11_bit_ids;
        target.dlc = queued.dlc;
        (void)memcpy(target.data, queued.data, sizeof(target.data));
        result = BSP_CAN_TryTransmit(&target);
        if (result == BSP_CAN_TX_ACCEPTED)
        {
            BMS_Can_Increment(&s_diagnostics.target_tx_count);
        }
        else if (result == BSP_CAN_TX_NO_MAILBOX)
        {
            if (xQueueSendToFront(xCanTxQueue, &queued, 0U) != pdPASS)
            {
                BMS_Can_Increment(&s_diagnostics.target_tx_drop_count);
            }
            break;
        }
        else
        {
            BMS_Can_Increment(&s_diagnostics.target_tx_drop_count);
        }
    }
#endif
}

void BMS_Can_TxRunOnce(uint32_t now_ms)
{
    BMS_DataSnapshot_t measurement;
    BMS_StateSafetySnapshot_t state;
    BMS_ProtectSafetySnapshot_t protect;
    BMS_RecoverySnapshot_t recovery;
    BMS_FetManagerSnapshot_t fet;
    BMS_CanFrame_t frames[BMS_CAN_TX_FRAME_COUNT];
    uint8_t count;
    uint8_t index;

    if ((s_policy == NULL) ||
        !BMS_Data_GetSnapshot(&measurement, now_ms))
    {
        return;
    }
    state = BMS_State_GetSafetySnapshot();
    protect = BMS_Protect_GetSafetySnapshot();
    recovery = BMS_Recovery_GetSnapshot();
    fet = BMS_FetManager_GetSnapshot();
    count = BMS_Can_BuildTxFrames(
        &measurement, &state, &protect, &recovery, &fet, frames);
    BMS_Can_Increment(&s_diagnostics.tx_cycle_count);
    for (index = 0U; index < count; ++index)
    {
        if ((xCanTxQueue != NULL) &&
            (xQueueSend(xCanTxQueue, &frames[index], 0U) == pdPASS))
        {
            BMS_Can_Increment(&s_diagnostics.tx_enqueued_count);
        }
        else
        {
            /* CAN absence/congestion is diagnostic-only in SIM_POLICY_V1. */
            BMS_Can_Increment(&s_diagnostics.tx_drop_count);
        }
    }
}

void BMS_Can_RxProcess(const BMS_CanFrame_t *frame,
                       uint32_t received_ms,
                       uint32_t now_ms)
{
    BMS_DataIdentity_t identity;
    BMS_StateSafetySnapshot_t state;
    BMS_ServiceResetRequest_t request;

    if ((s_policy == NULL) || !BMS_Data_GetIdentity(&identity))
    {
        BMS_Can_Increment(&s_diagnostics.rx_invalid_count);
        return;
    }
    state = BMS_State_GetSafetySnapshot();
    if (!BMS_Can_DecodeServiceReset(
            frame, received_ms, now_ms, s_policy, &identity,
            state.publication_revision, &request))
    {
        BMS_Can_Increment(&s_diagnostics.rx_invalid_count);
        return;
    }
    BMS_Can_Increment(&s_diagnostics.rx_valid_count);
    if (BMS_Protect_SubmitServiceResetRequest(&request))
    {
        BMS_Can_Increment(&s_diagnostics.service_request_count);
    }
    else
    {
        BMS_Can_Increment(&s_diagnostics.service_reject_count);
    }
}

BMS_CanDiagnostics_t BMS_Can_GetDiagnostics(void)
{
    BMS_CanDiagnostics_t snapshot;

    vTaskSuspendAll();
    snapshot = s_diagnostics;
    (void)xTaskResumeAll();
    return snapshot;
}

#if !defined(TEST_PHASE9_IMAGE)
void USB_LP_CAN1_RX0_IRQHandler(void)
{
    BaseType_t higher_priority_task_woken;
    BSP_CanFrame_t target;
    BMS_CanFrame_t frame;

    higher_priority_task_woken = pdFALSE;
    if (BSP_CAN_IsRxFifoOverrun())
    {
        BSP_CAN_ClearRxFifoOverrun();
        if (s_diagnostics.target_rx_fifo_overrun_count < UINT32_MAX)
        {
            ++s_diagnostics.target_rx_fifo_overrun_count;
        }
    }
    while (BSP_CAN_ReceivePending())
    {
        if (BSP_CAN_Receive(&target))
        {
            frame.ext_id = target.id;
            frame.dlc = target.dlc;
            (void)memcpy(frame.data, target.data, sizeof(frame.data));
            frame.received_tick = xTaskGetTickCountFromISR();
            if ((xCanRxQueue == NULL) ||
                (xQueueSendFromISR(xCanRxQueue, &frame,
                                   &higher_priority_task_woken) != pdPASS))
            {
                if (s_diagnostics.target_rx_queue_drop_count < UINT32_MAX)
                {
                    ++s_diagnostics.target_rx_queue_drop_count;
                }
            }
        }
    }
    portYIELD_FROM_ISR(higher_priority_task_woken);
}
#endif
