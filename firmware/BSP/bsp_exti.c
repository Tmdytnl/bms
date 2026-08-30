#include "bsp_exti.h"

#include "bsp_board_config.h"

#include "misc.h"          /* SPL 的 NVIC_StructInit / NVIC_Init */
#include "stm32f10x.h"
#include "stm32f10x_exti.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

/* PB1/EXTI1；project pin map 中 port id 1 表示 GPIOB。 */
BSP_BUILD_ASSERT(BSP_BOARD_ALERT_PIN == 1U,
                 alert_pin_is_pb1);

static bool s_exti_initialized;

bool BSP_ALERT_EXTI_Init(void)
{
    EXTI_InitTypeDef exti_config;
    GPIO_InitTypeDef gpio_config;
    NVIC_InitTypeDef nvic_config;

    /* 配置顺序：开时钟 -> 输入/路由 -> 清 pending 并 unmask -> 最后开启 NVIC。 */
    /* 为 PB1 打开 GPIOB clock。 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    /* EXTI line routing 依赖 AFIO clock。 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);

    /* PB1 floating input；外部电路提供设计所需 pull-up/down。 */
    GPIO_StructInit(&gpio_config);
    gpio_config.GPIO_Pin = GPIO_Pin_1;
    gpio_config.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOB, &gpio_config);

    /* EXTI1 只捕获 rising edge。 */
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource1);
    EXTI_StructInit(&exti_config);
    exti_config.EXTI_Line = EXTI_Line1;
    exti_config.EXTI_Mode = EXTI_Mode_Interrupt;
    exti_config.EXTI_Trigger = EXTI_Trigger_Rising;
    exti_config.EXTI_LineCmd = ENABLE;
    /*
     * unmask 前清 STM32 stale pending；随后 ProtectTask 直接读 PB1，因此 startup
     * 期间已经为高的 AFE ALERT 仍会转成任务工作。
     */
    EXTI_ClearITPendingBit(EXTI_Line1);
    EXTI_Init(&exti_config);

    /*
     * C-02：logical priority 6（raw 0x60）满足 max-syscall 5 的 FromISR 约束。
     * APL startup/composition 已在创建运行时前设置 PriorityGroup_4；APL 在
     * scheduler 启动后的任务上下文调用本函数，因此 ISR handoff 基础已就绪。
     */
    nvic_config.NVIC_IRQChannel = EXTI1_IRQn;
    nvic_config.NVIC_IRQChannelPreemptionPriority =
        (uint8_t)(BSP_EXTI1_LOGICAL_PRIORITY & 0x0FU);
    nvic_config.NVIC_IRQChannelSubPriority = 0;
    nvic_config.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic_config);

    /* 只有 GPIO/EXTI/NVIC 全部配置后才发布 initialized，避免半配置被误用。 */
    s_exti_initialized = true;
    return true;
}

bool BSP_ALERT_EXTI_IsInitialized(void)
{
    return s_exti_initialized;
}

bool BSP_ALERT_PinActive(void)
{
    /* 直接 PB1 level readback。 */
    return (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_1) != Bit_RESET);
}

bool BSP_ALERT_EXTI_IsPending(void)
{
    return EXTI_GetITStatus(EXTI_Line1) != RESET;
}

void BSP_ALERT_EXTI_ClearPending(void)
{
    EXTI_ClearITPendingBit(EXTI_Line1);
}
