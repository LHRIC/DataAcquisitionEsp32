#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <stdint.h>
#include <sys/param.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_app_desc.h"

#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Max length a file path can have on storage */
#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + CONFIG_SPIFFS_OBJ_NAME_LEN)

/* Max size of an individual file. Make sure this
 * value is same as that set in upload_script.html */
#define MAX_FILE_SIZE   (200*1024) // 200 KB
#define MAX_FILE_SIZE_STR "200KB"

/* Scratch buffer size */
#define SCRATCH_BUFSIZE 1024 

struct file_server_data {
    /* Base path of file storage */
    char base_path[ESP_VFS_PATH_MAX + 1];

    /* Scratch buffer for temporary storage during file transfer */
    char scratch[SCRATCH_BUFSIZE];
};

static const char *TAG = "file_server";
static struct file_server_data server_data;

#define LOG_STREAM_LINE_MAX 256
#define LOG_STREAM_CAPACITY 128

typedef int (*log_vprintf_fn_t)(const char *fmt, va_list ap);

typedef struct {
    uint32_t seq;
    char line[LOG_STREAM_LINE_MAX];
} log_stream_entry_t;

static portMUX_TYPE log_stream_lock = portMUX_INITIALIZER_UNLOCKED;
static log_stream_entry_t log_stream_buffer[LOG_STREAM_CAPACITY];
static uint32_t log_stream_write_seq;
static bool log_stream_ready;
static log_vprintf_fn_t log_stream_prev_vprintf;

static uint32_t log_stream_oldest_seq(void)
{
    uint32_t write_seq;

    taskENTER_CRITICAL(&log_stream_lock);
    write_seq = log_stream_write_seq;
    taskEXIT_CRITICAL(&log_stream_lock);

    if (write_seq > LOG_STREAM_CAPACITY) {
        return write_seq - LOG_STREAM_CAPACITY;
    }
    return 0;
}

static bool log_stream_pop_line(uint32_t *seq, char *line, size_t line_len)
{
    bool available = false;

    taskENTER_CRITICAL(&log_stream_lock);
    uint32_t write_seq = log_stream_write_seq;
    uint32_t oldest_seq = write_seq > LOG_STREAM_CAPACITY ? write_seq - LOG_STREAM_CAPACITY : 0;

    if (*seq < oldest_seq) {
        *seq = oldest_seq;
    }

    if (*seq < write_seq) {
        log_stream_entry_t *entry = &log_stream_buffer[*seq % LOG_STREAM_CAPACITY];
        if (entry->seq == *seq) {
            strlcpy(line, entry->line, line_len);
            *seq += 1;
            available = true;
        } else {
            *seq = write_seq;
        }
    }
    taskEXIT_CRITICAL(&log_stream_lock);

    return available;
}

static void log_stream_push_line(const char *line)
{
    taskENTER_CRITICAL(&log_stream_lock);
    uint32_t seq = log_stream_write_seq++;
    log_stream_entry_t *entry = &log_stream_buffer[seq % LOG_STREAM_CAPACITY];
    entry->seq = seq;
    strlcpy(entry->line, line, sizeof(entry->line));
    taskEXIT_CRITICAL(&log_stream_lock);
}

static int mirrored_vprintf(const char *fmt, va_list ap)
{
    char line[LOG_STREAM_LINE_MAX];
    va_list ap_copy;
    va_copy(ap_copy, ap);
    int len = vsnprintf(line, sizeof(line), fmt, ap_copy);
    va_end(ap_copy);

    if (log_stream_prev_vprintf) {
        log_stream_prev_vprintf(fmt, ap);
    }

    if (len > 0) {
        for (size_t i = 0; i < sizeof(line) && line[i] != '\0'; ++i) {
            if (line[i] == '\r' || line[i] == '\n') {
                line[i] = ' ';
            }
        }
        log_stream_push_line(line);
    }

    return len;
}

