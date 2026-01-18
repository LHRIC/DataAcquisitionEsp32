#pragma once
#include "driver/i2c.h"
#include "driver/i2c_master.h"

#define I2C_SCL_PIN (gpio_num_t)(0)
#define I2C_SDA_PIN (gpio_num_t)(1)

extern i2c_master_bus_handle_t i2c_bus_handle;
void i2c_init();
