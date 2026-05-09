#include "can/can.hpp"
#include "daq_core/buffer_pool.hpp"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sd/sd.hpp"
#include "twai/twai.hpp"
#include "xbee/uart.hpp"
#include "xbee/xbee_tx_task.hpp"
#include "http/file_server.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"

static void log_ram_status(const char *phase)
{
    ESP_LOGI("ram", "[%s] free_heap=%u 8bit_free=%u largest_8bit=%u app_main_stack=%u",
             phase,
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));

    if (sd_task) {
        ESP_LOGI("ram", "[%s] sd_task_stack=%u", phase,
                 (unsigned)uxTaskGetStackHighWaterMark(sd_task));
    }
    if (xbee_tx_task) {
        ESP_LOGI("ram", "[%s] xbee_tx_task_stack=%u", phase,
                 (unsigned)uxTaskGetStackHighWaterMark(xbee_tx_task));
    }
    if (can_task) {
        ESP_LOGI("ram", "[%s] can_task_stack=%u", phase,
                 (unsigned)uxTaskGetStackHighWaterMark(can_task));
    }
}

extern "C" void app_main(void)
{
    log_stream_init();
    ESP_LOGI("main", "Starting Data Acquisition System");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    log_ram_status("after-net-init");

    pool_init();
    uart_init(false);
    twai_init();
    can_init();
    log_ram_status("after-core-init");
    
    // Try to initialize SD card
    sd_init();
    log_ram_status("after-sd-init");

    const char *base_path = MOUNT_POINT;
    if (!sd_is_ready()) {
        ESP_LOGW("main", "SD card not ready, falling back to SPIFFS");
        base_path = "/spiffs";
        if (spiffs_init(base_path) != ESP_OK) {
            ESP_LOGE("main", "Failed to initialize SPIFFS fallback!");
        }
    }

    wifi_init_softap();
    log_ram_status("after-wifi-init");
    start_file_server(base_path);
    log_ram_status("after-file-server");
    // Start consumer tasks with HIGHER priority than producer
    // Priority hierarchy: SD(8) > XBee(7) > CAN(6)
    sd_task_start(8, 4096, 1);        // Highest - must drain queue fast
    // xbee_tx_task_start(7, 4096, 1);   // High - secondary consumer
    can_task_start(6, 4096, 1);       // Lower - producer/fanout
    log_ram_status("after-task-start");
    
    ESP_LOGI("main", "All tasks started");
}
