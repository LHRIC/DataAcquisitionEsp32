#include "can/gps_time.hpp"

#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

namespace gps_time
{
namespace
{
struct state_t
{
    bool valid;
    bool fix_valid;
    uint32_t first_seconds_of_day;
    int64_t first_anchor_monotonic_us;
    uint32_t current_seconds_of_day;
    int64_t current_anchor_monotonic_us;
};

static portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;
static state_t g_state = {};

static uint32_t extract_bits_le(const uint8_t *data, size_t len,
                                size_t start_bit, size_t bit_len)
{
    uint32_t value = 0;

    for (size_t i = 0; i < bit_len; ++i)
    {
        size_t bit_index = start_bit + i;
        size_t byte_index = bit_index / 8;
        if (byte_index >= len)
        {
            return 0;
        }

        uint8_t bit = (data[byte_index] >> (bit_index % 8)) & 0x1;
        value |= (uint32_t)bit << i;
    }

    return value;
}
} // namespace

void reset()
{
    taskENTER_CRITICAL(&g_lock);
    g_state = {};
    taskEXIT_CRITICAL(&g_lock);
}

bool update_from_can_payload(const uint8_t *data, size_t len)
{
    if (!data || len < 8)
    {
        return false;
    }

    uint32_t mux = extract_bits_le(data, len, 0, 4);
    if (mux != 1)
    {
        return false;
    }

    uint32_t seconds_of_day = extract_bits_le(data, len, 32, 24);
    bool fix_valid = extract_bits_le(data, len, 63, 1) != 0;
    if (!fix_valid)
    {
        return false;
    }

    int64_t now_us = esp_timer_get_time();

    taskENTER_CRITICAL(&g_lock);
    if (!g_state.valid)
    {
        g_state.first_seconds_of_day = seconds_of_day;
        g_state.first_anchor_monotonic_us = now_us;
    }
    g_state.current_seconds_of_day = seconds_of_day;
    g_state.current_anchor_monotonic_us = now_us;
    g_state.valid = true;
    g_state.fix_valid = true;
    taskEXIT_CRITICAL(&g_lock);

    return true;
}

bool has_anchor()
{
    taskENTER_CRITICAL(&g_lock);
    bool valid = g_state.valid;
    taskEXIT_CRITICAL(&g_lock);
    return valid;
}

bool get_anchor(uint32_t *seconds_of_day, int64_t *anchor_monotonic_us)
{
    taskENTER_CRITICAL(&g_lock);
    bool valid = g_state.valid;
    if (valid)
    {
        if (seconds_of_day)
        {
            *seconds_of_day = g_state.first_seconds_of_day;
        }
        if (anchor_monotonic_us)
        {
            *anchor_monotonic_us = g_state.first_anchor_monotonic_us;
        }
    }
    taskEXIT_CRITICAL(&g_lock);
    return valid;
}

bool get_timeval(timeval *tv)
{
    if (!tv)
    {
        return false;
    }

    uint32_t seconds_of_day = 0;
    int64_t anchor_monotonic_us = 0;

    taskENTER_CRITICAL(&g_lock);
    bool valid = g_state.valid;
    if (valid)
    {
        seconds_of_day = g_state.current_seconds_of_day;
        anchor_monotonic_us = g_state.current_anchor_monotonic_us;
    }
    taskEXIT_CRITICAL(&g_lock);

    if (!valid)
    {
        return false;
    }

    int64_t elapsed_us = esp_timer_get_time() - anchor_monotonic_us;
    if (elapsed_us < 0)
    {
        elapsed_us = 0;
    }

    uint64_t total_us = (uint64_t)seconds_of_day * 1000000ULL +
                        (uint64_t)elapsed_us;
    tv->tv_sec = (time_t)(total_us / 1000000ULL);
    tv->tv_usec = (suseconds_t)(total_us % 1000000ULL);
    return true;
}
} // namespace gps_time
