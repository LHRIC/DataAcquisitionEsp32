#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "include/buffer_pool.hpp"
#include "include/xbee.hpp"
#include "tasks/can.hpp"
#include "tasks/control.hpp"
#include "tasks/sd.hpp"
#include "tasks/xbee_rx.hpp"
#include "tasks/xbee_tx.hpp"

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
