#include "bms_can.h"
#include "bms_can_codec.h"
#include "bms_runtime_port.h"

/*
 * FML 拥有 frame 编解码、协议校验与 service-request 语义；APL 只提供周期调度、
 * RX ISR→queue handoff 和 bxCAN Tx 执行上下文。显式 encode/decode 隔离 C struct
 * layout，service frame 只能提交 source-specific request，最终授权仍归 Protect。
 */

#include <limits.h>
#include <stddef.h>
#include <string.h>

/* 初始化时绑定的只读 CAN 协议策略。 */
static const BMS_Policy_t *s_policy;
/* CAN 编解码与服务请求的模块私有诊断计数。 */
static BMS_CanDiagnostics_t s_diagnostics;

/* 饱和递增一项 CAN 诊断计数，避免计数回绕误导观察者。 */
static void BMS_Can_Increment(uint32_t *value)
{
    BMS_Runtime_CriticalEnter();
    if (*value < UINT32_MAX)
    {
        ++(*value);
    }
    BMS_Runtime_CriticalExit();
}

/* 把 16 位值以协议规定的小端字节序写入载荷。 */
static void BMS_Can_PutU16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value & 0xFFU);
    destination[1] = (uint8_t)(value >> 8U);
}

/* 把 32 位值以协议规定的小端字节序写入载荷。 */
static void BMS_Can_PutU32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value & 0xFFUL);
    destination[1] = (uint8_t)((value >> 8U) & 0xFFUL);
    destination[2] = (uint8_t)((value >> 16U) & 0xFFUL);
    destination[3] = (uint8_t)((value >> 24U) & 0xFFUL);
}

/* 从四个小端载荷字节恢复无符号 32 位字段。 */
static uint32_t BMS_Can_GetU32(const uint8_t *source)
{
    return (uint32_t)source[0] |
        ((uint32_t)source[1] << 8U) |
        ((uint32_t)source[2] << 16U) |
        ((uint32_t)source[3] << 24U);
}

/* 把电芯毫伏值压缩为协议字节，超界时饱和。 */
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

/* 填入标准帧 ID 和固定 DLC，并清空全部载荷字节。 */
static void BMS_Can_InitFrame(BMS_CanFrame_t *frame, uint16_t id)
{
    frame->standard_id = id;
    frame->dlc = 8U;
    (void)memset(frame->data, 0, sizeof(frame->data));
    frame->received_ms = 0UL;
}

/* 把多位 owner 的只读快照编码为六帧诊断报文，不转移安全写权限。 */
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

    /* 0x180：运行分类、effective FET、技术就绪、事务状态与低 16-bit sample 序号。 */
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

    /* 0x181：pack mV、10 mA 分辨率有符号电流、SOC permille 与剩余 mAh。 */
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

    /* 0x182：最低/最高单体、0.1 °C 温度及 1-based 极值电芯编号。 */
    BMS_Can_InitFrame(&frames[2], 0x182U);
    BMS_Can_PutU16(&frames[2].data[0], minimum);
    BMS_Can_PutU16(&frames[2].data[2], maximum);
    BMS_Can_PutU16(&frames[2].data[4],
                   (uint16_t)measurement->temperature_decic);
    frames[2].data[6] = (uint8_t)(min_index + 1U);
    frames[2].data[7] = (uint8_t)(max_index + 1U);

    /* 0x183：完整 active 与 latched fault bitmap，便于区分当前条件和历史锁存。 */
    BMS_Can_InitFrame(&frames[3], 0x183U);
    BMS_Can_PutU32(&frames[3].data[0], active);
    BMS_Can_PutU32(&frames[3].data[4], latched);

    /* 0x184：cell1..7 以 (mV-2000)/10 压缩，末字节携带 sequence 低位。 */
    BMS_Can_InitFrame(&frames[4], 0x184U);
    for (index = 0U; index < 7U; ++index)
    {
        frames[4].data[index] = BMS_Can_EncodeCellMv(
            measurement->cell_voltage_mv[index]);
    }
    frames[4].data[7] = (uint8_t)measurement->sample_sequence;

    /* 0x185：cell8..13 同样压缩，并携带 AFE generation/sequence 低位。 */
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

