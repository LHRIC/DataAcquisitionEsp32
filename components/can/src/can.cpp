#include "can/can.hpp"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/idf_additions.h"
#include "portmacro.h"
#include "twai/twai.hpp"

TaskHandle_t can_task;

void fanout()
{
    block_t *block;
    if (xQueueReceive(twai_queue, &block, 0) != pdTRUE)
    {
        // no block available; drop the packet
        ESP_LOGW("fanout", "Getting nothing from twai");
        return;
    }

    // configASSERT(block->refcnt == 0);

    // producer <- one reference while publishing
    block_acquire(block);

    uint8_t refs = 0;
    if (xQueueSend(xbee_queue, &block, 0) == pdTRUE)
    {
        block_acquire(block);
        refs++;
    }

    // SD task is disabled, so don't send to sd_queue
    // if (xQueueSend(sd_queue, &block, 0) == pdTRUE)
    //     refs++;

    if (refs == 0)
    {
        // nobody took it; drop the packet
        block_release(block);
        ESP_LOGD("fanout", "No consumers; dropped");
        return;
    }

    // drop producer's temporary reference
    block_release(block);
    return;
}

static void can_cb(void *)
{
    while (1)
    {
        fanout();

        vTaskDelay(30 / portTICK_PERIOD_MS);
    }
}

void can_task_start(UBaseType_t prio, UBaseType_t stackWords, BaseType_t core)
{
    xTaskCreatePinnedToCore(can_cb, "can", stackWords, NULL, prio, &can_task,
                            core);
}
