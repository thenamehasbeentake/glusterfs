/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __IOT_H
#define __IOT_H


#include "compat-errno.h"
#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "common-utils.h"
#include "list.h"
#include <stdlib.h>
#include "locking.h"
#include "iot-mem-types.h"
#include <semaphore.h>
#include "statedump.h"


struct iot_conf;

// 如果是用nsec， 虽然是long，在32位系统下，4个字节？ 最多21亿，可能会溢出变成负数
#define MAX_IDLE_SKEW                   4       /* In secs */
#define skew_sec_idle_time(sec)         ((sec) + (random () % MAX_IDLE_SKEW))
#define IOT_DEFAULT_IDLE                120     /* In secs. */

#define IOT_MIN_THREADS         1
#define IOT_DEFAULT_THREADS     16
#define IOT_MAX_THREADS         64


#define IOT_THREAD_STACK_SIZE   ((size_t)(256*1024))


typedef enum {
        IOT_PRI_HI = 0, /* low latency */
        IOT_PRI_NORMAL, /* normal */
        IOT_PRI_LO,     /* bulk */      //显得大
        IOT_PRI_LEAST,  /* least */     // 最小
        IOT_PRI_MAX,
} iot_pri_t;

typedef struct {
        struct list_head        clients;
        struct list_head        reqs;
} iot_client_ctx_t;

struct iot_conf {
        pthread_mutex_t      mutex;
        pthread_cond_t       cond;

        int32_t              max_count;   /* configured maximum */
        int32_t              curr_count;  /* actual number of threads running */
        int32_t              sleep_count;

        int32_t              idle_time;   /* in seconds */

        struct list_head     clients[IOT_PRI_MAX];
        /*
         * It turns out that there are several ways a frame can get to us
         * without having an associated client (server_first_lookup was the
         * first one I hit).  Instead of trying to update all such callers,
         * we use this to queue them.
         */
        // 事实证明，在没有关联客户端的情况下，框架可以通过多种方式到达我们这里（server_first_lookup 是我遇到的第一个）。
        //  我们没有尝试更新所有此类调用者，而是使用它来将它们排入队列。
        iot_client_ctx_t     no_client[IOT_PRI_MAX];

        int32_t              ac_iot_limit[IOT_PRI_MAX];
        int32_t              ac_iot_count[IOT_PRI_MAX];
        int                  queue_sizes[IOT_PRI_MAX];
        int                  queue_size;
        pthread_attr_t       w_attr;
        gf_boolean_t         least_priority; /*Enable/Disable least-priority */

        xlator_t            *this;
        size_t               stack_size;        // w_attr绑定 线程的栈大小
        gf_boolean_t         down; /*PARENT_DOWN event is notified*/
        gf_boolean_t         mutex_inited;      // mutex是否初始化
        gf_boolean_t         cond_inited;       // cond是否初始化
};

typedef struct iot_conf iot_conf_t;

#endif /* __IOT_H */
