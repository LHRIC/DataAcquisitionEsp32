#pragma once

#include <stdint.h>
#include <sys/time.h>

#include <stddef.h>

namespace gps_time
{
bool update_from_can_payload(const uint8_t *data, size_t len);
bool has_anchor();
bool get_anchor(uint32_t *seconds_of_day, int64_t *anchor_monotonic_us);
bool get_timeval(timeval *tv);
void reset();
} // namespace gps_time
