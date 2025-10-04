#include "xbee/xbee_tx_task.hpp"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "portmacro.h"

TaskHandle_t xbee_tx_task;

static void xbee_tx_cb(void *)
{
    block_t *block;

    while (1)
    {
        if (xQueueReceive(xbee_queue, &block, portMAX_DELAY) == pdTRUE)
        {
            ESP_LOGI("xbee_tx", "wrote data to uart");
            uart_write_bytes(UART_PORT, (const char *)block->data, block->size);
            block_release(block);
        }

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void xbee_tx_task_start(UBaseType_t prio, UBaseType_t stackWords,
                        BaseType_t core)
{
    xTaskCreatePinnedToCore(xbee_tx_cb, "xbee_tx", stackWords, NULL, prio,
                            &xbee_tx_task, core);
}
