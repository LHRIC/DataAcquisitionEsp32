#include "i2c/i2c.hpp"
#include "driver/i2c.h"
#include "driver/i2c_master.h"
#include "freertos/projdefs.h"
#include "hal/gpio_types.h"
#include "hal/i2c_types.h"
#include "soc/clk_tree_defs.h"

#ifndef I2C_NEW_DRIVER
i2c_master_bus_handle_t i2c_bus_handle;
#endif

esp_err_t i2c_init()
{
    esp_err_t error;
#ifdef I2C_NEW_DRIVER
    i2c_master_bus_config_t bus_config;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.i2c_port = -1; // Auto Select I2C Port
    bus_config.sda_io_num = I2C_SDA_PIN;
    bus_config.scl_io_num = I2C_SCL_PIN;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;
    bus_config.trans_queue_depth = 0;

    error = i2c_new_master_bus(&bus_config, &i2c_bus_handle);
    if (error)
    {
        return error;
    }
#else
    i2c_config_t config;

    config.mode = I2C_MODE_MASTER;
    config.sda_io_num = I2C_SDA_PIN;
    config.scl_io_num = I2C_SCL_PIN;
    config.sda_pullup_en = GPIO_PULLUP_ENABLE;
    config.scl_pullup_en = GPIO_PULLUP_ENABLE;
    config.master.clk_speed = 400000;

    error = i2c_param_config(I2C_PORT, &config);
    if (error)
        return error;
    error = i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (error)
        return error;
#endif
    return ESP_OK;
}

esp_err_t i2c_add_device(uint8_t device_address, i2c_device *device)
{
    esp_err_t error;
#ifdef I2C_NEW_DRIVER
    i2c_device_config_t device_config;
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = device_address;
    device_config.scl_speed_hz = 400000;

    error = i2c_master_bus_add_device(i2c_bus_handle, &device_config,
                                      &device->handle);
    if (error)
        return error;
#else
    device->device_address = device_address;
#endif
    return ESP_OK;
}

esp_err_t i2c_read_register(i2c_device *device, uint8_t device_register,
                            uint8_t *buffer, uint8_t n)
{
    esp_err_t error;
#ifdef I2C_NEW_DRIVER
    uint8_t send_buffer[1] = {device_register};
    error = i2c_master_transmit_receive(device->handle, send_buffer,
                                        sizeof(send_buffer), buffer, n,
                                        I2C_TIMEOUT);
    if (error)
        return error;
#else
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (device->device_address << 1) | I2C_MASTER_WRITE,
                          true);
    i2c_master_write_byte(cmd, device_register, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (device->device_address << 1) | I2C_MASTER_READ,
                          true);
    if (n > 1)
    {
        i2c_master_read(cmd, buffer, n - 1, I2C_MASTER_ACK);
    }

    i2c_master_read_byte(cmd, buffer + n - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    error = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(I2C_TIMEOUT));
    i2c_cmd_link_delete(cmd);
    if (error)
        return error;
#endif
    return ESP_OK;
}
