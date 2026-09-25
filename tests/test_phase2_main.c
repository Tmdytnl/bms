#include "test_phase2.h"

volatile uint32_t g_phase2_test_failures;
volatile uint32_t g_phase2_test_completed;

/* production startup 在 __main 前调用的 test-only no-op。 */
void SystemInit(void)
{
}

int main(void)
{
    g_phase2_test_failures = Test_Phase2_Crc();
    g_phase2_test_failures += Test_Phase2_SoftI2C();
    g_phase2_test_completed = 1UL;
    __breakpoint(0);

    while (1)
    {
    }
}
