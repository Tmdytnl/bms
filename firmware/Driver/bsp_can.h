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

/* Configure PA11/PA12 and bxCAN at 500 kbit/s. The exact standard data-frame
 * service ID is routed to FIFO0. RX interrupts remain masked until a task
 * calls BSP_CAN_EnableRxInterrupt after the scheduler has started. */
bool BSP_CAN_Init500K(uint16_t service_rx_id);
bool BSP_CAN_EnableRxInterrupt(void);
bool BSP_CAN_IsInitialized(void);

BSP_CanTxResult_t BSP_CAN_TryTransmit(const BSP_CanFrame_t *frame);
bool BSP_CAN_ReceivePending(void);
bool BSP_CAN_Receive(BSP_CanFrame_t *frame);
bool BSP_CAN_IsRxFifoOverrun(void);
void BSP_CAN_ClearRxFifoOverrun(void);

/* CAN remains diagnostic-only in SIM_POLICY_V1. Recovery restarts only the
 * CAN peripheral/filter and never changes local BMS protection state. */
bool BSP_CAN_IsBusOff(void);
bool BSP_CAN_Recover(void);

#endif /* BSP_CAN_H */
