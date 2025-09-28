#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "tasks/can.cpp"
#include "tasks/control.cpp"
#include "tasks/sd.cpp"
#include "tasks/xbee_rx.cpp"
#include "tasks/xbee_tx.cpp"

void app_main(void)
{
    xbee_rx_task_start(6, 4096, 1);
    xbee_tx_task_start(6, 4096, 1);
    can_task_start(6, 4096, 1);
    sd_task_start(6, 4096, 1);
    control_task_start(6, 4096, 1);
}
