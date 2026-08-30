#ifndef BSP_GPIO_H
#define BSP_GPIO_H

#include <stdbool.h>

void BSP_GPIO_Init(void);

/* open-drain contract：release 表示 high-impedance，绝不是主动 drive-high。 */
void BSP_I2C_SCL_DriveLow(void);
void BSP_I2C_SCL_Release(void);
bool BSP_I2C_SCL_Read(void);
void BSP_I2C_SDA_DriveLow(void);
void BSP_I2C_SDA_Release(void);
bool BSP_I2C_SDA_Read(void);

/* 为 BQ/TS1 path 产生一次有界 PA8 low→high WAKE edge。 */
bool BSP_AFE_WakePulse(void);

#endif /* BSP_GPIO_H：include guard */
