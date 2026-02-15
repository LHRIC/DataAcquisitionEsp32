#include "sd/sd.hpp"
#include "daq_core/buffer_pool.hpp"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_vfs_fat.h"

#if SD_USE_SDIO
#include "driver/sdmmc_host.h"
#else
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#endif
#include "sdmmc_cmd.h"

#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "sd";
static const char *MOUNT_POINT = "/sdcard";

TaskHandle_t sd_task;

// Globals
static sdmmc_card_t *g_card = NULL;
static FILE *g_session = NULL;

// Paths
static char g_session_path[128] = {0};

typedef struct __attribute__((packed))
{
    int64_t sec;
    int32_t usec;
    uint16_t len;
} sd_rec_hdr_t;

static esp_err_t mkdir_p(const char *path)
{
    // FAT + VFS: mkdir returns -1 on failure, errno set.
    // If it already exists, that's okay.
    if (mkdir(path, 0775) == 0)
        return ESP_OK;
    if (errno == EEXIST)
        return ESP_OK;
    ESP_LOGE(TAG, "mkdir(%s) failed: errno=%d", path, errno);
    return ESP_FAIL;
}

static void ensure_dirs_for_today_and_meta(void)
{
    // /sdcard/meta
    mkdir_p("/sdcard/meta");
    mkdir_p("/sdcard/logs");

    // /sdcard/logs/YYYY/MM/DD
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);

    char p[64];

    snprintf(p, sizeof(p), "/sdcard/logs/%04d", tmv.tm_year + 1900);
    mkdir_p(p);

    snprintf(p, sizeof(p), "/sdcard/logs/%04d/%02d", tmv.tm_year + 1900,
             tmv.tm_mon + 1);
    mkdir_p(p);

    snprintf(p, sizeof(p), "/sdcard/logs/%04d/%02d/%02d", tmv.tm_year + 1900,
             tmv.tm_mon + 1, tmv.tm_mday);
    mkdir_p(p);
}

static void make_session_path(char *out, size_t out_sz)
{
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);

    // A simple session id: random 32-bit (good enough for uniqueness per boot)
    uint32_t sid = esp_random();

    snprintf(out, out_sz, "/sdcard/logs/%04d/%02d/%02d/S%08lx.bin",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
             (unsigned long)sid);
}

static void index_append_session_start(const char *session_path)
{
    FILE *f = fopen("/sdcard/meta/index.txt", "a");
    if (!f)
        return;

    time_t now = time(NULL);
    fprintf(f, "START %ld %s\n", (long)now, session_path);
    fclose(f);
}

static esp_err_t open_new_session_file(void)
{
    if (g_session)
    {
        fflush(g_session);
        fclose(g_session);
        g_session = NULL;
    }

    // Don't try to create session file if time isn't set (would create invalid
    // paths)
    time_t now = time(NULL);
    if (now < 1000000)
    {
        ESP_LOGW(TAG, "System time not set, skipping session file creation");
        return ESP_ERR_INVALID_STATE;
    }

    ensure_dirs_for_today_and_meta();
    make_session_path(g_session_path, sizeof(g_session_path));

    g_session = fopen(g_session_path, "ab"); // append-binary
    if (!g_session)
    {
        ESP_LOGE(TAG, "Failed to open session file %s (errno=%d)",
                 g_session_path, errno);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Logging to session: %s", g_session_path);
    index_append_session_start(g_session_path);
    return ESP_OK;
}

void sd_task_start(UBaseType_t prio, UBaseType_t stackWords, BaseType_t core)
{
    xTaskCreatePinnedToCore(
        [](void *)
        {
            ESP_LOGI(TAG, "SD task started");

            block_t *block = NULL;

            while (1)
            {
                if (xQueueReceive(sd_queue, &block, portMAX_DELAY) != pdTRUE)
                {
                    continue;
                }

                if (!sd_is_ready())
                {
                    ESP_LOGW(TAG, "SD not ready; dropping %u bytes",
                             (unsigned)block->size);
                    block_release(block);
                    continue;
                }

                // Get timestamp in milliseconds
                uint32_t timestamp_ms =
                    xTaskGetTickCount() * portTICK_PERIOD_MS;

                char hex_str[BLOCK_SIZE * 2 + 1];
                for (size_t i = 0; i < block->size && i < BLOCK_SIZE; i++)
                {
                    sprintf(&hex_str[i * 2], "%02X", block->data[i]);
                }
                hex_str[block->size * 2] = '\0';

                // sd_write_line("%lu DATA:%s", (unsigned long)timestamp_ms,
                //               hex_str);

                block_release(block);
            }
        },
        "sd", stackWords, NULL, prio, &sd_task, core);
}

void sd_init()
{
#if SD_USE_SDIO
    ESP_LOGI(TAG, "Initializing SD card via SDIO (4-bit)");
    ESP_LOGI(TAG, "Pins: CLK=%d CMD=%d D0=%d D1=%d D2=%d D3=%d", 
             SD_PIN_CLK, SD_PIN_CMD, SD_PIN_D0, SD_PIN_D1, SD_PIN_D2, SD_PIN_D3);

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    sdmmc_slot_config_t slot_cfg = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_cfg.width = 4;
    slot_cfg.clk = SD_PIN_CLK;
    slot_cfg.cmd = SD_PIN_CMD;
    slot_cfg.d0 = SD_PIN_D0;
    slot_cfg.d1 = SD_PIN_D1;
    slot_cfg.d2 = SD_PIN_D2;
    slot_cfg.d3 = SD_PIN_D3;
    slot_cfg.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t err = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &g_card);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_vfs_fat_sdmmc_mount failed: %s", esp_err_to_name(err));
        g_card = NULL;
        return;
    }
