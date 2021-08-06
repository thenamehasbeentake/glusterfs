/*
  Copyright (c) 2012-2014 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "ec-mem-types.h"
#include "ec-helpers.h"
#include "ec-common.h"
#include "ec-data.h"
#include "ec-messages.h"

ec_cbk_data_t * ec_cbk_data_allocate(call_frame_t * frame, xlator_t * this,
                                     ec_fop_data_t * fop, int32_t id,
                                     int32_t idx, int32_t op_ret,
                                     int32_t op_errno)
{
    ec_cbk_data_t * cbk;
    ec_t * ec = this->private;

    if (fop->xl != this)
    {
        gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                EC_MSG_XLATOR_MISMATCH, "Mismatching xlators between request "
                "and answer (req=%s, ans=%s).", fop->xl->name, this->name);

        return NULL;
    }
    if (fop->frame != frame)
    {
        gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                EC_MSG_FRAME_MISMATCH, "Mismatching frames between request "
                                         "and answer (req=%p, ans=%p).",
                                         fop->frame, frame);

        return NULL;
    }
    if (fop->id != id)
    {
        gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                EC_MSG_FOP_MISMATCH, "Mismatching fops between request "
                                         "and answer (req=%d, ans=%d).",
                                         fop->id, id);

        return NULL;
    }

    cbk = mem_get0(ec->cbk_pool);
    if (cbk == NULL)
    {
        gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                EC_MSG_NO_MEMORY, "Failed to allocate memory for an "
                                         "answer.");
    }

    cbk->fop = fop;
    cbk->idx = idx;
    cbk->mask = 1ULL << idx;
    cbk->count = 1;
    cbk->op_ret = op_ret;
    cbk->op_errno = op_errno;
    INIT_LIST_HEAD (&cbk->entries.list);

    LOCK(&fop->lock);

    list_add_tail(&cbk->answer_list, &fop->answer_list);

    UNLOCK(&fop->lock);

    return cbk;
}

void ec_cbk_data_destroy(ec_cbk_data_t * cbk)
{
    if (cbk->xdata != NULL)
    {
        dict_unref(cbk->xdata);
    }
    if (cbk->dict != NULL)
    {
        dict_unref(cbk->dict);
    }
    if (cbk->inode != NULL)
    {
        inode_unref(cbk->inode);
    }
    if (cbk->fd != NULL)
    {
        fd_unref(cbk->fd);
    }
    if (cbk->buffers != NULL)
    {
        iobref_unref(cbk->buffers);
    }
    GF_FREE(cbk->vector);
    gf_dirent_free (&cbk->entries);
    GF_FREE (cbk->str);

    mem_put(cbk);
}

/* PARENT_DOWN will be notified to children only after these fops are complete
 * when graph switch happens.  We do not want graph switch to be waiting on
 * heal to complete as healing big file/directory could take a while. Which
 * will lead to hang on the mount.
 */
/*
PARENT_DOWN 只有在发生拓扑结构变化时，这些 fops 完成后才会通知孩子。 我们不希望拓扑结构变化等待修复完成，因为修复大文件/目录可能需要一段时间。 这将导致mount的客户端挂住。 
*/
static gf_boolean_t
ec_needs_graceful_completion (ec_fop_data_t *fop)
{
        if ((fop->id != EC_FOP_HEAL) && (fop->id != EC_FOP_FHEAL))
                return _gf_true;
        return _gf_false;
}

