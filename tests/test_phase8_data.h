#ifndef TEST_PHASE8_DATA_H
#define TEST_PHASE8_DATA_H

#include <stdint.h>

extern volatile uint32_t g_phase8_data_test_failures;
extern volatile uint32_t g_phase8_data_test_completed;

uint32_t Test_Phase8_Data(void);

#endif /* TEST_PHASE8_DATA_H */
