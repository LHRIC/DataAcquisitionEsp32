#pragma once
#include "daq_core/buffer_pool.hpp"
#include "esp_log.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "esp_twai_types.h"
#include "freertos/idf_additions.h"

#define TWAI_TX_PIN (GPIO_NUM_4)
#define TWAI_RX_PIN (GPIO_NUM_5)

extern QueueHandle_t twai_queue;
void twai_init();
