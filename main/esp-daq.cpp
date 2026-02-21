#include "can/can.hpp"
#include "daq_core/buffer_pool.hpp"
#include "esp_log.h"
#include "sd/sd.hpp"
#include "twai/twai.hpp"
#include "xbee/uart.hpp"
#include "xbee/xbee_tx_task.hpp"
#include "http/file_server.h"

extern "C" void app_main(void)
{
    ESP_LOGI("main", "Starting Data Acquisition System");
    
    pool_init();
    uart_init(false);
    twai_init();
    can_init();
    sd_init();

    wifi_init_softap();
    start_file_server(MOUNT_POINT);
    // Start consumer tasks with HIGHER priority than producer
    // Priority hierarchy: SD(8) > XBee(7) > CAN(6)
    sd_task_start(8, 8192, 1);        // Highest - must drain queue fast
    xbee_tx_task_start(7, 4096, 1);   // High - secondary consumer
    can_task_start(6, 4096, 1);       // Lower - producer/fanout
    
    ESP_LOGI("main", "All tasks started");
}
