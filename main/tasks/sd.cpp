#include "sd.hpp"
#include "driver/uart.h"
#include "freertos/idf_additions.h"

TaskHandle_t sd_task;

static void sd_cb(void *)
{
    printf("sd_task\n");
    while (1)
        vTaskDelay(100 / portTICK_PERIOD_MS);
}

void sd_task_start(UBaseType_t prio, UBaseType_t stackWords, BaseType_t core)
{
    xTaskCreatePinnedToCore(sd_cb, "sd", stackWords, NULL, prio, &sd_task,
                            core);
}