#else
    ESP_LOGI(TAG, "Initializing SD card via SPI");
    ESP_LOGI(TAG, "Pins: MISO=%d MOSI=%d CLK=%d CS=%d", SD_PIN_MISO,
             SD_PIN_MOSI, SD_PIN_CLK, SD_PIN_CS);

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_PIN_MOSI,
        .miso_io_num = SD_PIN_MISO,
        .sclk_io_num = SD_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
        .flags = SPICOMMON_BUSFLAG_MASTER,
    };

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.host_id = SPI2_HOST;
    slot_cfg.gpio_cs = SD_PIN_CS;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };

    err = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &g_card);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_vfs_fat_sdspi_mount failed: %s", esp_err_to_name(err));
        g_card = NULL;
        return;
    }
#endif

    ESP_LOGI(TAG, "SD mounted at %s", MOUNT_POINT);
    sdmmc_card_print_info(stdout, g_card);

    if (open_new_session_file() != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to open session file (not critical for testing)");
        // Don't return - allow SD to be used even if session file fails
    }
}

bool sd_is_ready() { return g_card != NULL; }

const char *sd_get_current_session_path()
{
    return g_session_path[0] != '\0' ? g_session_path : NULL;
}

esp_err_t sd_write_line(FILE *f, const char *format, ...)
{
    if (!g_card)
    {
        ESP_LOGE(TAG, "SD card not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!f)
    {
        ESP_LOGE(TAG, "Attempted to write to a NULL file");
        return ESP_ERR_INVALID_STATE;
    }

    va_list args;
    va_start(args, format);
    vfprintf(f, format, args);
    va_end(args);
    fprintf(f, "\n");

    fflush(f);

    return ESP_OK;
}

esp_err_t sd_write_can_message(uint32_t timestamp_ms, uint32_t can_id,
                               const uint8_t *data, size_t len)
{
    if (!g_card)
    {
        ESP_LOGE(TAG, "SD card not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // First try to create a simple test file to verify SD is writable
    static bool sd_write_verified = false;
    if (!sd_write_verified)
    {
        FILE *test = fopen("/sdcard/.writetest", "w");
        if (test)
        {
            fclose(test);
            remove("/sdcard/.writetest");
            sd_write_verified = true;
            ESP_LOGI(TAG, "SD write verified");
        }
        else
        {
            ESP_LOGE(TAG, "SD card not writable (errno=%d)", errno);
            return ESP_FAIL;
        }
    }

    // Open CAN log file
    FILE *can_log = fopen("/sdcard/can_log.txt", "a");
    if (!can_log)
    {
        ESP_LOGE(TAG, "Failed to open CAN log file (errno=%d)", errno);
        return ESP_FAIL;
    }

    // Write timestamp and CAN ID
    fprintf(can_log, "%lu CAN_ID:0x%03lX DATA:", (unsigned long)timestamp_ms,
            (unsigned long)can_id);

    // Write data bytes
    for (size_t i = 0; i < len; i++)
    {
        fprintf(can_log, "%02X", data[i]);
    }
    fprintf(can_log, "\n");

    fflush(can_log);
    fclose(can_log);

    return ESP_OK;
}

void sd_close()
{
    if (g_session)
    {
        fflush(g_session);
        fclose(g_session);
        g_session = NULL;
    }
    if (g_card)
    {
        esp_vfs_fat_sdcard_unmount(MOUNT_POINT, g_card);
        g_card = NULL;
    }
#if !SD_USE_SDIO
    spi_bus_free(SPI2_HOST);
#endif
}
