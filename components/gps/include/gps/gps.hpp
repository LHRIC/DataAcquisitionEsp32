#pragma once

#include "gps/defs.hpp"
#include "i2c/i2c.hpp"

enum Direction
{
    North,
    South,
    East,
    West
};

struct GPSPosition
{
    Direction north_south;
    Direction east_west;
};

void gps_init();
void gps_task(void *pvTaskParameters);
void gps_test();

static uint8_t gps_i2c_read_byte();
static uint16_t gps_i2c_available_bytes();
