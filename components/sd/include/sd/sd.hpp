#pragma once
#include "daq_core/buffer_pool.hpp"
#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// SD card mode selection
#define SD_USE_SDIO 1  // Set to 1 for SDIO, 0 for SPI

#if SD_USE_SDIO
// SDIO mode pins (4-bit mode)
#define SD_PIN_CLK  GPIO_NUM_39
#define SD_PIN_CMD  GPIO_NUM_38
#define SD_PIN_D0   GPIO_NUM_40
#define SD_PIN_D1   GPIO_NUM_41
#define SD_PIN_D2   GPIO_NUM_42
#define SD_PIN_D3   GPIO_NUM_36
#else
// SPI mode pins
#define SD_PIN_MISO GPIO_NUM_37
#define SD_PIN_MOSI GPIO_NUM_35
#define SD_PIN_CLK GPIO_NUM_36
#define SD_PIN_CS GPIO_NUM_38
#endif

extern TaskHandle_t sd_task;

// Core SD functions
void sd_init();
void sd_close();

esp_err_t sd_write_line(FILE *f, const char *format, ...);
esp_err_t sd_write_can_message(uint32_t timestamp_ms, uint32_t can_id,
                               const uint8_t *data, size_t len);

// Task management
void sd_task_start(UBaseType_t prio, UBaseType_t stackWords, BaseType_t core);

// Testing/diagnostics
bool sd_is_ready();
const char *sd_get_current_session_path();