/* 校验服务帧、时效与测量身份，只生成待 Protect 接纳的请求。 */
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

    /*
     * 0x280 是受限 service request，不是远程 MOS 命令。这里仅验证标准 ID、固定
     * magic/command、source 枚举、保留位、接收时效与非零 request_id，并把当前
     * measurement identity 写入 request；最终是否接受仍由 ProtectTask 决定。
     */
    if ((frame == NULL) || (policy == NULL) || (identity == NULL) ||
        (request == NULL) || (frame->standard_id != policy->can.service_rx_id) ||
        (frame->standard_id > 0x7FFUL) || (frame->dlc != 8U) ||
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

/* 保存经校验的 CAN 策略并清空协议诊断计数。 */
void BMS_Can_Init(const BMS_Policy_t *policy)
{
    s_policy = BMS_Policy_Validate(policy) ? policy : NULL;
    (void)memset(&s_diagnostics, 0, sizeof(s_diagnostics));
}

/* 抓取最新只读诊断快照并构造六帧周期报文。 */
uint8_t BMS_Can_BuildPeriodicFrames(
    uint32_t now_ms,
    BMS_CanFrame_t frames[BMS_CAN_TX_FRAME_COUNT])
{
    BMS_DataSnapshot_t measurement;
    BMS_StateSafetySnapshot_t state;
    BMS_ProtectSafetySnapshot_t protect;
    BMS_RecoverySnapshot_t recovery;
    BMS_FetManagerSnapshot_t fet;
    uint8_t count;

    if ((frames == NULL) || (s_policy == NULL) ||
        !BMS_Data_GetSnapshot(&measurement, now_ms))
    {
        return 0U;
    }
    state = BMS_State_GetSafetySnapshot();
    protect = BMS_Protect_GetSafetySnapshot();
    recovery = BMS_Recovery_GetSnapshot();
    fet = BMS_FetManager_GetSnapshot();
    count = BMS_Can_BuildTxFrames(
        &measurement, &state, &protect, &recovery, &fet, frames);
    BMS_Can_Increment(&s_diagnostics.tx_cycle_count);
    return count;
}

/* 解码并暂存一帧服务请求；最终授权和执行由 Protect 后续应答决定。 */
bool BMS_Can_RxProcess(const BMS_CanFrame_t *frame,
                       uint32_t received_ms,
                       uint32_t now_ms)
{
    BMS_DataIdentity_t identity;
    BMS_StateSafetySnapshot_t state;
    BMS_ServiceResetRequest_t request;

    if ((s_policy == NULL) || !BMS_Data_GetIdentity(&identity))
    {
        BMS_Can_Increment(&s_diagnostics.rx_invalid_count);
        return false;
    }
    state = BMS_State_GetSafetySnapshot();
    if (!BMS_Can_DecodeServiceReset(
            frame, received_ms, now_ms, s_policy, &identity,
            state.publication_revision, &request))
    {
        BMS_Can_Increment(&s_diagnostics.rx_invalid_count);
        return false;
    }
    BMS_Can_Increment(&s_diagnostics.rx_valid_count);
    if (BMS_Protect_SubmitServiceResetRequest(&request))
    {
        BMS_Can_Increment(&s_diagnostics.service_request_count);
        return true;
    }
    else
    {
        BMS_Can_Increment(&s_diagnostics.service_reject_count);
        return false;
    }
}

/* 对指定 CAN 目标事件饱和递增一次诊断计数。 */
void BMS_Can_RecordDiagnostic(BMS_CanDiagnosticEvent_t event)
{
    BMS_Can_RecordDiagnosticCount(event, 1UL);
}

/* 对指定 CAN 目标事件饱和累计多次诊断计数。 */
void BMS_Can_RecordDiagnosticCount(BMS_CanDiagnosticEvent_t event,
                                   uint32_t count)
{
    uint32_t *counter;

    counter = NULL;
    switch (event)
    {
        case BMS_CAN_DIAG_TX_ENQUEUED:
            counter = &s_diagnostics.tx_enqueued_count;
            break;
        case BMS_CAN_DIAG_TX_QUEUE_DROP:
            counter = &s_diagnostics.tx_drop_count;
            break;
        case BMS_CAN_DIAG_TARGET_INIT_FAILURE:
            counter = &s_diagnostics.target_init_failure_count;
            break;
        case BMS_CAN_DIAG_TARGET_TX:
            counter = &s_diagnostics.target_tx_count;
            break;
        case BMS_CAN_DIAG_TARGET_TX_DROP:
            counter = &s_diagnostics.target_tx_drop_count;
            break;
        case BMS_CAN_DIAG_TARGET_RX_FIFO_OVERRUN:
            counter = &s_diagnostics.target_rx_fifo_overrun_count;
            break;
        case BMS_CAN_DIAG_TARGET_RX_QUEUE_DROP:
            counter = &s_diagnostics.target_rx_queue_drop_count;
            break;
        case BMS_CAN_DIAG_TARGET_BUS_OFF_RECOVERY:
            counter = &s_diagnostics.target_bus_off_recovery_count;
            break;
        default:
            break;
    }
    if (counter != NULL)
    {
        BMS_Runtime_CriticalEnter();
        if ((UINT32_MAX - *counter) < count)
        {
            *counter = UINT32_MAX;
        }
        else
        {
            *counter += count;
        }
        BMS_Runtime_CriticalExit();
    }
}

/* 在短临界区复制 CAN 协议与目标计数的一致快照。 */
BMS_CanDiagnostics_t BMS_Can_GetDiagnostics(void)
{
    BMS_CanDiagnostics_t snapshot;

    BMS_Runtime_CriticalEnter();
    snapshot = s_diagnostics;
    BMS_Runtime_CriticalExit();
    return snapshot;
}
