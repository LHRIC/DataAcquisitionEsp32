#include "twai/twai.hpp"
#include "esp_twai_types.h"
#include "freertos/idf_additions.h"
#include "hal/twai_types.h"

QueueHandle_t twai_queue = nullptr;

static bool twai_rx_cb(twai_node_handle_t node_handle,
                       const twai_rx_done_event_data_t *edata, void *user_ctx)
{
    uint8_t recv_buffer[8];
    twai_frame_header_t header = {};
    twai_frame_t rx_frame = {
        .header = header,
        .buffer = recv_buffer,
        .buffer_len = sizeof(recv_buffer),
    };

    // keep ISR as small as possible so just write to event queue
    if (twai_node_receive_from_isr(node_handle, &rx_frame) == ESP_OK)
    {
        xQueueSend(twai_queue, recv_buffer, 0);
    }

    return true;
}

void twai_init()
{
    twai_queue = xQueueCreate(100, 100);

    twai_node_handle_t node_handle = NULL;
    twai_onchip_node_config_t node_config = {};

    node_config.io_cfg.tx = TWAI_TX_PIN;
    node_config.io_cfg.rx = TWAI_RX_PIN;
    node_config.bit_timing.bitrate = 1000000;
    node_config.tx_queue_depth = 5;

    ESP_ERROR_CHECK(twai_new_node_onchip(&node_config, &node_handle));
    ESP_ERROR_CHECK(twai_node_enable(node_handle));

    twai_event_callbacks_t callback = {
        .on_rx_done = twai_rx_cb,
    };

    twai_node_register_event_callbacks(node_handle, &callback, NULL);
}
