#include "daq_core/daq_bus.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static block_t pool_storage[POOL_SIZE];

void daq_bus_init(DaqBus *bus)
{
    bus->pool = pool_storage;
    bus->pool_len = POOL_SIZE;
    bus->free_q = xQueueCreate(POOL_SIZE, sizeof(block_t *));
    bus->xbee_q = xQueueCreate(POOL_SIZE, sizeof(block_t *));
    bus->sd_q = xQueueCreate(POOL_SIZE, sizeof(block_t *));
    for (size_t i = 0; i < POOL_SIZE; i++)
    {
        auto *b = &pool_storage[i];
        b->refcnt = 0;
        xQueueSend(bus->free_q, &b, 0);
    }
}
void block_acquire(block_t *b)
{
    __atomic_add_fetch(&b->refcnt, 1, __ATOMIC_ACQ_REL);
}
void block_release(DaqBus *bus, block_t *b)
{
    if (__atomic_sub_fetch(&b->refcnt, 1, __ATOMIC_ACQ_REL) == 0)
    {
        xQueueSend(bus->free_q, &b, 0);
    }
}
