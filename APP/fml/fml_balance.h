#ifndef FML_BALANCE_H
#define FML_BALANCE_H

#include <stdbool.h>
#include <stdint.h>

#include "fml_policy.h"
#include "bsp_bq76940.h"

typedef struct
{
    uint16_t requested_bitmap;        /* engine 基于捕获证据选择的目标电芯。 */
    uint16_t confirmed_bitmap;        /* CELLBAL1..3 readback 解码结果。 */
    uint32_t publication_revision;    /* 每次任务级服务结果发布递增。 */
    uint32_t evaluated_sample_sequence; /* 选择依据的完整 measurement。 */
    uint32_t evaluated_afe_generation;  /* 选择依据的 AFE 生命周期。 */
    uint32_t confirmed_afe_generation;  /* readback 时 Protect 报告的 generation。 */
    BQ76940_Status_t last_transport_status; /* 最近写/回读结果。 */
    bool register_state_confirmed;    /* 三个 CELLBAL 字节都与目标一致。 */
    bool confirmed_all_off;           /* Recovery 可消费的“硬件均衡全关”证据。 */
} BMS_BalanceSnapshot_t;

/* 绑定 AFE、策略及启动期 all-off 证据；不直接触发运行期均衡。 */
void FML_Balance_Init(BQ76940_t *device,
                      const BMS_Policy_t *policy,
                      bool startup_all_off_confirmed);
/* BalanceTask 唯一调用；内部获取 I2C mutex，写后三寄存器回读并复核全部 revision。 */
void FML_Balance_RunOnce(uint32_t now_ms);
/* 一次复制均衡请求、回读结果和测量身份，不取得 CELLBAL 写权限。 */
BMS_BalanceSnapshot_t FML_Balance_GetSnapshot(void);

#endif /* FML_BALANCE_H：头文件防重复包含 */