void log_stream_init(void)
{
    if (log_stream_ready) {
        return;
    }

    log_stream_prev_vprintf = esp_log_set_vprintf(mirrored_vprintf);
    log_stream_ready = true;
}

static int build_status_json(char *buffer, size_t buffer_len)
{
    wifi_sta_list_t sta_list = {0};
    uint16_t sta_count = 0;
    if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK) {
        sta_count = sta_list.num;
    }

    const esp_app_desc_t *app_desc = esp_app_get_description();
    uint64_t uptime_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);

    return snprintf(
        buffer,
        buffer_len,
        "{\"uptime_ms\":%" PRIu64 ",\"free_heap\":%u,\"largest_8bit_block\":%u,"
        "\"wifi_sta_count\":%u,\"version\":\"%s\",\"project\":\"%s\"}",
        uptime_ms,
        (unsigned)esp_get_free_heap_size(),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        (unsigned)sta_count,
        app_desc->version,
        app_desc->project_name
    );
}

static void delayed_restart_task(void *arg)
{
    uint32_t delay_ms = (uint32_t)(uintptr_t)arg;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    esp_restart();
}

static esp_err_t schedule_restart(uint32_t delay_ms)
{
    BaseType_t task_ok = xTaskCreate(
        delayed_restart_task, "delayed_restart", 2048, (void *)(uintptr_t)delay_ms, 5, NULL
    );
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create delayed_restart task");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    char payload[256];
    int written = build_status_json(payload, sizeof(payload));
    if (written < 0 || written >= (int)sizeof(payload)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Status payload too large");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, payload, written);
    return ESP_OK;
}

static esp_err_t monitor_stream_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");
    httpd_resp_set_hdr(req, "X-Accel-Buffering", "no");

    uint32_t next_seq = log_stream_oldest_seq();

    while (true) {
        char line[LOG_STREAM_LINE_MAX];
        bool sent_log = false;

        while (log_stream_pop_line(&next_seq, line, sizeof(line))) {
            char event_chunk[LOG_STREAM_LINE_MAX + 32];
            int event_len = snprintf(event_chunk, sizeof(event_chunk), "event: log\ndata: %s\n\n", line);
            if (event_len < 0 || event_len >= (int)sizeof(event_chunk)) {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Event payload too large");
                return ESP_FAIL;
            }

            if (httpd_resp_send_chunk(req, event_chunk, event_len) != ESP_OK) {
                return ESP_FAIL;
            }
            sent_log = true;
        }

        if (!sent_log) {
            if (httpd_resp_send_chunk(req, ": keepalive\n\n", strlen(": keepalive\n\n")) != ESP_OK) {
                return ESP_FAIL;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_HTTP_MONITOR_STREAM_INTERVAL_MS));
    }
}

static esp_err_t reset_post_handler(httpd_req_t *req)
{
    if (schedule_restart(500) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to schedule restart");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"message\":\"Restart scheduled\"}");
    return ESP_OK;
}

static esp_err_t ota_post_handler(httpd_req_t *req)
{
    if (req->content_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request body must contain firmware binary");
        return ESP_FAIL;
    }

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition available");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(update_partition, req->content_len, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    char *buffer = ((struct file_server_data *)req->user_ctx)->scratch;
    while (remaining > 0) {
        int received = httpd_req_recv(req, buffer, MIN(remaining, SCRATCH_BUFSIZE));
        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (received <= 0) {
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA receive failed");
            return ESP_FAIL;
        }
        err = esp_ota_write(ota_handle, buffer, received);
        if (err != ESP_OK) {
            esp_ota_abort(ota_handle);
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            return ESP_FAIL;
        }
        remaining -= received;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA finalize failed");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to switch boot partition");
        return ESP_FAIL;
    }

    if (schedule_restart(1000) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to schedule restart");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"message\":\"OTA applied, restarting\"}");
    return ESP_OK;
}

/* Handler to respond with an icon file embedded in flash.
 * Browsers expect to GET website icon at URI /favicon.ico.
 * This can be overridden by uploading file with same name */
