idf_component_register(
  SRCS "test_http.cpp"
  INCLUDE_DIRS "."
  REQUIRES unity freertos sd http esp_http_client nvs_flash esp_netif esp_event spiffs
)
