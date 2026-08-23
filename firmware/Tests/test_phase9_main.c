#include <stdint.h>

uint32_t Test_Phase9(void);
uint32_t Test_Continuation(void);

volatile uint32_t g_phase9_test_failures;
volatile uint32_t g_phase9_test_completed;
volatile uint32_t g_phase9_test_probe;

void SystemInit(void)
{
}

int main(void)
{
    g_phase9_test_probe = 1UL;
    g_phase9_test_failures = Test_Phase9() + Test_Continuation();
    g_phase9_test_probe = 2UL;
    g_phase9_test_completed = 1UL;
    g_phase9_test_probe = 3UL;
    __breakpoint(0);
    while (1)
    {
    }
}
