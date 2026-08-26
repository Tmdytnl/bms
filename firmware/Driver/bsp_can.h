#ifndef BSP_CAN_H
#define BSP_CAN_H

#include <stdbool.h>
#include <stdint.h>

#define BSP_CAN_RX_LOGICAL_PRIORITY              (7U)

typedef struct
{
    uint32_t id;
    bool extended;
    uint8_t dlc;
    uint8_t data[8];
} BSP_CanFrame_t;

typedef enum
{
    BSP_CAN_TX_ACCEPTED = 0,
    BSP_CAN_TX_NO_MAILBOX,
    BSP_CAN_TX_NOT_READY,
    BSP_CAN_TX_INVALID
} BSP_CanTxResult_t;

/*
 * 配置 PA11/PA12 与 500 kbit/s bxCAN，filter 只把指定 standard data-frame
 * service ID 路由到 FIFO0。RX interrupt 保持 mask，直到 scheduler 启动后由任务
 * 显式 enable，避免 FromISR 在 FreeRTOS port 初始化前运行。
 */
bool BSP_CAN_Init500K(uint16_t service_rx_id);
bool BSP_CAN_EnableRxInterrupt(void);
bool BSP_CAN_IsInitialized(void);

BSP_CanTxResult_t BSP_CAN_TryTransmit(const BSP_CanFrame_t *frame);
bool BSP_CAN_ReceivePending(void);
bool BSP_CAN_Receive(BSP_CanFrame_t *frame);
bool BSP_CAN_IsRxFifoOverrun(void);
void BSP_CAN_ClearRxFifoOverrun(void);

/* bus-off recovery 只重启 CAN peripheral/filter，绝不改变本地 BMS protection state。 */
bool BSP_CAN_IsBusOff(void);
bool BSP_CAN_Recover(void);

#endif /* BSP_CAN_H：include guard */
