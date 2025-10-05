idf_component_register(
  SRCS "test_buffer_pool.cpp"
  INCLUDE_DIRS "."
  REQUIRES unity freertos daq_core esp_timer
)
