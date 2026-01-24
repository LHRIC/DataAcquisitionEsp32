#include "sd/sd.hpp"
#include "daq_core/buffer_pool.hpp"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "portmacro.h"

TaskHandle_t sd_task;

static void sd_cb(void *)
{
    printf("sd_task\n");

    block_t *block;

    while (1)
    {
        if (xQueueReceive(sd_queue, &block, portMAX_DELAY) == pdTRUE)
        {
            ESP_LOGI("sd", "wrote data to sd");
            block_release(block);
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void sd_task_start(UBaseType_t prio, UBaseType_t stackWords, BaseType_t core)
{
    xTaskCreatePinnedToCore(sd_cb, "sd", stackWords, NULL, prio, &sd_task,
                            core);
}

void sd_init() {}
