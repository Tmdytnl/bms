#ifndef BSP_GPIO_H
#define BSP_GPIO_H

#include <stdbool.h>

void BSP_GPIO_Init(void);

/* Open-drain line contract: release means high-impedance, not drive-high. */
void BSP_I2C_SCL_DriveLow(void);
void BSP_I2C_SCL_Release(void);
bool BSP_I2C_SCL_Read(void);
void BSP_I2C_SDA_DriveLow(void);
void BSP_I2C_SDA_Release(void);
bool BSP_I2C_SDA_Read(void);

/* Emit one bounded PA8 low-to-high wake edge for the BQ/TS1 path. */
bool BSP_AFE_WakePulse(void);

#endif /* BSP_GPIO_H */
