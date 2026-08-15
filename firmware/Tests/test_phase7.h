#ifndef TEST_PHASE7_H
#define TEST_PHASE7_H

#include <stdint.h>

extern volatile uint32_t g_p7_probe;

uint32_t Test_Phase7_ProtectLogic(void);
uint32_t Test_Phase7_CcQueue(void);
uint32_t Test_Phase7_Xready(void);

#endif /* TEST_PHASE7_H */
