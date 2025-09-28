#include "xbee_tx.hpp"
#include "driver/uart.h"
#include "freertos/idf_additions.h"
#include "portmacro.h"

TaskHandle_t xbee_tx_task;

static void xbee_tx_cb(void *)
{
    printf("xbee_tx_task\n");
    while (1)
        vTaskDelay(100 / portTICK_PERIOD_MS);
}

void xbee_tx_task_start(UBaseType_t prio, UBaseType_t stackWords,
                        BaseType_t core)
{
    xTaskCreatePinnedToCore(xbee_tx_cb, "xbee_tx", stackWords, NULL, prio,
                            &xbee_tx_task, core);
}
