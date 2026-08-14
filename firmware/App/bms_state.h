#ifndef BMS_STATE_H
#define BMS_STATE_H

#include "bms_build_assert.h"

/* BMS application states only. BQ SHIP/NORMAL are AFE device modes. */
typedef enum
{
    BMS_STATE_INIT = 0,
    BMS_STATE_STANDBY = 1,
    BMS_STATE_CHARGE = 2,
    BMS_STATE_DISCHARGE = 3,
    BMS_STATE_FAULT = 4,
    BMS_STATE_COUNT = 5
} BMS_State_t;

BMS_BUILD_ASSERT(BMS_STATE_INIT == 0,
                 state_init_value_is_stable);
BMS_BUILD_ASSERT(BMS_STATE_FAULT == 4,
                 state_fault_value_is_stable);
BMS_BUILD_ASSERT(BMS_STATE_COUNT == 5,
                 state_count_is_five);

#endif /* BMS_STATE_H */