static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    extern const unsigned char favicon_ico_start[] asm("_binary_favicon_ico_start");
    extern const unsigned char favicon_ico_end[]   asm("_binary_favicon_ico_end");
    const size_t favicon_ico_size = (favicon_ico_end - favicon_ico_start);
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_send(req, (const char *)favicon_ico_start, favicon_ico_size);
    return ESP_OK;
}

static esp_err_t dashboard_get_handler(httpd_req_t *req)
{
    extern const unsigned char dashboard_html_start[] asm("_binary_dashboard_html_start");
    extern const unsigned char dashboard_html_end[] asm("_binary_dashboard_html_end");
    const size_t dashboard_html_size = dashboard_html_end - dashboard_html_start;
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)dashboard_html_start, dashboard_html_size);
    return ESP_OK;
}

/* Send HTTP response with a run-time generated html consisting of
 * a list of all files and folders under the requested path.
 * In case of SPIFFS this returns empty list when path is any
 * string other than '/', since SPIFFS doesn't support directories */
static esp_err_t http_resp_dir_html(httpd_req_t *req, const char *dirpath, const char *uri_prefix)
{
    char entrypath[FILE_PATH_MAX];
    char entrysize[16];
    const char *entrytype;

    struct dirent *entry;
    struct stat entry_stat;

    DIR *dir = opendir(dirpath);
    const size_t dirpath_len = strlen(dirpath);

    /* Retrieve the base path of file storage to construct the full path */
    strlcpy(entrypath, dirpath, sizeof(entrypath));
    if (entrypath[dirpath_len - 1] != '/') {
        strlcat(entrypath, "/", sizeof(entrypath));
    }
    const size_t full_path_len = strlen(entrypath);

    if (!dir) {
        ESP_LOGE(TAG, "Failed to stat dir : %s", dirpath);
        /* Respond with 404 Not Found */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Directory does not exist");
        return ESP_FAIL;
    }

    /* Send HTML file header */
    httpd_resp_sendstr_chunk(req, "<!DOCTYPE html><html><body>");

    /* Get handle to embedded file upload script */
    extern const unsigned char upload_script_start[] asm("_binary_upload_script_html_start");
    extern const unsigned char upload_script_end[]   asm("_binary_upload_script_html_end");
    const size_t upload_script_size = (upload_script_end - upload_script_start);

    /* Add file upload form and script which on execution sends a POST request to /upload */
    httpd_resp_send_chunk(req, (const char *)upload_script_start, upload_script_size);

    /* Send file-list table definition and column labels */
    httpd_resp_sendstr_chunk(req,
        "<table class=\"fixed\" border=\"1\">"
        "<col width=\"800px\" /><col width=\"300px\" /><col width=\"300px\" /><col width=\"100px\" />"
        "<thead><tr><th>Name</th><th>Type</th><th>Size (Bytes)</th><th>Delete</th></tr></thead>"
        "<tbody>");

    /* Iterate over all files / folders and fetch their names and sizes */
    while ((entry = readdir(dir)) != NULL) {
        entrytype = (entry->d_type == DT_DIR ? "directory" : "file");

        strlcpy(entrypath + full_path_len, entry->d_name, sizeof(entrypath) - full_path_len);
        if (stat(entrypath, &entry_stat) == -1) {
            ESP_LOGE(TAG, "Failed to stat %s : %s", entrytype, entry->d_name);
            continue;
        }
        sprintf(entrysize, "%ld", entry_stat.st_size);
        ESP_LOGI(TAG, "Found %s : %s (%s bytes)", entrytype, entry->d_name, entrysize);

        /* Send chunk of HTML file containing table entries with file name and size */
        httpd_resp_sendstr_chunk(req, "<tr><td><a href=\"");
        httpd_resp_sendstr_chunk(req, uri_prefix);
        httpd_resp_sendstr_chunk(req, entry->d_name);
        if (entry->d_type == DT_DIR) {
            httpd_resp_sendstr_chunk(req, "/");
        }
        httpd_resp_sendstr_chunk(req, "\">");
        httpd_resp_sendstr_chunk(req, entry->d_name);
        httpd_resp_sendstr_chunk(req, "</a></td><td>");
        httpd_resp_sendstr_chunk(req, entrytype);
        httpd_resp_sendstr_chunk(req, "</td><td>");
        httpd_resp_sendstr_chunk(req, entrysize);
        httpd_resp_sendstr_chunk(req, "</td><td>");
        httpd_resp_sendstr_chunk(req, "<form method=\"post\" action=\"/delete");
        httpd_resp_sendstr_chunk(req, uri_prefix);
        httpd_resp_sendstr_chunk(req, entry->d_name);
        httpd_resp_sendstr_chunk(req, "\"><button type=\"submit\">Delete</button></form>");
        httpd_resp_sendstr_chunk(req, "</td></tr>\n");
    }
    closedir(dir);

    /* Finish the file list table */
    httpd_resp_sendstr_chunk(req, "</tbody></table>");

    /* Send remaining chunk of HTML file to complete it */
    httpd_resp_sendstr_chunk(req, "</body></html>");

    /* Send empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

#define IS_FILE_EXT(filename, ext) \
    (strcasecmp(&filename[strlen(filename) - sizeof(ext) + 1], ext) == 0)

/* Set HTTP response content type according to file extension */
static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filename)
{
    if (IS_FILE_EXT(filename, ".pdf")) {
        return httpd_resp_set_type(req, "application/pdf");
    } else if (IS_FILE_EXT(filename, ".html")) {
        return httpd_resp_set_type(req, "text/html");
    } else if (IS_FILE_EXT(filename, ".jpeg")) {
        return httpd_resp_set_type(req, "image/jpeg");
    } else if (IS_FILE_EXT(filename, ".ico")) {
        return httpd_resp_set_type(req, "image/x-icon");
    }
    /* This is a limited set only */
    /* For any other type always set as plain text */
    return httpd_resp_set_type(req, "text/plain");
}

