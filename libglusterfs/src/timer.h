/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _TIMER_H
#define _TIMER_H

#include "glusterfs.h"
#include "xlator.h"
#include <sys/time.h>
#include <pthread.h>

typedef void (*gf_timer_cbk_t) (void *);

struct _gf_timer {
        union {
                struct list_head list;          // 链表头为gf_timer_registry_t::active
                struct {
                        struct _gf_timer *next;
                        struct _gf_timer *prev;
                };
        };
        struct timespec   at;                   // time out时间点
        gf_timer_cbk_t    callbk;               // 回调函数
        void             *data;                 // 回调参数
        xlator_t         *xl;                   // 回调xl
	gf_boolean_t      fired;                // 当前时间抵达at， fired true，即将触发cbk函数
};

struct _gf_timer_registry {
        pthread_t        th;            // timer执行线程tid
        char             fin;           // 线程结束标志，当fin不为空结束线程，并释放active链表指向的所有gf_timer_t资源
        struct list_head active;
        gf_lock_t        lock;
};

typedef struct _gf_timer gf_timer_t;
typedef struct _gf_timer_registry gf_timer_registry_t;

gf_timer_t *
gf_timer_call_after (glusterfs_ctx_t *ctx,
                     struct timespec delta,
                     gf_timer_cbk_t cbk,
                     void *data);

int32_t
gf_timer_call_cancel (glusterfs_ctx_t *ctx,
                      gf_timer_t *event);

void
gf_timer_registry_destroy (glusterfs_ctx_t *ctx);
#endif /* _TIMER_H */
