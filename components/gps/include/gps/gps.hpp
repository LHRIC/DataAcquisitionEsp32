#pragma once

#include "freertos/idf_additions.h"
#include "i2c/i2c.hpp"
#include <time.h>

#define GPS_I2C_BYTES_AVAILABLE_HIGH (0xFD)
#define GPS_I2C_BYTES_AVAILABLE_LOW (0xFE)
#define GPS_I2C_DATA_STREAM (0xFF)

#define GPS_I2C_DEVICE_ADDRESS (0x42)

#define GPS_POLLING_PERIOD_MS (100)

enum Direction
{
    North,
    South,
    East,
    West
};

struct GPSData
{
    Direction north_south;
    float latitude;
    Direction east_west;
    float longitude;
    tm time;
    TickType_t updated;
};

extern SemaphoreHandle_t gpsMutex;
extern volatile GPSData gpsData;

void gps_init();
void gps_task(void *pvTaskParameters);
void gps_test();

static uint8_t gps_i2c_read_byte();
static uint16_t gps_i2c_available_bytes();
