#include "buffer_pool.hpp"
#include "FreeRTOSConfig.h"
#include "esp_assert.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"

QueueHandle_t free_queue = NULL;
QueueHandle_t xbee_queue = NULL;
QueueHandle_t sd_queue = NULL;

void pool_init(void)
{
    free_queue = xQueueCreate(POOL_SIZE, sizeof(block_t *));
    xbee_queue = xQueueCreate(POOL_SIZE, sizeof(block_t *));
    sd_queue = xQueueCreate(POOL_SIZE, sizeof(block_t *));

    // populate the free queue
    for (int i = 0; i < POOL_SIZE; i++)
    {
        block_t *block = &pool[i];
        block->refcnt = 0;
        xQueueSend(free_queue, &block, 0);
    }
}

void block_acquire(block_t *block)
{
    __atomic_add_fetch(&block->refcnt, 1, __ATOMIC_ACQ_REL);
}

void block_release(block_t *block)
{
    if (__atomic_sub_fetch(&block->refcnt, 1, __ATOMIC_ACQ_REL) == 0)
    {
        BaseType_t ok = xQueueSend(free_queue, &block, 0);
        configASSERT(ok == pdTRUE);
    }
}
