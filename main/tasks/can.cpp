#include "can.hpp"
#include "driver/uart.h"
#include "freertos/idf_additions.h"
#include "portmacro.h"

TaskHandle_t can_task;

static void can_cb(void *)
{
    printf("can_task\n");
    while (1)
        vTaskDelay(100 / portTICK_PERIOD_MS);
}

void can_task_start(UBaseType_t prio, UBaseType_t stackWords, BaseType_t core)
{
    xTaskCreatePinnedToCore(can_cb, "can", stackWords, NULL, prio, &can_task,
                            core);
}
