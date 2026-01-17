#include "i2c/i2c.hpp"
#include "driver/i2c_master.h"
#include "soc/clk_tree_defs.h"

void i2c_init()
{
    i2c_master_bus_config_t bus_config;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.i2c_port = -1; // Auto Select I2C Port
    bus_config.sda_io_num = I2C_SDA_PIN;
    bus_config.scl_io_num = I2C_SCL_PIN;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus_handle));
}
