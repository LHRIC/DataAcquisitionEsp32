#include "twai/twai.hpp"
#include "daq_core/buffer_pool.hpp"
#include "esp_err.h"
#include "esp_twai.h"
#include "esp_twai_types.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "hal/twai_types.h"
#include "portmacro.h"

QueueHandle_t twai_queue = nullptr;

static bool IRAM_ATTR twai_rx_cb(twai_node_handle_t node_handle,
                                 const twai_rx_done_event_data_t *edata,
                                 void *user_ctx)
{
    BaseType_t hpw = pdFALSE;

    block_t *block = nullptr;

    if (xQueueReceiveFromISR(free_queue, &block, &hpw) != pdTRUE)
    {
        portYIELD_FROM_ISR(hpw);
        return false;
    }

    block_acquire(block);

    // TODO: pack header and data into free_queue buffer; should have 10 byte
    // buffers to fanout
    twai_frame_header_t h = {0};
    twai_frame_t rx = {
        .header = h, .buffer = block->data, .buffer_len = sizeof(block->data)};

    if (twai_node_receive_from_isr(node_handle, &rx) != ESP_OK)
    {
        // Return buffer if RX failed
        (void)xQueueSendFromISR(free_queue, &block, &hpw);
        portYIELD_FROM_ISR(hpw);
        return true;
    }

    block->size = rx.buffer_len;
    block->refcnt = 1;

    xQueueSendFromISR(twai_queue, &block, &hpw);

    portYIELD_FROM_ISR(hpw);
    return true;
}

bool IRAM_ATTR twai_err_cb(twai_node_handle_t handle,
                           const twai_error_event_data_t *err, void *user_data)
{
    BaseType_t hpw = pdFALSE;
    esp_rom_printf(
        "[TWAI ERR] flags: val=%d bit_err=%d, ack_err=%d, arb_lost=%d, "
        "stuff_err=%d form_err=%d\n",
        (int)err->err_flags.val, (int)err->err_flags.bit_err,
        (int)err->err_flags.ack_err, (int)err->err_flags.arb_lost,
        (int)err->err_flags.form_err, (int)err->err_flags.stuff_err);

    portYIELD_FROM_ISR(hpw);
    return true;
}

void twai_init()
{
    twai_queue = xQueueCreate(100, sizeof(block_t *));

    twai_node_handle_t node_handle = NULL;
    twai_onchip_node_config_t node_config = {};

    node_config.io_cfg.tx = TWAI_TX_PIN;
    node_config.io_cfg.rx = TWAI_RX_PIN;
    node_config.bit_timing.bitrate = 1000000;
    node_config.tx_queue_depth = 5;
    // node_config.flags.enable_listen_only = 1;

    ESP_ERROR_CHECK(twai_new_node_onchip(&node_config, &node_handle));

    twai_event_callbacks_t callback = {};
    callback.on_rx_done = twai_rx_cb;
    callback.on_error = twai_err_cb;

    ESP_ERROR_CHECK(
        twai_node_register_event_callbacks(node_handle, &callback, NULL));

    ESP_ERROR_CHECK(twai_node_enable(node_handle));

    ESP_LOGI("twai", "initialized twai");
}
