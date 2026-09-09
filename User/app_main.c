#include <rtthread.h>
#include "app_ble.h"
#include "app_power.h"
#include "app_ppg.h"
#include "app_spo2.h"
#include "app_device.h"
#include "app_record.h"
#include "bsp_power.h"

volatile uint8_t  g_cw2015_init_status = 0xFFU;

int main(void)
{
    /* 开机确认 */
    if (!app_power_boot_check())
    {
        app_power_enter_standby();
        return 0;
    }

    /* 模块初始化 */
    bsp_power_init();
    app_power_init();
    if (app_device_init() != 0)
    {
        app_power_off();
        return -1;
    }

    /* 创建并启动任务 */
    if ((app_ppg_init() != 0) ||
        (app_spo2_init() != 0) ||
        (app_record_init() != 0) ||
        (app_ble_init() != 0))
    {
        app_power_off();
        return -1;
    }

    /* 初始化完成，允许空闲线程进入低功耗。 */
    app_power_unlock(PM_SYSTEM);
    return 0;
}
