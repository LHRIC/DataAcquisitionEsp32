idf_component_register(SRCS "esp-daq.cpp" "tasks/xbee_rx.cpp" "tasks/xbee_tx.cpp" "tasks/control.cpp" "tasks/sd.cpp" "tasks/can.cpp" "include/xbee.cpp" "include/buffer_pool.cpp"
                    INCLUDE_DIRS "."

                    REQUIRES freertos driver fatfs unity)