/* Copies the full path into destination buffer and returns
 * pointer to path (skipping the preceding base path) */
static const char* get_path_from_uri(char *dest, const char *base_path, const char *uri, size_t destsize)
{
    const size_t base_pathlen = strlen(base_path);
    size_t pathlen = strlen(uri);

    const char *quest = strchr(uri, '?');
    if (quest) {
        pathlen = MIN(pathlen, quest - uri);
    }
    const char *hash = strchr(uri, '#');
    if (hash) {
        pathlen = MIN(pathlen, hash - uri);
    }

    if (base_pathlen + pathlen + 1 > destsize) {
        /* Full path string won't fit into destination buffer */
        return NULL;
    }

    /* Construct full path (base + path) */
    strcpy(dest, base_path);
    strlcpy(dest + base_pathlen, uri, pathlen + 1);

    /* Return pointer to path, skipping the base */
    return dest + base_pathlen;
}

/* Handler to download a file kept on the server */
static esp_err_t download_get_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    FILE *fd = NULL;
    struct stat file_stat;

    const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                             req->uri, sizeof(filepath));
    if (!filename) {
        ESP_LOGE(TAG, "Filename is too long");
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }

    if (strcmp(filename, "/") == 0 || strcmp(filename, "/index.html") == 0) {
        return dashboard_get_handler(req);
    }

    if (strcmp(filename, "/browser") == 0) {
        httpd_resp_set_status(req, "307 Temporary Redirect");
        httpd_resp_set_hdr(req, "Location", "/browser/");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    if (strcmp(filename, "/browser/") == 0) {
        return http_resp_dir_html(req, ((struct file_server_data *)req->user_ctx)->base_path, "/");
    }

    /* If name has trailing '/', respond with directory contents */
    if (filename[strlen(filename) - 1] == '/') {
        return http_resp_dir_html(req, filepath, req->uri);
    }

    if (stat(filepath, &file_stat) == -1) {
        /* If file not present on SPIFFS check if URI
         * corresponds to one of the hardcoded paths */
        if (strcmp(filename, "/dashboard") == 0 || strcmp(filename, "/dashboard.html") == 0) {
            return dashboard_get_handler(req);
        } else if (strcmp(filename, "/favicon.ico") == 0) {
            return favicon_get_handler(req);
        }
        
        ESP_LOGD(TAG, "File not found: %s. Redirecting to / for captive portal.", filepath);
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    fd = fopen(filepath, "r");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to read existing file : %s", filepath);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Sending file : %s (%ld bytes)...", filename, file_stat.st_size);
    set_content_type_from_file(req, filename);

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char *chunk = ((struct file_server_data *)req->user_ctx)->scratch;
    size_t chunksize;
    do {
        /* Read file in chunks into the scratch buffer */
        chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);

        if (chunksize > 0) {
            /* Send the buffer contents as HTTP response chunk */
            if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
                fclose(fd);
                ESP_LOGE(TAG, "File sending failed!");
                /* Abort sending file */
                httpd_resp_send_chunk(req, NULL, 0);
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
               return ESP_FAIL;
           }
        }

        /* Keep looping till the whole file is sent */
    } while (chunksize != 0);

    /* Close file after sending complete */
    fclose(fd);
    ESP_LOGI(TAG, "File sending complete");

    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* Handler to upload a file onto the server */
