/**
 * @file    main.h
 * @brief   工程主头文件与全局蓝牙状态定义
 * @details 定义系统级蓝牙服务器运行状态机枚举 `BT_SERVER_STS` 及全局状态变量。
 */

#ifndef __MAIN_H__
#define __MAIN_H__

#include "n32wb452.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

/**
 * @brief 蓝牙从机服务运行状态枚举
 */
typedef enum
{
    BT_IDLE = 0,        /**< 空闲复位状态 */
    BT_INITIALIZED,     /**< 协议栈已初始化完成 */
    BT_ADVERTISING,     /**< 正在发送广播等待主机连接 */
    BT_CONNECTED,       /**< 已与手机/中央设备建立连接 */
    BT_DISCONNECTED,    /**< 连接断开事件触发 */
    BT_STS_TOTAL        /**< 状态枚举总数计数 */
} BT_SERVER_STS;

/* 全局蓝牙连接状态 */
extern BT_SERVER_STS gBT_STS;

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H__ */
