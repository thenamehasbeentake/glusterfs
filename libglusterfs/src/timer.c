/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "timer.h"
#include "logging.h"
#include "common-utils.h"
#include "globals.h"
#include "timespec.h"
#include "libglusterfs-messages.h"

/* fwd decl */
static gf_timer_registry_t *
gf_timer_registry_init (glusterfs_ctx_t *);

// 新建timer链进ctx->timer
gf_timer_t *
gf_timer_call_after (glusterfs_ctx_t *ctx,      // 取出ctx->timer(gf_timer_registry_t),在其中的active链表里面加入新建的gf_timer_t
                     struct timespec delta,     // 延迟时间，精确到纳秒
                     gf_timer_cbk_t callbk,     // 超时回调函数
                     void *data)                // 超时回调函数参数
{
        gf_timer_registry_t *reg = NULL;
        gf_timer_t *event = NULL;
        gf_timer_t *trav = NULL;
        uint64_t at = 0;

        if ((ctx == NULL) || (ctx->cleanup_started))    // cleanup_started， 可能是清除ctx的标识
        {
                gf_msg_callingfn ("timer", GF_LOG_ERROR, EINVAL,
                                  LG_MSG_INVALID_ARG, "Either ctx is NULL or"
                                  " ctx cleanup started");
                return NULL;
        }

        reg = gf_timer_registry_init (ctx);

        if (!reg) {
                gf_msg_callingfn ("timer", GF_LOG_ERROR, 0,
                                  LG_MSG_TIMER_REGISTER_ERROR, "!reg");
                return NULL;
        }

        event = GF_CALLOC (1, sizeof (*event), gf_common_mt_gf_timer_t);
        if (!event) {
                return NULL;
        }
        timespec_now (&event->at);
        timespec_adjust_delta (&event->at, delta);
        at = TS (event->at);
        event->callbk = callbk;
        event->data = data;
        event->xl = THIS;
        LOCK (&reg->lock);
        {
                list_for_each_entry_reverse (trav, &reg->active, list) {        // active链表按照时间从小到大插入，反序遍历，找到第一个时间
                        if (TS (trav->at) < at)
                                break;
                }
                list_add (&event->list, &trav->list);                   // 在适当的位置插入新建的timer任务
        }
        UNLOCK (&reg->lock);
        return event;
}


int32_t
gf_timer_call_cancel (glusterfs_ctx_t *ctx,
                      gf_timer_t *event)
{
        gf_timer_registry_t *reg = NULL;
        gf_boolean_t fired = _gf_false;

        if (ctx == NULL || event == NULL)
        {
                gf_msg_callingfn ("timer", GF_LOG_ERROR, EINVAL,
                                  LG_MSG_INVALID_ARG, "invalid argument");
                return 0;
        }

        if (ctx->cleanup_started) {
                gf_msg_callingfn ("timer", GF_LOG_INFO, 0,
                                  LG_MSG_CTX_CLEANUP_STARTED,
                                  "ctx cleanup started");
                return 0;
        }

        LOCK (&ctx->lock);
        {
                reg = ctx->timer;
        }
        UNLOCK (&ctx->lock);

        if (!reg) {
                /* This can happen when cleanup may have just started and
                 * gf_timer_registry_destroy() sets ctx->timer to NULL.
                 * Just bail out as success as gf_timer_proc() takes
                 * care of cleaning up the events.
                 */
                return 0;
        }

        LOCK (&reg->lock);
        {
                // 已经执行了，就不能取消了
                fired = event->fired;
                if (fired)
                        goto unlock;
                // 还没执行，从链表中删除。修改fired值与这个是同一个锁
                // 不过可能还没执行cbk，仅仅改变了fire值，但是还是没法取消
                list_del (&event->list);
        }
unlock:
        UNLOCK (&reg->lock);
        // 还没执行释放资源
        if (!fired) {
                GF_FREE (event);
                return 0;
        }
        return -1;
}