ec_fop_data_t * ec_fop_data_allocate(call_frame_t * frame, xlator_t * this,
                                     int32_t id, uint32_t flags,
                                     uintptr_t target, int32_t minimum,
                                     ec_wind_f wind, ec_handler_f handler,
                                     ec_cbk_t cbks, void * data)
{
    ec_fop_data_t * fop, * parent;          // 可能出现一个fop中执行了另一个fop的现象
    ec_t * ec = this->private;

    fop = mem_get0(ec->fop_pool);
    if (fop == NULL)
    {
        gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                EC_MSG_NO_MEMORY, "Failed to allocate memory for a "
                                         "request.");

        return NULL;
    }

    INIT_LIST_HEAD(&fop->cbk_list);
    INIT_LIST_HEAD(&fop->healer);
    INIT_LIST_HEAD(&fop->answer_list);
    INIT_LIST_HEAD(&fop->pending_list);
    INIT_LIST_HEAD(&fop->locks[0].owner_list);
    INIT_LIST_HEAD(&fop->locks[0].wait_list);
    INIT_LIST_HEAD(&fop->locks[1].owner_list);
    INIT_LIST_HEAD(&fop->locks[1].wait_list);

    fop->xl = this;
    fop->req_frame = frame;

    /* fops need a private frame to be able to execute some postop operations
     * even if the original fop has completed and reported back to the upper
     * xlator and it has destroyed the base frame.
     *
     * TODO: minimize usage of private frames. Reuse req_frame as much as
     *       possible.
     */
    if (frame != NULL)
    {
        fop->frame = copy_frame(frame);     // 从父frame中拷贝一个自己的frame，继承其中一些如uid之类的信息
    }
    else
    {
        fop->frame = create_frame(this, this->ctx->pool);   // 新建一个自己的frame
    }
    if (fop->frame == NULL)
    {
        gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                EC_MSG_NO_MEMORY, "Failed to create a private frame "
                                         "for a request");

        mem_put(fop);

        return NULL;
    }
    fop->id = id;               // GF_FOP_XXX
    fop->refs = 1;

    fop->flags = flags;         // 锁类型
    fop->minimum = minimum;
    fop->mask = target;

    fop->wind = wind;           // wind函数
    fop->handler = handler;     // 管理状态的处理函数
    fop->cbks = cbks;           // fop的回调函数
    fop->data = data;           // 回来看，随便看一些fop，wind下来的参数都为NULL

    fop->uid = fop->frame->root->uid;   // 继承root frame的uid和gid
    fop->gid = fop->frame->root->gid;

    LOCK_INIT(&fop->lock);

    fop->frame->local = fop;            // local frame等于自己

    if (frame != NULL)                  // 有上一个fop调用的fop
    {
        parent = frame->local;          // parent为上一个fop
        if (parent != NULL)
        {
            ec_sleep(parent);           // 父fop sleep， ref++和jobs++
        }

        fop->parent = parent;
    }
// 把修复的fop放入pending队列，避免因graph拓扑结构变化导致客户端来的fop卡住，
// 这个graph拓扑结构变化->PARENT_DOWN-> fop等待的过程 回来看
    if (ec_needs_graceful_completion (fop)) {   
            LOCK(&ec->lock);
            // 将fop->pending_list(new)插入 ec->pending_fops(head)链表的尾部
            list_add_tail(&fop->pending_list, &ec->pending_fops);

            UNLOCK(&ec->lock);
    }

    return fop;
}

void ec_fop_data_acquire(ec_fop_data_t * fop)
{
    LOCK(&fop->lock);

    ec_trace("ACQUIRE", fop, "");

    fop->refs++;

    UNLOCK(&fop->lock);
}

static void
ec_handle_last_pending_fop_completion (ec_fop_data_t *fop, gf_boolean_t *notify)
{
        ec_t *ec = fop->xl->private;

        if (!list_empty (&fop->pending_list)) {
                LOCK(&ec->lock);
                {
                        list_del_init (&fop->pending_list);
                        *notify = list_empty (&ec->pending_fops);
                }
                UNLOCK(&ec->lock);
        }
}

void
ec_fop_cleanup(ec_fop_data_t *fop)
{
        ec_cbk_data_t *cbk, *tmp;

        list_for_each_entry_safe(cbk, tmp, &fop->answer_list, answer_list) {
            list_del_init(&cbk->answer_list);

            ec_cbk_data_destroy(cbk);
        }
        INIT_LIST_HEAD(&fop->cbk_list);

        fop->answer = NULL;
}

void ec_fop_data_release(ec_fop_data_t * fop)
{
    ec_t *ec = NULL;
    int32_t refs;
    gf_boolean_t notify = _gf_false;

    LOCK(&fop->lock);

    ec_trace("RELEASE", fop, "");

    GF_ASSERT (fop->refs > 0);
    refs = --fop->refs;

    UNLOCK(&fop->lock);

    if (refs == 0)
    {
        fop->frame->local = NULL;
        STACK_DESTROY(fop->frame->root);

        LOCK_DESTROY(&fop->lock);

        if (fop->xdata != NULL)
        {
            dict_unref(fop->xdata);
        }
        if (fop->dict != NULL)
        {
            dict_unref(fop->dict);
        }
        if (fop->inode != NULL)
        {
            inode_unref(fop->inode);
        }
        if (fop->fd != NULL)
        {
            fd_unref(fop->fd);
        }
        if (fop->buffers != NULL)
        {
            iobref_unref(fop->buffers);
        }
        GF_FREE(fop->vector);
        GF_FREE(fop->str[0]);
        GF_FREE(fop->str[1]);
        loc_wipe(&fop->loc[0]);
        loc_wipe(&fop->loc[1]);

        ec_resume_parent(fop, fop->error);

        ec_fop_cleanup(fop);

        ec = fop->xl->private;
        ec_handle_last_pending_fop_completion (fop, &notify);
        ec_handle_healers_done (fop);
        mem_put(fop);
        if (notify) {
            ec_pending_fops_completed(ec);
        }
    }
}
