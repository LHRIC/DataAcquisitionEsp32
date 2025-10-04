#pragma once
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "esp_twai_types.h"
#include "freertos/idf_additions.h"

#define TWAI_TX_PIN (GPIO_NUM_4)
#define TWAI_RX_PIN (GPIO_NUM_5)

QueueHandle_t twai_queue;
static bool twai_rx_cb(twai_node_handle_t node_handle,
                       const twai_rx_done_event_data_t *edata, void *user_ctx);
void twai_init();
