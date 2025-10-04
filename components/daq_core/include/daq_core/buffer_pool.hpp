#pragma once
#include "freertos/idf_additions.h"
#include <string.h>

#define POOL_SIZE 128
#define BLOCK_SIZE 128
#define NUM_CONSUMERS 1

extern QueueHandle_t free_queue, xbee_queue, sd_queue;

typedef struct
{
    uint16_t size;
    uint8_t data[BLOCK_SIZE];
    uint8_t refcnt;
} block_t;

static block_t pool[POOL_SIZE];

void pool_init(void);
void block_acquire(block_t *block);
void block_release(block_t *block);