static esp_err_t upload_post_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    FILE *fd = NULL;
    struct stat file_stat;

    /* Skip leading "/upload" from URI to get filename */
    /* Note sizeof() counts NULL termination hence the -1 */
    const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                             req->uri + sizeof("/upload") - 1, sizeof(filepath));
    if (!filename) {
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }

    /* Filename cannot have a trailing '/' */
    if (filename[strlen(filename) - 1] == '/') {
        ESP_LOGE(TAG, "Invalid filename : %s", filename);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid filename");
        return ESP_FAIL;
    }

    if (stat(filepath, &file_stat) == 0) {
        ESP_LOGE(TAG, "File already exists : %s", filepath);
        /* Respond with 400 Bad Request */
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File already exists");
        return ESP_FAIL;
    }

    /* File cannot be larger than a limit */
    if (req->content_len > MAX_FILE_SIZE) {
        ESP_LOGE(TAG, "File too large : %d bytes", req->content_len);
        /* Respond with 400 Bad Request */
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "File size must be less than "
                            MAX_FILE_SIZE_STR "!");
        /* Return failure to close underlying connection else the
         * incoming file content will keep the socket busy */
        return ESP_FAIL;
    }

    fd = fopen(filepath, "w");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to create file : %s", filepath);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Receiving file : %s...", filename);

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char *buf = ((struct file_server_data *)req->user_ctx)->scratch;
    int received;

    /* Content length of the request gives
     * the size of the file being uploaded */
    int remaining = req->content_len;

    while (remaining > 0) {

        ESP_LOGI(TAG, "Remaining size : %d", remaining);
        /* Receive the file part by part into a buffer */
        if ((received = httpd_req_recv(req, buf, MIN(remaining, SCRATCH_BUFSIZE))) <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry if timeout occurred */
                continue;
            }

            /* In case of unrecoverable error,
             * close and delete the unfinished file*/
            fclose(fd);
            unlink(filepath);

            ESP_LOGE(TAG, "File reception failed!");
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive file");
            return ESP_FAIL;
        }

        /* Write buffer content to file on storage */
        if (received && (received != fwrite(buf, 1, received, fd))) {
            /* Couldn't write everything to file!
             * Storage may be full? */
            fclose(fd);
            unlink(filepath);

            ESP_LOGE(TAG, "File write failed!");
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write file to storage");
            return ESP_FAIL;
        }

        /* Keep track of remaining size of
         * the file left to be uploaded */
        remaining -= received;
    }

    /* Close file upon upload completion */
    fclose(fd);
    ESP_LOGI(TAG, "File reception complete");

    /* Redirect onto browser to see the updated file list */
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/browser/");
    httpd_resp_sendstr(req, "File uploaded successfully");
    return ESP_OK;
}

