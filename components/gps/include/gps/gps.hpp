#pragma once

#include "gps/defs.hpp"
#include "i2c/i2c.hpp"
#include <cmath>
#include <cstdint>
#include <ctime>

enum Direction
{
    North,
    South,
    East,
    West
};

struct GPSPosition
{
    std::tm *utc_timestamp;
    double_t longitude;
    Direction north_south;
    double_t latitude;
    Direction east_west;
};

void gps_init();
void gps_test();

static uint8_t gps_i2c_read_byte();
