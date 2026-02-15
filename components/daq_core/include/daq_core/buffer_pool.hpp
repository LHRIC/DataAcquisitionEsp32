#pragma once
#include "esp_log.h"
#include "freertos/idf_additions.h"

#define POOL_SIZE 128
#define BLOCK_SIZE 128
#define NUM_CONSUMERS 2

extern QueueHandle_t free_queue, xbee_queue, sd_queue;

typedef struct
{
    uint16_t size;
    uint8_t data[BLOCK_SIZE];
    uint8_t refcnt;
} block_t;

static block_t pool[POOL_SIZE];

/*
 * @brief Initializes the buffer pool
 *
 * Creates various freeRTOS queues for management and allocates memory
 */
void pool_init(void);

/*
 * @brief Acquires a block, increasing its refcnt by 1
 * @param block A pointer to the block to acquire
 */
void block_acquire(block_t *block);

/*
 * @brief Releases a block, decrementing its refcnt
 *
 * If the block's refcnt drops to 0, the block will also be freed.
 * This means that it will be added back to the free queue, which means
 * that the block can be overwritten at any time. It must be guaranteed that
 * this data is no longer useful to anyone at this point
 *
 * @param block A pointer to the block to release
 */
void block_release(block_t *block);
