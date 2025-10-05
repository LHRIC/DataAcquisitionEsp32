#include "can/can.hpp"
#include "daq_core/buffer_pool.hpp"
#include "xbee/uart.hpp"
#include "xbee/xbee_tx_task.hpp"

extern "C" void app_main(void)
{
    uart_init(false);
    pool_init();

    // xbee_rx_task_start(6, 4096, 1);
    xbee_tx_task_start(6, 4096, 1);
    can_task_start(6, 4096, 1);
    // sd_task_start(6, 4096, 1);
    // control_task_start(6, 4096, 1);
}
