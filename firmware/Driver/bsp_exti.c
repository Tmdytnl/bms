#include "bsp_exti.h"

#include "bms_build_assert.h"
#include "bms_config.h"

#include "misc.h"          /* NVIC_StructInit / NVIC_Init (SPL) */
#include "stm32f10x.h"
#include "stm32f10x_exti.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

/*
 * PB1 / EXTI1. Port id 1 = GPIOB in the project pin map (bms_config.h).
 */
BMS_BUILD_ASSERT(BMS_AFE_ALERT_PORT_ID == BMS_GPIO_PORT_B_ID,
                 alert_pin_is_on_port_b);
BMS_BUILD_ASSERT(BMS_AFE_ALERT_PIN == 1U,
                 alert_pin_is_pb1);

static bool s_exti_initialized;

bool BSP_ALERT_EXTI_Init(void)
{
    EXTI_InitTypeDef exti_config;
    GPIO_InitTypeDef gpio_config;
    NVIC_InitTypeDef nvic_config;

    /* GPIOB clock (PB1). */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    /* AFIO clock is required for EXTI line routing. */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);

    /* PB1 input (floating; board provides pull-up/pull-down as designed). */
    GPIO_StructInit(&gpio_config);
    gpio_config.GPIO_Pin = GPIO_Pin_1;
    gpio_config.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOB, &gpio_config);

    /* EXTI1 rising edge. */
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource1);
    EXTI_StructInit(&exti_config);
    exti_config.EXTI_Line = EXTI_Line1;
    exti_config.EXTI_Mode = EXTI_Mode_Interrupt;
    exti_config.EXTI_Trigger = EXTI_Trigger_Rising;
    exti_config.EXTI_LineCmd = ENABLE;
    EXTI_Init(&exti_config);

    /*
     * C-02: logical priority 6 (raw 0x60) >= max-syscall 5, so FromISR
     * calls in the handler are legal. PriorityGroup_4 must already be set
     * by the Phase 6 main flow before the scheduler starts.
     */
    nvic_config.NVIC_IRQChannel = EXTI1_IRQn;
    nvic_config.NVIC_IRQChannelPreemptionPriority =
        (uint8_t)(BSP_EXTI1_LOGICAL_PRIORITY & 0x0FU);
    nvic_config.NVIC_IRQChannelSubPriority = 0;
    nvic_config.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic_config);

    s_exti_initialized = true;
    return true;
}

bool BSP_ALERT_EXTI_IsInitialized(void)
{
    return s_exti_initialized;
}

bool BSP_ALERT_PinActive(void)
{
    /* PB1 readback. */
    return (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_1) != Bit_RESET);
}
