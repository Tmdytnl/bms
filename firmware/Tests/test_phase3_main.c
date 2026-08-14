#include "test_phase3.h"

volatile uint32_t g_phase3_test_failures;
volatile uint32_t g_phase3_test_completed;

void SystemInit(void)
{
}

int main(void)
{
    g_phase3_test_failures = Test_Phase3_Transport();
    g_phase3_test_failures += Test_Phase3_Decode();
    g_phase3_test_completed = 1UL;
    __breakpoint(0);

    while (1)
    {
    }
}