// timer线程， 顺序遍历双向链表，取出超时timer，执行cbk，释放timer资源
static void *
gf_timer_proc (void *data)
{
        gf_timer_registry_t *reg = data;
        const struct timespec sleepts = {.tv_sec = 1, .tv_nsec = 0, };
        gf_timer_t *event = NULL;
        gf_timer_t *tmp = NULL;
        xlator_t   *old_THIS = NULL;
        // 初始时未赋值reg->fin
        while (!reg->fin) {
                uint64_t now;
                struct timespec now_ts;

                timespec_now (&now_ts);
                now = TS (now_ts);      // 将struct timespec转换成usec LL
                while (1) {
                        uint64_t at;
                        char need_cbk = 0;      // 有timer timeout

                        LOCK (&reg->lock);
                        {       // 以&reg->active为链表头节点，tmp临时变量，安全遍历&reg->active指向的event类型
                                list_for_each_entry_safe (event,
                                             tmp, &reg->active, list) {
                                        at = TS (event->at);
                                        if (now >= at) {
                                                need_cbk = 1;
                                                // event->fired ， 该event可以触发
                                                event->fired = _gf_true;
                                                list_del (&event->list);        // 遍历到的当前event退出链表
                                                break;                          // 退出遍历，list_for_宏是一个for循环
                                        }
                                }
                        }
                        UNLOCK (&reg->lock);
                        if (need_cbk) {
                                old_THIS = NULL;
                                if (event->xl) {
                                        old_THIS = THIS;        // 暂存当前xlator
                                        THIS = event->xl;
                                }
                                event->callbk (event->data);    // 触发回调函数
                                GF_FREE (event);                // 释放event资源
                                if (old_THIS) {                 // 回退当前xlator
                                        THIS = old_THIS;
                                }
                        } else {
                                // 没有timer超时就跳出循环，nanosleep 1s
                                break;
                        }
                }
                nanosleep (&sleepts, NULL);     // 纳秒为单位的sleep，回来看
        }
        // fin, 释放timer资源
        LOCK (&reg->lock);
        {
                /* Do not call gf_timer_call_cancel(),
                 * it will lead to deadlock
                 */
                list_for_each_entry_safe (event, tmp, &reg->active, list) {
                        list_del (&event->list);
                        GF_FREE (event);
                }
        }
        UNLOCK (&reg->lock);
        LOCK_DESTROY (&reg->lock);

        return NULL;
}

// 初始化 glusterfs_ctx_t中的timer指针， 先malloc，后赋值。用的ctx->lock
// calloc gf_timer_registery_t类型的指针，初始化lock，active链表
// 创建timer线程，线程调用 gf_timer_proc
static gf_timer_registry_t *
gf_timer_registry_init (glusterfs_ctx_t *ctx)
{
        gf_timer_registry_t *reg = NULL;

        LOCK (&ctx->lock);
        {
                reg = ctx->timer;
                if (reg) {
                        UNLOCK (&ctx->lock);
                        goto out;
                }
                reg = GF_CALLOC (1, sizeof (*reg),
                              gf_common_mt_gf_timer_registry_t);
                if (!reg) {
                        UNLOCK (&ctx->lock);
                        goto out;
                }
                ctx->timer = reg;
                LOCK_INIT (&reg->lock);
                INIT_LIST_HEAD (&reg->active);
        }
        UNLOCK (&ctx->lock);
        gf_thread_create (&reg->th, NULL, gf_timer_proc, reg, "timer");
out:
        return reg;
}


void
gf_timer_registry_destroy (glusterfs_ctx_t *ctx)
{
        pthread_t thr_id;
        gf_timer_registry_t *reg = NULL;

        if (ctx == NULL)
                return;

        LOCK (&ctx->lock);
        {
                reg = ctx->timer;
                ctx->timer = NULL;
        }
        UNLOCK (&ctx->lock);

        if (!reg)
                return;

        thr_id = reg->th;
        // ctx->timer->fin = 1, 线程nanosleep之后就退出了
        reg->fin = 1;
        pthread_join (thr_id, NULL);
        GF_FREE (reg);
}
