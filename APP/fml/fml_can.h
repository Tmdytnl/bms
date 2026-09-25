#ifndef FML_CAN_H
#define FML_CAN_H

#include <stdbool.h>
#include <stdint.h>

#include "fml_policy.h"

#define BMS_CAN_PROTOCOL_VERSION                  (1U)
#define BMS_CAN_TX_FRAME_COUNT                    (6U)
#define BMS_CAN_SERVICE_MAGIC                     (0xA5U)
#define BMS_CAN_SERVICE_RESET_COMMAND             (0x01U)

typedef struct
{
    uint32_t standard_id;              /* 11-bit CAN 标准帧 ID；不接纳扩展帧。 */
    uint8_t dlc; /* 本帧有效载荷字节数，当前协议帧固定为 8。 */
    uint8_t data[8]; /* CAN 载荷字节；按显式协议编码，不复制 C struct。 */
    uint32_t received_ms;             /* APL ISR handoff 已换算的毫秒时刻。 */
} BMS_CanFrame_t;

typedef enum
{
    BMS_CAN_DIAG_TX_ENQUEUED = 0, /* 周期帧进入 APL 软件发送队列。 */
    BMS_CAN_DIAG_TX_QUEUE_DROP,    /* 发送队列满而丢失诊断帧。 */
    BMS_CAN_DIAG_TARGET_INIT_FAILURE, /* CAN 硬件初始化失败。 */
    BMS_CAN_DIAG_TARGET_TX,        /* 硬件邮箱接纳发送帧。 */
    BMS_CAN_DIAG_TARGET_TX_DROP,   /* 目标发送服务丢失帧。 */
    BMS_CAN_DIAG_TARGET_RX_FIFO_OVERRUN, /* 硬件接收 FIFO 溢出。 */
    BMS_CAN_DIAG_TARGET_RX_QUEUE_DROP, /* APL 接收队列满。 */
    BMS_CAN_DIAG_TARGET_BUS_OFF_RECOVERY /* bus-off 后重新初始化外设。 */
} BMS_CanDiagnosticEvent_t;

typedef struct
{
    uint32_t tx_cycle_count;       /* 完成一次六帧编码尝试。 */
    uint32_t tx_enqueued_count;    /* 成功进入软件 TX queue 的帧数。 */
    uint32_t tx_drop_count;        /* 软件队列满导致的诊断帧丢弃。 */
    uint32_t rx_valid_count;       /* 通过 0x280 格式/时效/identity 解码。 */
    uint32_t rx_invalid_count;     /* 非法、过期或无法绑定当前数据的接收帧。 */
    uint32_t service_request_count;/* Protect 已暂存待审核服务请求的次数；不代表最终执行。 */
    uint32_t service_reject_count; /* decode 合法但 owner 因当前状态拒绝。 */
    uint32_t target_init_failure_count; /* CAN 目标外设初始化失败次数。 */
    uint32_t target_tx_count;             /* 硬件邮箱接受的发送帧数。 */
    uint32_t target_tx_drop_count;        /* 目标硬件发送服务丢帧数。 */
    uint32_t target_rx_fifo_overrun_count;/* bxCAN RX FIFO 溢出次数。 */
    uint32_t target_rx_queue_drop_count;  /* APL RX 队列满导致的丢帧数。 */
    uint32_t target_bus_off_recovery_count; /* bus-off 后重新初始化次数。 */
} BMS_CanDiagnostics_t;

/* 绑定不可变协议策略并清空诊断；策略非法时不启用服务处理。 */
void FML_Can_Init(const BMS_Policy_t *policy);
/* 读取当前诊断投影并编码六帧；返回实际形成的帧数。 */
uint8_t FML_Can_BuildPeriodicFrames(
    uint32_t now_ms,
    BMS_CanFrame_t frames[BMS_CAN_TX_FRAME_COUNT]);
/* 解码并向 Protect 提交待审核请求；true 仅表示已暂存，不表示重置已执行。 */
bool FML_Can_RxProcess(const BMS_CanFrame_t *frame,
                       uint32_t received_ms,
                       uint32_t now_ms);
/* 记录一次来自 APL 目标绑定的 CAN 诊断事件。 */
void FML_Can_RecordDiagnostic(BMS_CanDiagnosticEvent_t event);
/* 一次记录指定次数的目标 CAN 诊断事件。 */
void FML_Can_RecordDiagnosticCount(BMS_CanDiagnosticEvent_t event,
                                   uint32_t count);
/* 返回 CAN 协议与目标诊断的只读一致快照。 */
BMS_CanDiagnostics_t FML_Can_GetDiagnostics(void);

#endif /* FML_CAN_H：头文件防重复包含 */
