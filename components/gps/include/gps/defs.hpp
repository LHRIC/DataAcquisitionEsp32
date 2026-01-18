#pragma once

// WARN: These registers are unimplemented, they shouldn't be used.
#define GPS_I2C_DATA_REGISTER_START (0x00)
#define GPS_I2C_DATA_REGISTER_END (0xFC)
#define GPS_I2C_DATA_REGISTER_SIZE (0xFD)

#define GPS_I2C_BYTES_AVAILABLE_HIGH (0xFD)
#define GPS_I2C_BYTES_AVAILABLE_LOW (0xFE)
#define GPS_I2C_DATA_STREAM (0xFF)

#define GPS_I2C_DEVICE_ADDRESS (0x84)
