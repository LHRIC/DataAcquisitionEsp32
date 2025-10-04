idf_component_register(
  SRCS "test_can.cpp"
  INCLUDE_DIRS "."
  REQUIRES unity freertos daq_core esp_timer can
)
