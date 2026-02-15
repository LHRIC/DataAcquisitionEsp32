#include "can/can.hpp"
#include "daq_core/buffer_pool.hpp"
#include "esp_log.h"
#include "sd/sd.hpp"
#include "twai/twai.hpp"
#include "xbee/uart.hpp"
#include "xbee/xbee_tx_task.hpp"

extern "C" void app_main(void)
{
    pool_init();
    uart_init(false);
    can_init();
    sd_init();


    sd_task_start(6, 8192, 1);


    ESP_LOGI("main", "Testing SD card...");
    sd_test_write();
    ESP_LOGI("main", "SD test complete, starting tasks...");

    xbee_tx_task_start(6, 4096, 1);
    can_task_start(6, 4096, 1);
    // xbee_rx_task_start(6, 4096, 1);
    // control_task_start(6, 4096, 1);
}
