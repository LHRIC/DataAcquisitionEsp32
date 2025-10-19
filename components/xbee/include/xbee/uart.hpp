#pragma once
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"

static const char *TAG = "XBEE";
#define UART_PORT UART_NUM_2
#define UART_BAUD 230400
#define RX_BUFFER_SIZE 1024
#define TX_BUFFER_SIZE 1024

// TODO: figure out real pins
#define RXD_PIN (GPIO_NUM_16)
#define TXD_PIN (GPIO_NUM_17)
#define RTS_PIN UART_PIN_NO_CHANGE
#define CTS_PIN UART_PIN_NO_CHANGE

extern QueueHandle_t rx_queue;
void uart_init(bool hw_flow_control_on);
