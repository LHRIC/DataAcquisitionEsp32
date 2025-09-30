#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "include/buffer_pool.hpp"

extern TaskHandle_t can_task;
void can_task_start(UBaseType_t prio, UBaseType_t stackWords, BaseType_t core);
static inline void fanout(const uint8_t *data, uint16_t size);
