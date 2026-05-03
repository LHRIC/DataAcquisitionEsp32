#pragma once

#include "sdkconfig.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t start_file_server(const char *base_path);

esp_err_t spiffs_init(const char *base_path);

void wifi_init_softap(void);

void start_dns_server(void);


#ifdef __cplusplus
}
#endif
