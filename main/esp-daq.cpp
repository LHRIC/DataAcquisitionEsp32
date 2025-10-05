#include "can/can.hpp"
#include "daq_core/buffer_pool.hpp"
#include "twai/twai.hpp"
#include "xbee/uart.hpp"
#include "xbee/xbee_tx_task.hpp"

extern "C" void app_main(void)
{
    pool_init();
    uart_init(false);
    twai_init();

    // xbee_rx_task_start(6, 4096, 1);
    xbee_tx_task_start(6, 4096, 1);
    can_task_start(6, 4096, 1);
    // sd_task_start(6, 4096, 1);
    // control_task_start(6, 4096, 1);
}
