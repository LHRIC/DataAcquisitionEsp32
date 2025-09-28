idf_component_register(SRCS "esp-daq.c" "tasks/xbee_rx.cpp" "tasks/xbee_tx.cpp" "tasks/control.cpp" "tasks/sd.cpp" "tasks/can.cpp"
                    INCLUDE_DIRS "."

                    REQUIRES freertos driver fatfs)
