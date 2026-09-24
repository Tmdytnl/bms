#ifndef BSP_GPIO_H
#define BSP_GPIO_H

#include <stdbool.h>

/* 建立软件 I2C 与 AFE wake 引脚的安全初始电平。 */
void BSP_GPIO_Init(void);

/* open-drain contract：release 表示 high-impedance，绝不是主动 drive-high。 */
/* 主动拉低 SCL；调用者负责总线时序。 */
void BSP_I2C_SCL_DriveLow(void);
/* 释放 SCL，由上拉形成高电平，可能仍被从机拉低。 */
void BSP_I2C_SCL_Release(void);
/* 读取 SCL 实际电平，用于判断时钟拉伸。 */
bool BSP_I2C_SCL_Read(void);
/* 主动拉低 SDA；调用者负责总线时序。 */
void BSP_I2C_SDA_DriveLow(void);
/* 释放 SDA，由上拉形成高电平，可能仍被从机拉低。 */
void BSP_I2C_SDA_Release(void);
/* 读取 SDA 实际电平，用于数据位或 ACK 采样。 */
bool BSP_I2C_SDA_Read(void);

/* 为 BQ/TS1 path 产生一次有界 PA8 low→high WAKE edge。 */
bool BSP_AFE_WakePulse(void);

#endif /* BSP_GPIO_H：include guard */
