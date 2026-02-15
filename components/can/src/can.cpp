#include "can/can.hpp"
#include "daq_core/buffer_pool.hpp"
#include "driver/twai.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "portmacro.h"

TaskHandle_t can_task;

void fanout(uint8_t *payload, size_t size)
{
    if (!payload)
    {
        ESP_LOGE("can", "Payload is NULL");
        return;
    }

    if (size > BLOCK_SIZE)
    {
        ESP_LOGE("can", "Size is greater than BLOCK_SIZE");
        return;
    }

    block_t *block;
    if (xQueueReceive(free_queue, &block, 0) != pdTRUE)
    {
        ESP_LOGW("can", "No free block, packet dropped");
        return;
    }

    memcpy(block->data, payload, size);
    block->size = size;

    ESP_LOGI("can", "Received data from twai: %x%x", block->data[0],
             block->data[1]);

    configASSERT(block->refcnt == 1);

    // producer <- one reference while publishing
    block_acquire(block);

    uint8_t refs = 0;
    if (xQueueSend(xbee_queue, &block, 0) == pdTRUE)
    {
        block_acquire(block);
        refs++;
    }
    else
    {
        ESP_LOGW("can", "xbee_queue full; dropping packet");
    }

    if (xQueueSend(sd_queue, &block, 0) == pdTRUE)
    {
        block_acquire(block);
        refs++;
    }
    else
    {
        ESP_LOGW("can", "sd_queue full; dropping packet");
    }

    if (refs == 0)
    {
        // nobody took it; drop the packet
        block_release(block);
        ESP_LOGD("can", "No consumers; dropped packet");
        return;
    }

    // drop producer's temporary reference
    block_release(block);
    return;
}

void can_init(void)
{
    twai_general_config_t g_config =
        TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_GPIO, CAN_RX_GPIO, TWAI_MODE_NORMAL);
    g_config.rx_queue_len = 10;

    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_ERROR_CHECK(twai_start());

    ESP_LOGI("can", "TWAI driver started on TX:%d RX:%d @ 500kbps", CAN_TX_GPIO,
             CAN_RX_GPIO);
}

/**
 * Callback for CAN task
 *
 * Retreives TWAI messages and attempts to fan them out to current consumers.
 * If there are no TWAI messages, immediately relinquish control
 */
static void can_cb(void *)
{
    twai_message_t rx_msg;
    uint8_t payload[8];

    ESP_LOGI("can", "CAN task started");

    // continuously retreive twai messages and fan them out to consumers
    while (1)
    {
        esp_err_t ret = twai_receive(&rx_msg, pdMS_TO_TICKS(1000));

        if (ret == ESP_OK)
        {
            uint16_t plen = rx_msg.data_length_code;

            if (plen > sizeof(payload))
            {
                ESP_LOGW("can", "CAN message too large: %d bytes", plen);
                plen = sizeof(payload);
            }

            if (plen > 0)
            {
                memcpy(payload, rx_msg.data, plen);
                fanout(payload, plen);
                ESP_LOGI("can", "Received CAN ID:0x%lx len:%d",
                         rx_msg.identifier, plen);
            }
        }
        else if (ret == ESP_ERR_TIMEOUT)
        {
            continue;
        }
        else
        {
            ESP_LOGW("can", "TWAI receive error: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void can_task_start(UBaseType_t prio, UBaseType_t stackWords, BaseType_t core)
{
    xTaskCreatePinnedToCore(can_cb, "can", stackWords, NULL, prio, &can_task,
                            core);
}
