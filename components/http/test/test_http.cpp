#include "http/file_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unity.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_spiffs.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

// Toggle storage backend: comment out to use SD card, uncomment to use SPIFFS.
#define TEST_USE_SPIFFS

#ifdef TEST_USE_SPIFFS
#define TEST_BASE_PATH "/spiffs"
#else
#include "sd/sd.hpp"
#define TEST_BASE_PATH MOUNT_POINT
#endif

static const char *TAG = "test_http";

static TickType_t to_ticks(int ms) { return pdMS_TO_TICKS(ms); }

static bool http_stack_ready  = false;
static bool wifi_initialized  = false;
static bool storage_ready     = false;
static bool server_started    = false;

static void ensure_http_stack(void)
{
    if (http_stack_ready) return;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    http_stack_ready = true;
}

static void ensure_storage_ready(void)
{
    if (storage_ready) return;

#ifdef TEST_USE_SPIFFS
    esp_vfs_spiffs_conf_t spiffs_cfg = {
        .base_path              = TEST_BASE_PATH,
        .partition_label        = NULL,
        .max_files              = 8,
        .format_if_mount_failed = true,
    };
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&spiffs_cfg));
    ESP_LOGI(TAG, "SPIFFS mounted at %s", TEST_BASE_PATH);
#else
    sd_init();
    vTaskDelay(to_ticks(500));
    TEST_ASSERT_TRUE_MESSAGE(sd_is_ready(), "SD card not ready");
    ESP_LOGI(TAG, "SD card mounted at %s", TEST_BASE_PATH);
#endif

    storage_ready = true;
}

static void ensure_wifi_initialized(void)
{
    if (wifi_initialized) return;
    ensure_http_stack();
    wifi_init_softap();
    wifi_initialized = true;
}

static void ensure_server_started(void)
{
    if (server_started) return;

    ensure_wifi_initialized();
    ensure_storage_ready();

    esp_err_t err = start_file_server(TEST_BASE_PATH);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    vTaskDelay(to_ticks(200));
    server_started = true;
}

static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    (void)evt;
    return ESP_OK;
}

// ---- wifi_init_softap ----

TEST_CASE("wifi_init_softap completes without error", "[http][wifi]")
{
    ensure_wifi_initialized();
    TEST_PASS();
}

// ---- start_file_server ----

TEST_CASE("start_file_server returns ESP_OK on first call", "[http][server]")
{
    ensure_wifi_initialized();
    ensure_storage_ready();

    esp_err_t err = start_file_server(TEST_BASE_PATH);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    server_started = true;
    vTaskDelay(to_ticks(200));
}

TEST_CASE("start_file_server returns ESP_ERR_INVALID_STATE when already running", "[http][server]")
{
    // Static server_data guard inside start_file_server prevents a second start.
    // This test must run after the first-call test.
    esp_err_t err = start_file_server(TEST_BASE_PATH);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);
}

// ---- HTTP endpoints via loopback ----

