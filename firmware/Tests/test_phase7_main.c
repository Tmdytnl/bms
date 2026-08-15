#include "test_phase7.h"

volatile uint32_t g_phase7_test_failures;
volatile uint32_t g_phase7_test_completed;
volatile uint32_t g_p7_protect_failures;
volatile uint32_t g_p7_cc_failures;
volatile uint32_t g_p7_xready_failures;
volatile uint32_t g_p7_probe;

void SystemInit(void)
{
}

int main(void)
{
    g_p7_probe = 1UL;
    g_p7_protect_failures = Test_Phase7_ProtectLogic();
    g_p7_probe = 2UL;
    g_p7_cc_failures = Test_Phase7_CcQueue();
    g_p7_probe = 3UL;
    g_p7_xready_failures = Test_Phase7_Xready();
    g_p7_probe = 4UL;
    g_phase7_test_failures = g_p7_protect_failures + g_p7_cc_failures +
                             g_p7_xready_failures;
    g_phase7_test_completed = 1UL;
    g_p7_probe = 5UL;
    __breakpoint(0);

    while (1)
    {
    }
}
