#pragma once
#include "daq_core/buffer_pool.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern TaskHandle_t can_task;
void can_task_start(UBaseType_t prio, UBaseType_t stackWords, BaseType_t core);
void fanout();
