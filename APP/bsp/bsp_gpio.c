#include "bsp_gpio.h"

#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

#define BSP_I2C_SCL_PIN    GPIO_Pin_8
#define BSP_I2C_SDA_PIN    GPIO_Pin_9
#define BSP_AFE_WAKE_PIN   GPIO_Pin_8

/* 建立软件 I2C 开漏引脚和 AFE wake 引脚的板级初始状态。 */
void BSP_GPIO_Init(void)
{
    /* 当前访问的 GPIO 端口。 */
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB, ENABLE);

    /* 先置 ODR 再启用 open-drain，使两根总线从 release 状态开始。 */
    GPIO_SetBits(GPIOB, BSP_I2C_SCL_PIN | BSP_I2C_SDA_PIN);
    gpio.GPIO_Pin = BSP_I2C_SCL_PIN | BSP_I2C_SDA_PIN;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    gpio.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_Init(GPIOB, &gpio);

    GPIO_ResetBits(GPIOA, BSP_AFE_WAKE_PIN);
    gpio.GPIO_Pin = BSP_AFE_WAKE_PIN;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOA, &gpio);
}

/* 主动拉低软件 I2C 的 SCL 开漏线。 */
void BSP_I2C_SCL_DriveLow(void)
{
    GPIO_ResetBits(GPIOB, BSP_I2C_SCL_PIN);
}

/* 释放软件 I2C 的 SCL 开漏线，由外部上拉形成高电平。 */
void BSP_I2C_SCL_Release(void)
{
    GPIO_SetBits(GPIOB, BSP_I2C_SCL_PIN);
}

/* 读取软件 I2C 的 SCL 实际输入电平。 */
bool BSP_I2C_SCL_Read(void)
{
    return GPIO_ReadInputDataBit(GPIOB, BSP_I2C_SCL_PIN) == Bit_SET;
}

/* 主动拉低软件 I2C 的 SDA 开漏线。 */
void BSP_I2C_SDA_DriveLow(void)
{
    GPIO_ResetBits(GPIOB, BSP_I2C_SDA_PIN);
}

/* 释放软件 I2C 的 SDA 开漏线，由外部上拉形成高电平。 */
void BSP_I2C_SDA_Release(void)
{
    GPIO_SetBits(GPIOB, BSP_I2C_SDA_PIN);
}

/* 读取软件 I2C 的 SDA 实际输入电平。 */
bool BSP_I2C_SDA_Read(void)
{
    return GPIO_ReadInputDataBit(GPIOB, BSP_I2C_SDA_PIN) == Bit_SET;
}

/* 在板级 AFE wake 引脚上产生唤醒脉冲。 */
bool BSP_AFE_WakePulse(void)
{
    GPIO_ResetBits(GPIOA, BSP_AFE_WAKE_PIN);
    GPIO_SetBits(GPIOA, BSP_AFE_WAKE_PIN);
    return true;
}
