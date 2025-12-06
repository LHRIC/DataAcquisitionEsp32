idf_component_register(
  SRCS "test_twai.cpp"
  INCLUDE_DIRS "."
  REQUIRES unity freertos daq_core esp_timer can xbee
)