/* Handler to delete a file from the server */
static esp_err_t delete_post_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    struct stat file_stat;

    /* Skip leading "/delete" from URI to get filename */
    /* Note sizeof() counts NULL termination hence the -1 */
    const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                             req->uri  + sizeof("/delete") - 1, sizeof(filepath));
    if (!filename) {
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }

    /* Filename cannot have a trailing '/' */
    if (filename[strlen(filename) - 1] == '/') {
        ESP_LOGE(TAG, "Invalid filename : %s", filename);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid filename");
        return ESP_FAIL;
    }

    if (stat(filepath, &file_stat) == -1) {
        ESP_LOGE(TAG, "File does not exist : %s", filename);
        /* Respond with 400 Bad Request */
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File does not exist");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Deleting file : %s", filename);
    /* Delete file */
    unlink(filepath);

    /* Redirect onto browser to see the updated file list */
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/browser/");
    httpd_resp_sendstr(req, "File deleted successfully");
    return ESP_OK;
}

/* Function to start the file server */
esp_err_t start_file_server(const char *base_path)
{
    static httpd_handle_t server = NULL;

    if (server) {
        ESP_LOGE(TAG, "File server already started");
        return ESP_ERR_INVALID_STATE;
    }

    memset(&server_data, 0, sizeof(server_data));
    strlcpy(server_data.base_path, base_path, sizeof(server_data.base_path));

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    /* Use the URI wildcard matching function in order to
     * allow the same handler to respond to multiple different
     * target URIs which match the wildcard scheme */
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 4096;
    config.max_open_sockets = 6;
    config.max_uri_handlers = 8;

    ESP_LOGI(TAG, "Starting HTTP Server on port: '%d' stack=%u sockets=%u handlers=%u",
             config.server_port, (unsigned)config.stack_size,
             (unsigned)config.max_open_sockets, (unsigned)config.max_uri_handlers);
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start file server: %s", esp_err_to_name(err));
        server = NULL;
        return err;
    }

    httpd_uri_t status_get = {
        .uri      = "/api/status",
        .method   = HTTP_GET,
        .handler  = status_get_handler,
        .user_ctx = &server_data,
    };
    httpd_register_uri_handler(server, &status_get);

    httpd_uri_t monitor_stream_get = {
        .uri      = "/api/monitor",
        .method   = HTTP_GET,
        .handler  = monitor_stream_get_handler,
        .user_ctx = &server_data,
    };
    httpd_register_uri_handler(server, &monitor_stream_get);

    httpd_uri_t reset_post = {
        .uri      = "/api/reset",
        .method   = HTTP_POST,
        .handler  = reset_post_handler,
        .user_ctx = &server_data,
    };
    httpd_register_uri_handler(server, &reset_post);

    httpd_uri_t ota_post = {
        .uri      = "/api/ota",
        .method   = HTTP_POST,
        .handler  = ota_post_handler,
        .user_ctx = &server_data,
    };
    httpd_register_uri_handler(server, &ota_post);

    /* URI handler for uploading files to server */
    httpd_uri_t file_upload = {
        .uri       = "/upload/*",   // Match all URIs of type /upload/path/to/file
        .method    = HTTP_POST,
        .handler   = upload_post_handler,
        .user_ctx  = &server_data    // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_upload);

    /* URI handler for deleting files from server */
    httpd_uri_t file_delete = {
        .uri       = "/delete/*",   // Match all URIs of type /delete/path/to/file
        .method    = HTTP_POST,
        .handler   = delete_post_handler,
        .user_ctx  = &server_data    // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_delete);

    /* URI handler for getting uploaded files */
    httpd_uri_t file_download = {
        .uri       = "/*",  // Match all URIs of type /path/to/file
        .method    = HTTP_GET,
        .handler   = download_get_handler,
        .user_ctx  = &server_data    // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_download);

    return ESP_OK;
}
