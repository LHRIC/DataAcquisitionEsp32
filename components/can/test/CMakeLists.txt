idf_component_register(
  SRCS
    "test_can.cpp"
    "test_can_extended.cpp"
  INCLUDE_DIRS "."
  REQUIRES unity freertos daq_core esp_timer can
)
