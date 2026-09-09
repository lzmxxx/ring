#ifndef __MAIN_H__
#define __MAIN_H__

#include "n32wb452.h"
#include <stdint.h>
#include <stdbool.h>

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

typedef enum
{
    BT_IDLE = 0,
    BT_INITIALIZED,
    BT_ADVERTISING,
    BT_CONNECTED,
    BT_DISCONNECTED,
    BT_STS_TOTAL
} BT_SERVER_STS;

extern BT_SERVER_STS gBT_STS;

#endif /* __MAIN_H__ */