TEST_CASE("HTTP GET root returns 200 OK", "[http][endpoint]")
{
    ensure_server_started();

    esp_http_client_config_t cfg = {
        .url           = "http://127.0.0.1/",
        .timeout_ms    = 5000,
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    TEST_ASSERT_NOT_NULL(client);

    TEST_ASSERT_EQUAL(ESP_OK, esp_http_client_perform(client));
    TEST_ASSERT_EQUAL(200, esp_http_client_get_status_code(client));

    esp_http_client_cleanup(client);
}

TEST_CASE("HTTP GET /browser/ returns 200 OK", "[http][endpoint]")
{
    ensure_server_started();

    esp_http_client_config_t cfg = {
        .url           = "http://127.0.0.1/browser/",
        .timeout_ms    = 5000,
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    TEST_ASSERT_NOT_NULL(client);

    TEST_ASSERT_EQUAL(ESP_OK, esp_http_client_perform(client));
    TEST_ASSERT_EQUAL(200, esp_http_client_get_status_code(client));

    esp_http_client_cleanup(client);
}

TEST_CASE("HTTP GET /api/status returns 200 OK and JSON", "[http][endpoint]")
{
    ensure_server_started();

    esp_http_client_config_t cfg = {
        .url           = "http://127.0.0.1/api/status",
        .timeout_ms    = 5000,
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    TEST_ASSERT_NOT_NULL(client);

    TEST_ASSERT_EQUAL(ESP_OK, esp_http_client_perform(client));
    TEST_ASSERT_EQUAL(200, esp_http_client_get_status_code(client));

    char body[256] = {0};
    int read = esp_http_client_read_response(client, body, sizeof(body) - 1);
    TEST_ASSERT_TRUE(read > 0);
    TEST_ASSERT_NOT_EQUAL(NULL, strstr(body, "\"uptime_ms\""));
    TEST_ASSERT_NOT_EQUAL(NULL, strstr(body, "\"free_heap\""));

    esp_http_client_cleanup(client);
}

TEST_CASE("HTTP GET non-existent file returns 404", "[http][endpoint]")
{
    ensure_server_started();

    esp_http_client_config_t cfg = {
        .url           = "http://127.0.0.1/no_such_file_xyz.txt",
        .timeout_ms    = 5000,
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    TEST_ASSERT_NOT_NULL(client);

    TEST_ASSERT_EQUAL(ESP_OK, esp_http_client_perform(client));
    TEST_ASSERT_EQUAL(404, esp_http_client_get_status_code(client));

    esp_http_client_cleanup(client);
}

TEST_CASE("HTTP POST upload creates file on storage", "[http][endpoint]")
{
    ensure_server_started();

    const char *upload_data = "hello from upload test";
    const char *filename    = "test_upload.txt";
    char        filepath[64];
    snprintf(filepath, sizeof(filepath), "%s/%s", TEST_BASE_PATH, filename);
    unlink(filepath);

    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1/upload/%s", filename);

    esp_http_client_config_t cfg = {
        .url                   = url,
        .method                = HTTP_METHOD_POST,
        .timeout_ms            = 5000,
        .disable_auto_redirect = true,
        .event_handler         = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    TEST_ASSERT_NOT_NULL(client);
    esp_http_client_set_post_field(client, upload_data, strlen(upload_data));

    TEST_ASSERT_EQUAL(ESP_OK, esp_http_client_perform(client));
    int status = esp_http_client_get_status_code(client);
    TEST_ASSERT_TRUE_MESSAGE(status == 200 || status == 303, "expected 200 or 303 on upload");
    esp_http_client_cleanup(client);

    FILE *f = fopen(filepath, "r");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "uploaded file not found on storage");
    char buf[64] = {0};
    fgets(buf, sizeof(buf), f);
    fclose(f);
    TEST_ASSERT_EQUAL_STRING(upload_data, buf);

    unlink(filepath);
}

TEST_CASE("HTTP POST upload returns 400 when file already exists", "[http][endpoint]")
{
    ensure_server_started();

    char filepath[64];
    snprintf(filepath, sizeof(filepath), "%s/existing.txt", TEST_BASE_PATH);
    FILE *f = fopen(filepath, "w");
    TEST_ASSERT_NOT_NULL(f);
    fprintf(f, "already here");
    fclose(f);

    esp_http_client_config_t cfg = {
        .url           = "http://127.0.0.1/upload/existing.txt",
        .method        = HTTP_METHOD_POST,
        .timeout_ms    = 5000,
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    TEST_ASSERT_NOT_NULL(client);

    const char *data = "new content";
    esp_http_client_set_post_field(client, data, strlen(data));

    TEST_ASSERT_EQUAL(ESP_OK, esp_http_client_perform(client));
    TEST_ASSERT_EQUAL(400, esp_http_client_get_status_code(client));

    esp_http_client_cleanup(client);
    unlink(filepath);
}

TEST_CASE("HTTP POST delete removes file from storage", "[http][endpoint]")
{
    ensure_server_started();

    char filepath[64];
    snprintf(filepath, sizeof(filepath), "%s/to_delete.txt", TEST_BASE_PATH);
    FILE *f = fopen(filepath, "w");
    TEST_ASSERT_NOT_NULL(f);
    fprintf(f, "delete me");
    fclose(f);

    esp_http_client_config_t cfg = {
        .url                   = "http://127.0.0.1/delete/to_delete.txt",
        .method                = HTTP_METHOD_POST,
        .timeout_ms            = 5000,
        .disable_auto_redirect = true,
        .event_handler         = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    TEST_ASSERT_NOT_NULL(client);

    TEST_ASSERT_EQUAL(ESP_OK, esp_http_client_perform(client));
    int status = esp_http_client_get_status_code(client);
    TEST_ASSERT_TRUE_MESSAGE(status == 200 || status == 303, "expected 200 or 303 on delete");
    esp_http_client_cleanup(client);

    struct stat st;
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, stat(filepath, &st), "file should be gone after delete");
}

TEST_CASE("HTTP POST delete non-existent file returns 400", "[http][endpoint]")
{
    ensure_server_started();

    esp_http_client_config_t cfg = {
        .url           = "http://127.0.0.1/delete/ghost_file_xyz.txt",
        .method        = HTTP_METHOD_POST,
        .timeout_ms    = 5000,
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    TEST_ASSERT_NOT_NULL(client);

    TEST_ASSERT_EQUAL(ESP_OK, esp_http_client_perform(client));
    TEST_ASSERT_EQUAL(400, esp_http_client_get_status_code(client));

    esp_http_client_cleanup(client);
}

TEST_CASE("HTTP POST /api/ota without body returns 400", "[http][endpoint]")
{
    ensure_server_started();

    esp_http_client_config_t cfg = {
        .url           = "http://127.0.0.1/api/ota",
        .method        = HTTP_METHOD_POST,
        .timeout_ms    = 5000,
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    TEST_ASSERT_NOT_NULL(client);

    TEST_ASSERT_EQUAL(ESP_OK, esp_http_client_perform(client));
    TEST_ASSERT_EQUAL(400, esp_http_client_get_status_code(client));

    esp_http_client_cleanup(client);
}
