#include "gps/gps.hpp"

i2c_master_dev_handle_t gps_i2c_dev_handle;

void gps_init()
{
    i2c_device_config_t device_config;

    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = GPS_I2C_DEVICE_ADDRESS;
    device_config.scl_speed_hz = 400000;

    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_handle, &device_config,
                                              &gps_i2c_dev_handle));
}

void gps_test()
{
    while (true)
    {
        char gps_data = gps_i2c_read_byte();
        printf("%c", gps_data);
    }
}

/*
 *  GPS I2C Protocol Functions
 */

static uint8_t gps_i2c_read_byte()
{
    uint8_t data;
    ESP_ERROR_CHECK(i2c_master_receive(gps_i2c_dev_handle, &data, 1, -1));

    return data;
}
