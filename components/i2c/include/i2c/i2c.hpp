#pragma once
#include "driver/i2c.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

// The new i2c driver will break down on some ESPs when used with the GPS.
// This is likely because the ublox gps uses clock stretching which the new
// driver doesn't know how to handle correctly. Nonetheless, I'm keeping this
// here just in case it can be used later.
// #define I2C_NEW_DRIVER

#define I2C_PORT I2C_NUM_0
#define I2C_TIMEOUT -1

#define I2C_SCL_PIN (gpio_num_t)(35)
#define I2C_SDA_PIN (gpio_num_t)(36)

#ifndef I2C_NEW_DRIVER
extern i2c_master_bus_handle_t i2c_bus_handle;
#endif

typedef union
{
    uint8_t device_address;
    i2c_master_dev_handle_t handle;
} i2c_device;

esp_err_t i2c_init();
esp_err_t i2c_add_device(uint8_t device_address, i2c_device *device);
esp_err_t i2c_read_register(i2c_device *device, uint8_t device_register,
                            uint8_t *buffer, uint8_t n);
