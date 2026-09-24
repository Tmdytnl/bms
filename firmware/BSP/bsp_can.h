#ifndef BSP_CAN_H
#define BSP_CAN_H

#include <stdbool.h>
#include <stdint.h>

#define BSP_CAN_RX_LOGICAL_PRIORITY              (7U)

typedef struct
{
    uint32_t id;    /* 硬件 CAN 帧标识符；当前服务协议要求 11-bit。 */
    bool extended;  /* true 表示扩展帧，当前接收策略会拒绝。 */
    uint8_t dlc;    /* 有效载荷字节数，最大 8。 */
    uint8_t data[8];/* 硬件报文载荷，不含 ID 或时间戳。 */
} BSP_CanFrame_t;

typedef enum
{
    BSP_CAN_TX_ACCEPTED = 0, /* 硬件发送邮箱已接纳此帧。 */
    BSP_CAN_TX_NO_MAILBOX,    /* 三个发送邮箱暂时都不可用。 */
    BSP_CAN_TX_NOT_READY,     /* CAN 外设尚未完成初始化。 */
    BSP_CAN_TX_INVALID        /* 帧 ID、长度或参数不合法。 */
} BSP_CanTxResult_t;

/*
 * 配置 PA11/PA12 与 500 kbit/s bxCAN，filter 只把指定 standard data-frame
 * service ID 路由到 FIFO0。RX interrupt 保持 mask，由上层在 IRQ handoff 环境
 * 就绪后显式 enable，避免启动早期 FIFO 事件进入未就绪的软件路径。
 */
bool BSP_CAN_Init500K(uint16_t service_rx_id);
/* 仅在 CAN 已初始化后开启 FIFO0 接收中断。 */
bool BSP_CAN_EnableRxInterrupt(void);
/* 读取 CAN 外设和过滤器配置完成标志。 */
bool BSP_CAN_IsInitialized(void);

/* 尝试占用可用发送邮箱，不阻塞等待总线发送完成。 */
BSP_CanTxResult_t BSP_CAN_TryTransmit(const BSP_CanFrame_t *frame);
/* 读取 RX FIFO0 的待收报文数量。 */
bool BSP_CAN_ReceivePending(void);
/* 从 RX FIFO0 取出一帧硬件报文并释放 FIFO 邮箱。 */
bool BSP_CAN_Receive(BSP_CanFrame_t *frame);
/* 检查 RX FIFO0 是否曾溢出，供 APL 记录诊断。 */
bool BSP_CAN_IsRxFifoOverrun(void);
/* 清除 RX FIFO0 的硬件溢出标志。 */
void BSP_CAN_ClearRxFifoOverrun(void);

/* bus-off recovery 只重启 CAN peripheral/filter，不解释或修改上层协议状态。 */
bool BSP_CAN_IsBusOff(void);
/* 按现有板级配置重新初始化 CAN 外设。 */
bool BSP_CAN_Recover(void);

#endif /* BSP_CAN_H：include guard */
