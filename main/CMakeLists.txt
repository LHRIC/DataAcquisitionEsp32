idf_component_register(
    SRCS
    "esp-daq.cpp"
    INCLUDE_DIRS "."
    REQUIRES freertos driver fatfs unity xbee can daq_core sd control twai http
)
