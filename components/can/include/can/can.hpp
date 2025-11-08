#pragma once
#include "daq_core/buffer_pool.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern TaskHandle_t can_task;
void can_task_start(UBaseType_t prio, UBaseType_t stackWords, BaseType_t core);

/*
 * @brief Takes a block from the twai_queue and fans it out to various other
 * queues for all reaping tasks
 *
 * This function acquires a block that has CAN data written to it and fans
 * it out to other interested parties by writing the block pointer to their
 * queues. If no data can be sent (due to busy queues), the data is dropped.
 * This should not happen if the egress rate exceeds the ingress rate.
 */
void fanout();
