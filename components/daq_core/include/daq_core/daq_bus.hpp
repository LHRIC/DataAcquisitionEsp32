#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <cstdint>

#define POOL_SIZE 128

struct block_t
{
    uint8_t data[1024];
    volatile uint32_t refcnt;
    size_t size;
};

struct DaqBus
{
    QueueHandle_t free_q{};
    QueueHandle_t xbee_q{};
    QueueHandle_t sd_q{};
    block_t *pool;
    size_t pool_len;
};

#ifdef __cplusplus
extern "C"
{
#endif
    void daq_bus_init(DaqBus *bus); // creates queues, populates pool
    void block_acquire(block_t *b);
    void block_release(DaqBus *bus, block_t *b);
#ifdef __cplusplus
}
#endif
