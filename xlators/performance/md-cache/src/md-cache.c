/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <glusterfs/timespec.h>
#include <glusterfs/glusterfs.h>
#include <glusterfs/defaults.h>
#include <glusterfs/logging.h>
#include <glusterfs/dict.h>
#include <glusterfs/xlator.h>
#include <glusterfs/syncop.h>
#include "md-cache-mem-types.h"
#include <glusterfs/compat-errno.h>
#include <glusterfs/glusterfs-acl.h>
#include <glusterfs/defaults.h>
#include <glusterfs/upcall-utils.h>
#include <assert.h>
#include <sys/time.h>
#include "md-cache-messages.h"
#include <glusterfs/statedump.h>
#include <glusterfs/atomic.h>

/* TODO:
   - cache symlink() link names and nuke symlink-cache
   - send proper postbuf in setattr_cbk even when op_ret = -1
*/

struct mdc_statfs_cache {
    pthread_mutex_t lock;                   // 缓存互斥锁
    gf_boolean_t initialized;               // 是否初始化
    struct timespec last_refreshed;         // 上次刷新时间戳
    struct statvfs buf;                     // cache
};

/*
struct statvfs
  {
    unsigned long int f_bsize;
    unsigned long int f_frsize;
#ifndef __USE_FILE_OFFSET64
    __fsblkcnt_t f_blocks;
    __fsblkcnt_t f_bfree;
    __fsblkcnt_t f_bavail;
    __fsfilcnt_t f_files;
    __fsfilcnt_t f_ffree;
    __fsfilcnt_t f_favail;
#else
    __fsblkcnt64_t f_blocks;
    __fsblkcnt64_t f_bfree;
    __fsblkcnt64_t f_bavail;
    __fsfilcnt64_t f_files;
    __fsfilcnt64_t f_ffree;
    __fsfilcnt64_t f_favail;
#endif
    unsigned long int f_fsid;
#ifdef _STATVFSBUF_F_UNUSED
    int __f_unused;
#endif
    unsigned long int f_flag;
    unsigned long int f_namemax;
    int __f_spare[6];
  };
*/

struct mdc_statistics {
    gf_atomic_t stat_hit; /* No. of times lookup/stat was served from
                             mdc */     // lookup/stat   mdc生效的次数

    gf_atomic_t stat_miss; /* No. of times valid stat wasn't present in
                              mdc */    // mdc未命中的次数

    gf_atomic_t xattr_hit; /* No. of times getxattr was served from mdc,
                              Note: this doesn't count the xattr served
                              from lookup */    // getxattr 命中次数

    gf_atomic_t xattr_miss;      /* No. of times xattr req was WIND from mdc */     // xattr请求从mdc wind下去的
    gf_atomic_t negative_lookup; /* No. of negative lookups */                      // 无效的lookup
    gf_atomic_t nameless_lookup; /* No. of negative lookups that were sent          // 无名的lookup，发送给brick
                                    to bricks */

    gf_atomic_t stat_invals;  /* No. of invalidates received from upcall */         // 从upcall收到的无效stat， iatt和xatt失效
    gf_atomic_t xattr_invals; /* No. of invalidates received from upcall */         // 从upcall收到的无效xattr
    gf_atomic_t need_lookup;  /* No. of lookups issued, because other               // 发出的lookup次数，由于其他xlator请求 明确的lookup
                                 xlators requested for explicit lookup */
};

struct mdc_conf {
    int timeout;                                // 超时
    gf_boolean_t cache_posix_acl;               // 是否缓存posix 访问控制
    gf_boolean_t cache_glusterfs_acl;           // 是否缓存glusterfs 访问控制
    gf_boolean_t cache_selinux;                 // 是否缓存selinux
    gf_boolean_t cache_capability;              // 是否缓存容量
    gf_boolean_t cache_ima;                     // 
    gf_boolean_t force_readdirp;                // 
    gf_boolean_t cache_swift_metadata;          // swift 对象元数据
    gf_boolean_t cache_samba_metadata;          // samba 网络共享元数据
    gf_boolean_t mdc_invalidation;              // 缓存失效
    gf_boolean_t global_invalidation;           // 全局失效

    time_t last_child_down;                     // 上一次 child xlator挂掉的时间
    gf_lock_t lock;                             // 锁
    struct mdc_statistics mdc_counter;          // 缓存命中或者失效计数
    gf_boolean_t cache_statfs;                  // 是否缓存statfs
    struct mdc_statfs_cache statfs_cache;       // statvfs的缓存
    char *mdc_xattr_str;                        // xattr
    gf_atomic_int32_t generation;               // 一代？版本？
};

struct mdc_local;
typedef struct mdc_local mdc_local_t;

#define MDC_STACK_UNWIND(fop, frame, params...)                                \
    do {                                                                       \
        mdc_local_t *__local = NULL;                                           \
        xlator_t *__xl = NULL;                                                 \
        if (frame) {                                                           \
            __xl = frame->this;                                                \
            __local = frame->local;                                            \
            frame->local = NULL;                                               \
        }                                                                      \
        STACK_UNWIND_STRICT(fop, frame, params);                               \
        mdc_local_wipe(__xl, __local);                                         \
    } while (0)

struct md_cache {
    ia_prot_t md_prot;              // 权限
    uint32_t md_nlink;              // link数
    uint32_t md_uid;                // uid
    uint32_t md_gid;                // gid
    uint32_t md_atime_nsec;         // 时间戳
    uint32_t md_mtime_nsec;
    uint32_t md_ctime_nsec;
    int64_t md_atime;
    int64_t md_mtime;
    int64_t md_ctime;
    uint64_t md_rdev;               // 设备
    uint64_t md_size;               // size大小
    uint64_t md_blocks;             // block数
    uint64_t generation;            // 版本？
    dict_t *xattr;                  // xattr字典
    char *linkname;                 // link的名字
    time_t ia_time;                 // ia时间？？
    time_t xa_time;
    gf_boolean_t need_lookup;       // 是否需要lookup
    gf_boolean_t valid;             // 是否有效
    gf_boolean_t gen_rollover;      // 轮转生产
    gf_boolean_t invalidation_rollover;         // 无效轮转
    gf_lock_t lock;                 // 锁
};

struct mdc_local {
    loc_t loc;
    loc_t loc2;
    fd_t *fd;
    char *linkname;
    char *key;
    dict_t *xattr;
    uint64_t incident_time;         // 时间时间
};

int
__mdc_inode_ctx_get(xlator_t *this, inode_t *inode, struct md_cache **mdc_p)
{
    int ret = 0;
    struct md_cache *mdc = NULL;
    uint64_t mdc_int = 0;

    ret = __inode_ctx_get(inode, this, &mdc_int);
    mdc = (void *)(long)(mdc_int);
    if (ret == 0 && mdc_p)
        *mdc_p = mdc;

    return ret;
}

int
mdc_inode_ctx_get(xlator_t *this, inode_t *inode, struct md_cache **mdc_p)
{
    int ret = -1;

    if (!inode)
        goto out;

    LOCK(&inode->lock);
    {
        ret = __mdc_inode_ctx_get(this, inode, mdc_p);
    }
    UNLOCK(&inode->lock);

out:
    return ret;
}

uint64_t
__mdc_inc_generation(xlator_t *this, struct md_cache *mdc)
{
    uint64_t gen = 0, rollover;
    struct mdc_conf *conf = NULL;

    conf = this->private;

    gen = GF_ATOMIC_INC(conf->generation);
    if (gen == 0) {
        mdc->gen_rollover = !mdc->gen_rollover;
        gen = GF_ATOMIC_INC(conf->generation);
        mdc->ia_time = 0;
        mdc->generation = 0;
    }

    rollover = mdc->gen_rollover;
    gen |= (rollover << 32);
    return gen;
}

uint64_t
mdc_inc_generation(xlator_t *this, inode_t *inode)
{
    struct mdc_conf *conf = NULL;
    uint64_t gen = 0;
    struct md_cache *mdc = NULL;

    conf = this->private;

    mdc_inode_ctx_get(this, inode, &mdc);

    if (mdc) {
        LOCK(&mdc->lock);
        {
            gen = __mdc_inc_generation(this, mdc);
        }
        UNLOCK(&mdc->lock);
    } else {
        gen = GF_ATOMIC_INC(conf->generation);
        if (gen == 0) {
            gen = GF_ATOMIC_INC(conf->generation);
        }
    }

    return gen;
}

uint64_t
mdc_get_generation(xlator_t *this, inode_t *inode)
{
    struct mdc_conf *conf = NULL;
    uint64_t gen = 0;
    struct md_cache *mdc = NULL;

    conf = this->private;

    mdc_inode_ctx_get(this, inode, &mdc);

    if (mdc) {
        LOCK(&mdc->lock);
        {
            gen = mdc->generation;
        }
        UNLOCK(&mdc->lock);
    } else
        gen = GF_ATOMIC_GET(conf->generation);

    return gen;
}

int
__mdc_inode_ctx_set(xlator_t *this, inode_t *inode, struct md_cache *mdc)
{
    int ret = 0;
    uint64_t mdc_int = 0;

    mdc_int = (long)mdc;
    ret = __inode_ctx_set(inode, this, &mdc_int);

    return ret;
}

int
mdc_inode_ctx_set(xlator_t *this, inode_t *inode, struct md_cache *mdc)
{
    int ret;

    LOCK(&inode->lock);
    {
        ret = __mdc_inode_ctx_set(this, inode, mdc);
    }
    UNLOCK(&inode->lock);

    return ret;
}

mdc_local_t *
mdc_local_get(call_frame_t *frame, inode_t *inode)
{
    mdc_local_t *local = NULL;

    local = frame->local;
    if (local)
        goto out;

    local = GF_CALLOC(sizeof(*local), 1, gf_mdc_mt_mdc_local_t);
    if (!local)
        goto out;

    local->incident_time = mdc_get_generation(frame->this, inode);
    frame->local = local;
out:
    return local;
}

void
mdc_local_wipe(xlator_t *this, mdc_local_t *local)
{
    if (!local)
        return;

    loc_wipe(&local->loc);

    loc_wipe(&local->loc2);

    if (local->fd)
        fd_unref(local->fd);

    GF_FREE(local->linkname);

    GF_FREE(local->key);

    if (local->xattr)
        dict_unref(local->xattr);

    GF_FREE(local);
    return;
}

int
mdc_inode_wipe(xlator_t *this, inode_t *inode)
{
    int ret = 0;
    uint64_t mdc_int = 0;
    struct md_cache *mdc = NULL;

    ret = inode_ctx_del(inode, this, &mdc_int);
    if (ret != 0)
        goto out;

    mdc = (void *)(long)mdc_int;

    if (mdc->xattr)
        dict_unref(mdc->xattr);

    GF_FREE(mdc->linkname);

    GF_FREE(mdc);

    ret = 0;
out:
    return ret;
}

struct md_cache *
mdc_inode_prep(xlator_t *this, inode_t *inode)
{
    int ret = 0;
    struct md_cache *mdc = NULL;

    LOCK(&inode->lock);
    {
        ret = __mdc_inode_ctx_get(this, inode, &mdc);
        if (ret == 0)
            goto unlock;

        mdc = GF_CALLOC(sizeof(*mdc), 1, gf_mdc_mt_md_cache_t);
        if (!mdc) {
            gf_msg(this->name, GF_LOG_ERROR, ENOMEM, MD_CACHE_MSG_NO_MEMORY,
                   "out of memory");
            goto unlock;
        }

        LOCK_INIT(&mdc->lock);

        ret = __mdc_inode_ctx_set(this, inode, mdc);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, ENOMEM, MD_CACHE_MSG_NO_MEMORY,
                   "out of memory");
            GF_FREE(mdc);
            mdc = NULL;
        }
    }
unlock:
    UNLOCK(&inode->lock);

    return mdc;
}

/* Cache is valid if:
 * - It is not cached before any brick was down. Brick down case is handled by
 *   invalidating all the cache when any brick went down.
 * - The cache time is not expired
 */
static gf_boolean_t
__is_cache_valid(xlator_t *this, time_t mdc_time)
{
    time_t now = 0;
    gf_boolean_t ret = _gf_true;
    struct mdc_conf *conf = NULL;
    int timeout = 0;
    time_t last_child_down = 0;

    conf = this->private;

    /* conf->lock here is not taken deliberately, so that the multi
     * threaded IO doesn't contend on a global lock. While updating
     * the variable, the lock is taken, so that at least the writes are
     * intact. The read of last_child_down may return junk, but that
     * is for a very short period of time.
     */
    last_child_down = conf->last_child_down;
    timeout = conf->timeout;

    time(&now);

    if ((mdc_time == 0) ||
        ((last_child_down != 0) && (mdc_time < last_child_down))) {
        ret = _gf_false;
        goto out;
    }

    if (now >= (mdc_time + timeout)) {
        ret = _gf_false;
    }

out:
    return ret;
}

static gf_boolean_t
is_md_cache_iatt_valid(xlator_t *this, struct md_cache *mdc)
{
    gf_boolean_t ret = _gf_true;

    LOCK(&mdc->lock);
    {
        if (mdc->valid == _gf_false) {
            ret = mdc->valid;
        } else {
            ret = __is_cache_valid(this, mdc->ia_time);
            if (ret == _gf_false) {
                mdc->ia_time = 0;
                mdc->generation = 0;
            }
        }
    }
    UNLOCK(&mdc->lock);

    return ret;
}

static gf_boolean_t
is_md_cache_xatt_valid(xlator_t *this, struct md_cache *mdc)
{
    gf_boolean_t ret = _gf_true;

    LOCK(&mdc->lock);
    {
        ret = __is_cache_valid(this, mdc->xa_time);
        if (ret == _gf_false)
            mdc->xa_time = 0;
    }
    UNLOCK(&mdc->lock);

    return ret;
}

void
mdc_from_iatt(struct md_cache *mdc, struct iatt *iatt)
{
    mdc->md_prot = iatt->ia_prot;
    mdc->md_nlink = iatt->ia_nlink;
    mdc->md_uid = iatt->ia_uid;
    mdc->md_gid = iatt->ia_gid;
    mdc->md_atime = iatt->ia_atime;
    mdc->md_atime_nsec = iatt->ia_atime_nsec;
    mdc->md_mtime = iatt->ia_mtime;
    mdc->md_mtime_nsec = iatt->ia_mtime_nsec;
    mdc->md_ctime = iatt->ia_ctime;
    mdc->md_ctime_nsec = iatt->ia_ctime_nsec;
    mdc->md_rdev = iatt->ia_rdev;
    mdc->md_size = iatt->ia_size;
    mdc->md_blocks = iatt->ia_blocks;
}

void
mdc_to_iatt(struct md_cache *mdc, struct iatt *iatt)
{
    iatt->ia_prot = mdc->md_prot;
    iatt->ia_nlink = mdc->md_nlink;
    iatt->ia_uid = mdc->md_uid;
    iatt->ia_gid = mdc->md_gid;
    iatt->ia_atime = mdc->md_atime;
    iatt->ia_atime_nsec = mdc->md_atime_nsec;
    iatt->ia_mtime = mdc->md_mtime;
    iatt->ia_mtime_nsec = mdc->md_mtime_nsec;
    iatt->ia_ctime = mdc->md_ctime;
    iatt->ia_ctime_nsec = mdc->md_ctime_nsec;
    iatt->ia_rdev = mdc->md_rdev;
    iatt->ia_size = mdc->md_size;
    iatt->ia_blocks = mdc->md_blocks;
}

int
mdc_inode_iatt_set_validate(xlator_t *this, inode_t *inode, struct iatt *prebuf,
                            struct iatt *iatt, gf_boolean_t update_time,
                            uint64_t incident_time)
{
    int ret = 0;
    struct md_cache *mdc = NULL;
    uint32_t rollover = 0;
    uint64_t gen = 0;
    gf_boolean_t update_xa_time = _gf_false;
    struct mdc_conf *conf = this->private;

    mdc = mdc_inode_prep(this, inode);
    if (!mdc) {
        ret = -1;
        goto out;
    }

    rollover = incident_time >> 32;
    incident_time = (incident_time & 0xffffffff);

    LOCK(&mdc->lock);
    {
        if (!iatt || !iatt->ia_ctime) {
            gf_msg_callingfn("md-cache", GF_LOG_TRACE, 0, 0,
                             "invalidating iatt(NULL)"
                             "(%s)",
                             uuid_utoa(inode->gfid));
            mdc->ia_time = 0;
            mdc->valid = 0;

            gen = __mdc_inc_generation(this, mdc);
            mdc->generation = (gen & 0xffffffff);
            goto unlock;
        }

        /* There could be a race in invalidation, where the
         * invalidations in order A, B reaches md-cache in the order
         * B, A. Hence, make sure the invalidation A is discarded if
         * it comes after B. ctime of a file is always in ascending
         * order unlike atime and mtime(which can be changed by user
         * to any date), also ctime gets updates when atime/mtime
         * changes, hence check for ctime only.
         */
        if (mdc->md_ctime > iatt->ia_ctime) {
            gf_msg_callingfn(this->name, GF_LOG_DEBUG, EINVAL,
                             MD_CACHE_MSG_DISCARD_UPDATE,
                             "discarding the iatt validate "
                             "request (%s)",
                             uuid_utoa(inode->gfid));
            ret = -1;
            goto unlock;
        }
        if ((mdc->md_ctime == iatt->ia_ctime) &&
            (mdc->md_ctime_nsec > iatt->ia_ctime_nsec)) {
            gf_msg_callingfn(this->name, GF_LOG_DEBUG, EINVAL,
                             MD_CACHE_MSG_DISCARD_UPDATE,
                             "discarding the iatt validate "
                             "request(ctime_nsec) (%s)",
                             uuid_utoa(inode->gfid));
            ret = -1;
            goto unlock;
        }

        /*
         * Invalidate the inode if the mtime or ctime has changed
         * and the prebuf doesn't match the value we have cached.
         * TODO: writev returns with a NULL iatt due to
         * performance/write-behind, causing invalidation on writes.
         */
        if ((iatt->ia_mtime != mdc->md_mtime) ||
            (iatt->ia_mtime_nsec != mdc->md_mtime_nsec) ||
            (iatt->ia_ctime != mdc->md_ctime) ||
            (iatt->ia_ctime_nsec != mdc->md_ctime_nsec)) {
            if (conf->global_invalidation &&
                (!prebuf || (prebuf->ia_mtime != mdc->md_mtime) ||
                 (prebuf->ia_mtime_nsec != mdc->md_mtime_nsec) ||
                 (prebuf->ia_ctime != mdc->md_ctime) ||
                 (prebuf->ia_ctime_nsec != mdc->md_ctime_nsec))) {
                if (IA_ISREG(inode->ia_type)) {
                    gf_msg("md-cache", GF_LOG_TRACE, 0,
                           MD_CACHE_MSG_DISCARD_UPDATE,
                           "prebuf doesn't match the value we have cached,"
                           " invalidate the inode(%s)",
                           uuid_utoa(inode->gfid));

                    inode_invalidate(inode);
                }
            } else {
                update_xa_time = _gf_true;
            }
        }

        if ((mdc->gen_rollover == rollover) &&
            (incident_time >= mdc->generation)) {
            mdc_from_iatt(mdc, iatt);
            mdc->valid = _gf_true;
            if (update_time) {
                time(&mdc->ia_time);

                if (mdc->xa_time && update_xa_time)
                    time(&mdc->xa_time);
            }

            gf_msg_callingfn(
                "md-cache", GF_LOG_TRACE, 0, MD_CACHE_MSG_CACHE_UPDATE,
                "Updated iatt(%s)"
                " time:%lld generation=%lld",
                uuid_utoa(iatt->ia_gfid), (unsigned long long)mdc->ia_time,
                (unsigned long long)mdc->generation);
        } else {
            gf_msg_callingfn("md-cache", GF_LOG_TRACE, 0, 0,
                             "not updating cache (%s)"
                             "mdc-rollover=%u rollover=%u "
                             "mdc-generation=%llu "
                             "mdc-ia_time=%llu incident_time=%llu ",
                             uuid_utoa(iatt->ia_gfid), mdc->gen_rollover,
                             rollover, (unsigned long long)mdc->generation,
                             (unsigned long long)mdc->ia_time,
                             (unsigned long long)incident_time);
        }
    }
unlock:
    UNLOCK(&mdc->lock);

out:
    return ret;
}

int
mdc_inode_iatt_set(xlator_t *this, inode_t *inode, struct iatt *iatt,
                   uint64_t incident_time)
{
    return mdc_inode_iatt_set_validate(this, inode, NULL, iatt, _gf_true,
                                       incident_time);
}

int
mdc_inode_iatt_get(xlator_t *this, inode_t *inode, struct iatt *iatt)
{
    int ret = -1;
    struct md_cache *mdc = NULL;

    if (mdc_inode_ctx_get(this, inode, &mdc) != 0) {
        gf_msg_trace("md-cache", 0, "mdc_inode_ctx_get failed (%s)",
                     uuid_utoa(inode->gfid));
        goto out;
    }

    if (!is_md_cache_iatt_valid(this, mdc)) {
        gf_msg_trace("md-cache", 0, "iatt cache not valid for (%s)",
                     uuid_utoa(inode->gfid));
        goto out;
    }

    LOCK(&mdc->lock);
    {
        mdc_to_iatt(mdc, iatt);
    }
    UNLOCK(&mdc->lock);

    gf_uuid_copy(iatt->ia_gfid, inode->gfid);
    iatt->ia_ino = gfid_to_ino(inode->gfid);
    iatt->ia_dev = 42;
    iatt->ia_type = inode->ia_type;

    ret = 0;
out:
    return ret;
}

struct updatedict {
    dict_t *dict;
    int ret;
};

static int
is_mdc_key_satisfied(xlator_t *this, const char *key)
{
    int ret = 0;
    char *pattern = NULL;
    struct mdc_conf *conf = this->private;
    char *mdc_xattr_str = NULL;
    char *tmp = NULL;
    char *tmp1 = NULL;

    if (!key)
        goto out;

    /* conf->mdc_xattr_str, is never freed and is hence safely used outside
     * of lock*/
    tmp1 = conf->mdc_xattr_str;
    if (!tmp1)
        goto out;

    mdc_xattr_str = gf_strdup(tmp1);
    if (!mdc_xattr_str)
        goto out;

    pattern = strtok_r(mdc_xattr_str, ",", &tmp);
    while (pattern) {
        gf_strTrim(&pattern);
        if (fnmatch(pattern, key, 0) == 0) {
            ret = 1;
            break;
        } else {
            gf_msg_trace("md-cache", 0,
                         "xattr key %s doesn't satisfy "
                         "caching requirements",
                         key);
        }
        pattern = strtok_r(NULL, ",", &tmp);
    }
    GF_FREE(mdc_xattr_str);
out:
    return ret;
}

static int
updatefn(dict_t *dict, char *key, data_t *value, void *data)
{
    struct updatedict *u = data;

    if (is_mdc_key_satisfied(THIS, key)) {
        if (!u->dict) {
            u->dict = dict_new();
            if (!u->dict) {
                u->ret = -1;
                return -1;
            }
        }

        if (dict_set(u->dict, key, value) < 0) {
            u->ret = -1;
            return -1;
        }
    }
    return 0;
}

static int
mdc_dict_update(dict_t **tgt, dict_t *src)
{
    struct updatedict u = {
        .dict = *tgt,
        .ret = 0,
    };

    dict_foreach(src, updatefn, &u);

    if (*tgt)
        return u.ret;

    if ((u.ret < 0) && u.dict) {
        dict_unref(u.dict);
        return u.ret;
    }

    *tgt = u.dict;

    return u.ret;
}

int
mdc_inode_xatt_set(xlator_t *this, inode_t *inode, dict_t *dict)
{
    int ret = -1;
    struct md_cache *mdc = NULL;
    dict_t *newdict = NULL;

    mdc = mdc_inode_prep(this, inode);
    if (!mdc)
        goto out;

    if (!dict) {
        gf_msg_trace("md-cache", 0,
                     "mdc_inode_xatt_set failed (%s) "
                     "dict NULL",
                     uuid_utoa(inode->gfid));
        goto out;
    }

    LOCK(&mdc->lock);
    {
        if (mdc->xattr) {
            gf_msg_trace("md-cache", 0,
                         "deleting the old xattr "
                         "cache (%s)",
                         uuid_utoa(inode->gfid));
            dict_unref(mdc->xattr);
            mdc->xattr = NULL;
        }

        ret = mdc_dict_update(&newdict, dict);
        if (ret < 0) {
            UNLOCK(&mdc->lock);
            goto out;
        }

        if (newdict)
            mdc->xattr = newdict;

        time(&mdc->xa_time);
        gf_msg_trace("md-cache", 0, "xatt cache set for (%s) time:%lld",
                     uuid_utoa(inode->gfid), (long long)mdc->xa_time);
    }
    UNLOCK(&mdc->lock);
    ret = 0;
out:
    return ret;
}

int
mdc_inode_xatt_update(xlator_t *this, inode_t *inode, dict_t *dict)
{
    int ret = -1;
    struct md_cache *mdc = NULL;

    mdc = mdc_inode_prep(this, inode);
    if (!mdc)
        goto out;

    if (!dict)
        goto out;

    LOCK(&mdc->lock);
    {
        ret = mdc_dict_update(&mdc->xattr, dict);
        if (ret < 0) {
            UNLOCK(&mdc->lock);
            goto out;
        }
    }
    UNLOCK(&mdc->lock);

    ret = 0;
out:
    return ret;
}

int
mdc_inode_xatt_unset(xlator_t *this, inode_t *inode, char *name)
{
    int ret = -1;
    struct md_cache *mdc = NULL;

    mdc = mdc_inode_prep(this, inode);
    if (!mdc)
        goto out;

    if (!name || !mdc->xattr)
        goto out;

    LOCK(&mdc->lock);
    {
        dict_del(mdc->xattr, name);
    }
    UNLOCK(&mdc->lock);

    ret = 0;
out:
    return ret;
}

int
mdc_inode_xatt_get(xlator_t *this, inode_t *inode, dict_t **dict)
{
    int ret = -1;
    struct md_cache *mdc = NULL;

    if (mdc_inode_ctx_get(this, inode, &mdc) != 0) {
        gf_msg_trace("md-cache", 0, "mdc_inode_ctx_get failed (%s)",
                     uuid_utoa(inode->gfid));
        goto out;
    }

    if (!is_md_cache_xatt_valid(this, mdc)) {
        gf_msg_trace("md-cache", 0, "xattr cache not valid for (%s)",
                     uuid_utoa(inode->gfid));
        goto out;
    }

    LOCK(&mdc->lock);
    {
        ret = 0;
        /* Missing xattr only means no keys were there, i.e
           a negative cache for the "loaded" keys
        */
        if (!mdc->xattr) {
            gf_msg_trace("md-cache", 0, "xattr not present (%s)",
                         uuid_utoa(inode->gfid));
            goto unlock;
        }

        if (dict)
            *dict = dict_ref(mdc->xattr);
    }
unlock:
    UNLOCK(&mdc->lock);

out:
    return ret;
}

gf_boolean_t
mdc_inode_reset_need_lookup(xlator_t *this, inode_t *inode)
{
    struct md_cache *mdc = NULL;
    gf_boolean_t need = _gf_false;

    if (mdc_inode_ctx_get(this, inode, &mdc) != 0)
        goto out;

    LOCK(&mdc->lock);
    {
        need = mdc->need_lookup;
        mdc->need_lookup = _gf_false;
    }
    UNLOCK(&mdc->lock);

out:
    return need;
}
// 明确需要lookup， 从inode的ctx中取出mdc，将其need_lookup置为need
void
mdc_inode_set_need_lookup(xlator_t *this, inode_t *inode, gf_boolean_t need)
{
    struct md_cache *mdc = NULL;

    if (mdc_inode_ctx_get(this, inode, &mdc) != 0)
        goto out;

    LOCK(&mdc->lock);
    {
        mdc->need_lookup = need;
    }
    UNLOCK(&mdc->lock);

out:
    return;
}

void
mdc_inode_iatt_invalidate(xlator_t *this, inode_t *inode)
{
    struct md_cache *mdc = NULL;
    uint32_t gen = 0;

    if (mdc_inode_ctx_get(this, inode, &mdc) != 0)
        goto out;

    gen = mdc_inc_generation(this, inode) & 0xffffffff;

    LOCK(&mdc->lock);
    {
        mdc->ia_time = 0;
        mdc->valid = _gf_false;
        mdc->generation = gen;
    }
    UNLOCK(&mdc->lock);

out:
    return;
}

int
mdc_inode_xatt_invalidate(xlator_t *this, inode_t *inode)
{
    int ret = -1;
    struct md_cache *mdc = NULL;

    if (mdc_inode_ctx_get(this, inode, &mdc) != 0)
        goto out;

    LOCK(&mdc->lock);
    {
        mdc->xa_time = 0;
    }
    UNLOCK(&mdc->lock);

out:
    return ret;
}
// 根据iatt中的gfid找到对应的inode，让其失效
static int
mdc_update_gfid_stat(xlator_t *this, struct iatt *iatt)
{
    int ret = 0;
    inode_table_t *itable = NULL;
    inode_t *inode = NULL;

    itable = ((xlator_t *)this->graph->top)->itable;
    inode = inode_find(itable, iatt->ia_gfid);
    if (!inode) {
        ret = -1;
        goto out;
    }
    ret = mdc_inode_iatt_set_validate(this, inode, NULL, iatt, _gf_true,
                                      mdc_inc_generation(this, inode));
out:
    return ret;
}
// 加载 cache_list到字典中
void
mdc_load_reqs(xlator_t *this, dict_t *dict)
{
    struct mdc_conf *conf = this->private;
    char *pattern = NULL;
    char *mdc_xattr_str = NULL;
    char *tmp = NULL;
    char *tmp1 = NULL;
    int ret = 0;

    tmp1 = conf->mdc_xattr_str;
    if (!tmp1)
        goto out;

    mdc_xattr_str = gf_strdup(tmp1);
    if (!mdc_xattr_str)
        goto out;

    pattern = strtok_r(mdc_xattr_str, ",", &tmp);
    while (pattern) {
        gf_strTrim(&pattern);       // trim 修剪，  去字符串开头和结尾的字符
        ret = dict_set_int8(dict, pattern, 0);          // 设置pattern 对应的value为0
        if (ret) {
            conf->mdc_xattr_str = NULL;
            gf_msg("md-cache", GF_LOG_ERROR, 0, MD_CACHE_MSG_NO_XATTR_CACHE,
                   "Disabled cache for xattrs, dict_set failed");
        }
        pattern = strtok_r(NULL, ",", &tmp);
    }

    GF_FREE(mdc_xattr_str);
out:
    return;
}

struct checkpair {
    int ret;
    dict_t *rsp;
};

static int
checkfn(dict_t *this, char *key, data_t *value, void *data)
{
    struct checkpair *pair = data;

    if (!is_mdc_key_satisfied(THIS, key))
        pair->ret = 0;

    return 0;
}

int
mdc_xattr_satisfied(xlator_t *this, dict_t *req, dict_t *rsp)
{
    struct checkpair pair = {
        .ret = 1,
        .rsp = rsp,
    };

    dict_foreach(req, checkfn, &pair);

    return pair.ret;
}

static void
mdc_cache_statfs(xlator_t *this, struct statvfs *buf)
{
    struct mdc_conf *conf = this->private;

    pthread_mutex_lock(&conf->statfs_cache.lock);
    {
        memcpy(&conf->statfs_cache.buf, buf, sizeof(struct statvfs));
        clock_gettime(CLOCK_MONOTONIC, &conf->statfs_cache.last_refreshed);
        conf->statfs_cache.initialized = _gf_true;
    }
    pthread_mutex_unlock(&conf->statfs_cache.lock);
}

int
mdc_load_statfs_info_from_cache(xlator_t *this, struct statvfs **buf)
{
    struct mdc_conf *conf = this->private;
    struct timespec now;
    double cache_age = 0.0;
    int ret = 0;

    if (!buf || !conf) {
        ret = -1;
        goto err;
    }

    *buf = NULL;
    timespec_now(&now);

    pthread_mutex_lock(&conf->statfs_cache.lock);
    {
        /* Skip if the cache is not initialized */
        if (!conf->statfs_cache.initialized) {
            ret = -1;
            goto unlock;
        }

        cache_age = (now.tv_sec - conf->statfs_cache.last_refreshed.tv_sec);

        gf_log(this->name, GF_LOG_DEBUG, "STATFS cache age = %lf", cache_age);
        if (cache_age > conf->timeout) {
            /* Expire the cache */
            gf_log(this->name, GF_LOG_DEBUG,
                   "Cache age %lf exceeded timeout %d", cache_age,
                   conf->timeout);
            ret = -1;
            goto unlock;
        }

        *buf = &conf->statfs_cache.buf;
    }
unlock:
    pthread_mutex_unlock(&conf->statfs_cache.lock);
err:
    return ret;
}

int
mdc_statfs_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct statvfs *buf,
               dict_t *xdata)
{
    struct mdc_conf *conf = this->private;
    mdc_local_t *local = NULL;

    local = frame->local;
    if (!local)
        goto out;

    if (op_ret != 0) {
        if ((op_errno == ENOENT) || (op_errno == ESTALE)) {
            mdc_inode_iatt_invalidate(this, local->loc.inode);
        }

        goto out;
    }

    if (conf && conf->cache_statfs) {
        mdc_cache_statfs(this, buf);
    }

out:
    MDC_STACK_UNWIND(statfs, frame, op_ret, op_errno, buf, xdata);

    return 0;
}

int
mdc_statfs(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    int ret = 0, op_ret = 0, op_errno = 0;
    struct statvfs *buf = NULL;
    mdc_local_t *local = NULL;
    struct mdc_conf *conf = this->private;

    local = mdc_local_get(frame, loc->inode);
    if (!local) {
        op_ret = -1;
        op_errno = ENOMEM;
        goto out;
    }

    loc_copy(&local->loc, loc);

    if (!conf) {
        goto uncached;
    }

    if (!conf->cache_statfs) {
        goto uncached;
    }

    ret = mdc_load_statfs_info_from_cache(this, &buf);
    if (ret == 0 && buf) {
        op_ret = 0;
        op_errno = 0;
        goto out;
    }

uncached:
    STACK_WIND(frame, mdc_statfs_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->statfs, loc, xdata);
    return 0;

out:
    MDC_STACK_UNWIND(statfs, frame, op_ret, op_errno, buf, xdata);
    return 0;
}

int
mdc_lookup_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, inode_t *inode,
               struct iatt *stbuf, dict_t *dict, struct iatt *postparent)
{
    mdc_local_t *local = NULL;
    struct mdc_conf *conf = this->private;

    local = frame->local;

    if (op_ret != 0) {
        if (op_errno == ENOENT)
            GF_ATOMIC_INC(conf->mdc_counter.negative_lookup);

        if (op_errno == ESTALE) {
            /* if op_errno is ENOENT, fuse-bridge will unlink the
             * dentry
             */
            if (local->loc.parent)
                mdc_inode_iatt_invalidate(this, local->loc.parent);
            else
                mdc_inode_iatt_invalidate(this, local->loc.inode);
        }

        goto out;
    }

    if (!local)
        goto out;

    if (local->loc.parent) {
        mdc_inode_iatt_set(this, local->loc.parent, postparent,
                           local->incident_time);
    }

    if (local->loc.inode) {
        mdc_inode_iatt_set(this, local->loc.inode, stbuf, local->incident_time);
        mdc_inode_xatt_set(this, local->loc.inode, dict);
    }
out:
    MDC_STACK_UNWIND(lookup, frame, op_ret, op_errno, inode, stbuf, dict,
                     postparent);
    return 0;
}

int
mdc_lookup(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    int ret = 0;
    struct iatt stbuf = {
        0,
    };
    struct iatt postparent = {
        0,
    };
    dict_t *xattr_rsp = NULL;
    dict_t *xattr_alloc = NULL;
    mdc_local_t *local = NULL;
    struct mdc_conf *conf = this->private;

    local = mdc_local_get(frame, loc->inode);
    if (!local) {
        GF_ATOMIC_INC(conf->mdc_counter.stat_miss);
        goto uncached;
    }

    loc_copy(&local->loc, loc);

    if (!inode_is_linked(loc->inode)) {
        GF_ATOMIC_INC(conf->mdc_counter.stat_miss);
        goto uncached;
    }

    if (mdc_inode_reset_need_lookup(this, loc->inode)) {
        GF_ATOMIC_INC(conf->mdc_counter.need_lookup);
        goto uncached;
    }

    ret = mdc_inode_iatt_get(this, loc->inode, &stbuf);
    if (ret != 0) {
        GF_ATOMIC_INC(conf->mdc_counter.stat_miss);
        goto uncached;
    }

    if (xdata) {
        ret = mdc_inode_xatt_get(this, loc->inode, &xattr_rsp);
        if (ret != 0) {
            GF_ATOMIC_INC(conf->mdc_counter.xattr_miss);
            goto uncached;
        }

        if (!mdc_xattr_satisfied(this, xdata, xattr_rsp)) {
            GF_ATOMIC_INC(conf->mdc_counter.xattr_miss);
            goto uncached;
        }
    }

    GF_ATOMIC_INC(conf->mdc_counter.stat_hit);
    MDC_STACK_UNWIND(lookup, frame, 0, 0, loc->inode, &stbuf, xattr_rsp,
                     &postparent);

    if (xattr_rsp)
        dict_unref(xattr_rsp);

    return 0;

uncached:
    if (!xdata)
        xdata = xattr_alloc = dict_new();
    if (xdata)
        mdc_load_reqs(this, xdata);

    STACK_WIND(frame, mdc_lookup_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->lookup, loc, xdata);

    if (xattr_rsp)
        dict_unref(xattr_rsp);
    if (xattr_alloc)
        dict_unref(xattr_alloc);
    return 0;
}

int
mdc_stat_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
             int32_t op_errno, struct iatt *buf, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = frame->local;
    if (!local)
        goto out;

    if (op_ret != 0) {
        if ((op_errno == ESTALE) || (op_errno == ENOENT)) {
            mdc_inode_iatt_invalidate(this, local->loc.inode);
        }

        goto out;
    }

    mdc_inode_iatt_set(this, local->loc.inode, buf, local->incident_time);
    mdc_inode_xatt_set(this, local->loc.inode, xdata);

out:
    MDC_STACK_UNWIND(stat, frame, op_ret, op_errno, buf, xdata);

    return 0;
}

int
mdc_stat(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    int ret;
    struct iatt stbuf;
    mdc_local_t *local = NULL;
    dict_t *xattr_alloc = NULL;
    struct mdc_conf *conf = this->private;

    local = mdc_local_get(frame, loc->inode);
    if (!local)
        goto uncached;

    loc_copy(&local->loc, loc);

    if (!inode_is_linked(loc->inode)) {
        GF_ATOMIC_INC(conf->mdc_counter.stat_miss);
        goto uncached;
    }

    ret = mdc_inode_iatt_get(this, loc->inode, &stbuf);
    if (ret != 0)
        goto uncached;

    GF_ATOMIC_INC(conf->mdc_counter.stat_hit);
    MDC_STACK_UNWIND(stat, frame, 0, 0, &stbuf, xdata);

    return 0;

uncached:
    if (!xdata)
        xdata = xattr_alloc = dict_new();
    if (xdata)
        mdc_load_reqs(this, xdata);

    GF_ATOMIC_INC(conf->mdc_counter.stat_miss);
    STACK_WIND(frame, mdc_stat_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->stat, loc, xdata);

    if (xattr_alloc)
        dict_unref(xattr_alloc);
    return 0;
}

int
mdc_fstat_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iatt *buf, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = frame->local;
    if (!local)
        goto out;

    if (op_ret != 0) {
        if ((op_errno == ENOENT) || (op_errno == ESTALE)) {
            mdc_inode_iatt_invalidate(this, local->fd->inode);
        }

        goto out;
    }

    mdc_inode_iatt_set(this, local->fd->inode, buf, local->incident_time);
    mdc_inode_xatt_set(this, local->fd->inode, xdata);

out:
    MDC_STACK_UNWIND(fstat, frame, op_ret, op_errno, buf, xdata);

    return 0;
}

int
mdc_fstat(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
    int ret;
    struct iatt stbuf;
    mdc_local_t *local = NULL;
    dict_t *xattr_alloc = NULL;
    struct mdc_conf *conf = this->private;

    local = mdc_local_get(frame, fd->inode);
    if (!local)
        goto uncached;

    local->fd = fd_ref(fd);

    ret = mdc_inode_iatt_get(this, fd->inode, &stbuf);
    if (ret != 0)
        goto uncached;

    GF_ATOMIC_INC(conf->mdc_counter.stat_hit);
    MDC_STACK_UNWIND(fstat, frame, 0, 0, &stbuf, xdata);

    return 0;

uncached:
    if (!xdata)
        xdata = xattr_alloc = dict_new();
    if (xdata)
        mdc_load_reqs(this, xdata);

    GF_ATOMIC_INC(conf->mdc_counter.stat_miss);
    STACK_WIND(frame, mdc_fstat_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fstat, fd, xdata);

    if (xattr_alloc)
        dict_unref(xattr_alloc);
    return 0;
}

int
mdc_truncate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                 struct iatt *postbuf, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = frame->local;

    if (!local)
        goto out;

    if (op_ret != 0) {
        if ((op_errno == ESTALE) || (op_errno == ENOENT))
            mdc_inode_iatt_invalidate(this, local->loc.inode);

        goto out;
    }

    mdc_inode_iatt_set_validate(this, local->loc.inode, prebuf, postbuf,
                                _gf_true, local->incident_time);

out:
    MDC_STACK_UNWIND(truncate, frame, op_ret, op_errno, prebuf, postbuf, xdata);

    return 0;
}

int
mdc_truncate(call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
             dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = mdc_local_get(frame, loc->inode);

    local->loc.inode = inode_ref(loc->inode);

    STACK_WIND(frame, mdc_truncate_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->truncate, loc, offset, xdata);
    return 0;
}

int
mdc_ftruncate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                  struct iatt *postbuf, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = frame->local;

    if (!local)
        goto out;

    if (op_ret != 0) {
        if ((op_errno == ENOENT) || (op_errno == ESTALE))
            mdc_inode_iatt_invalidate(this, local->fd->inode);

        goto out;
    }

    mdc_inode_iatt_set_validate(this, local->fd->inode, prebuf, postbuf,
                                _gf_true, local->incident_time);

out:
    MDC_STACK_UNWIND(ftruncate, frame, op_ret, op_errno, prebuf, postbuf,
                     xdata);

    return 0;
}

int
mdc_ftruncate(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
              dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = mdc_local_get(frame, fd->inode);

    local->fd = fd_ref(fd);

    STACK_WIND(frame, mdc_ftruncate_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->ftruncate, fd, offset, xdata);
    return 0;
}

int
mdc_mknod_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, inode_t *inode, struct iatt *buf,
              struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = frame->local;

    if (!local)
        goto out;

    if (op_ret != 0) {
        if ((op_errno == ESTALE) || (op_errno == ENOENT)) {
            mdc_inode_iatt_invalidate(this, local->loc.parent);
        }

        goto out;
    }

    if (local->loc.parent) {
        mdc_inode_iatt_set(this, local->loc.parent, postparent,
                           local->incident_time);
    }

    if (local->loc.inode) {
        mdc_inode_iatt_set(this, local->loc.inode, buf, local->incident_time);
    }
out:
    MDC_STACK_UNWIND(mknod, frame, op_ret, op_errno, inode, buf, preparent,
                     postparent, xdata);
    return 0;
}

int
mdc_mknod(call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
          dev_t rdev, mode_t umask, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = mdc_local_get(frame, loc->inode);

    loc_copy(&local->loc, loc);
    local->xattr = dict_ref(xdata);

    STACK_WIND(frame, mdc_mknod_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->mknod, loc, mode, rdev, umask, xdata);
    return 0;
}

int
mdc_mkdir_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, inode_t *inode, struct iatt *buf,
              struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = frame->local;

    if (!local)
        goto out;

    if (op_ret != 0) {
        if ((op_errno == ESTALE) || (op_errno == ENOENT)) {
            mdc_inode_iatt_invalidate(this, local->loc.parent);
        }

        goto out;
    }

    if (local->loc.parent) {
        mdc_inode_iatt_set(this, local->loc.parent, postparent,
                           local->incident_time);
    }

    if (local->loc.inode) {
        mdc_inode_iatt_set(this, local->loc.inode, buf, local->incident_time);
    }
out:
    MDC_STACK_UNWIND(mkdir, frame, op_ret, op_errno, inode, buf, preparent,
                     postparent, xdata);
    return 0;
}

int
mdc_mkdir(call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
          mode_t umask, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = mdc_local_get(frame, loc->inode);

    loc_copy(&local->loc, loc);
    local->xattr = dict_ref(xdata);

    STACK_WIND(frame, mdc_mkdir_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->mkdir, loc, mode, umask, xdata);
    return 0;
}

int
mdc_unlink_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *preparent,
               struct iatt *postparent, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = frame->local;

    if (!local)
        goto out;

    if (op_ret != 0) {
        /* if errno is ESTALE, parent is not present, which implies even
         * child is not present. Also, man 2 unlink states unlink can
         * return ENOENT if a component in pathname does not
         * exist or is a dangling symbolic link. So, invalidate both
         * parent and child for both errno
         */

        if ((op_errno == ENOENT) || (op_errno == ESTALE)) {
            mdc_inode_iatt_invalidate(this, local->loc.inode);
            mdc_inode_iatt_invalidate(this, local->loc.parent);
        }

        goto out;
    }

    if (local->loc.parent) {
        mdc_inode_iatt_set(this, local->loc.parent, postparent,
                           local->incident_time);
    }

    if (local->loc.inode) {
        mdc_inode_iatt_set(this, local->loc.inode, NULL, local->incident_time);
    }

out:
    MDC_STACK_UNWIND(unlink, frame, op_ret, op_errno, preparent, postparent,
                     xdata);
    return 0;
}

int
mdc_unlink(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t xflag,
           dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = mdc_local_get(frame, loc->inode);

    loc_copy(&local->loc, loc);

    STACK_WIND(frame, mdc_unlink_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->unlink, loc, xflag, xdata);
    return 0;
}

int
mdc_rmdir_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iatt *preparent, struct iatt *postparent,
              dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = frame->local;

    if (!local)
        goto out;

    if (op_ret != 0) {
        /* if errno is ESTALE, parent is not present, which implies even
         * child is not present. Also, man 2 rmdir states rmdir can
         * return ENOENT if a directory component in pathname does not
         * exist or is a dangling symbolic link. So, invalidate both
         * parent and child for both errno
         */

        if ((op_errno == ESTALE) || (op_errno == ENOENT)) {
            mdc_inode_iatt_invalidate(this, local->loc.inode);
            mdc_inode_iatt_invalidate(this, local->loc.parent);
        }

        goto out;
    }

    if (local->loc.parent) {
        mdc_inode_iatt_set(this, local->loc.parent, postparent,
                           local->incident_time);
    }

out:
    MDC_STACK_UNWIND(rmdir, frame, op_ret, op_errno, preparent, postparent,
                     xdata);
    return 0;
}

int
mdc_rmdir(call_frame_t *frame, xlator_t *this, loc_t *loc, int flag,
          dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = mdc_local_get(frame, loc->inode);

    loc_copy(&local->loc, loc);

    STACK_WIND(frame, mdc_rmdir_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->rmdir, loc, flag, xdata);
    return 0;
}

int
mdc_symlink_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, inode_t *inode,
                struct iatt *buf, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = frame->local;

    if (!local)
        goto out;

    if (op_ret != 0) {
        if ((op_errno == ESTALE) || (op_errno == ENOENT)) {
            mdc_inode_iatt_invalidate(this, local->loc.parent);
        }

        goto out;
    }

    if (local->loc.parent) {
        mdc_inode_iatt_set(this, local->loc.parent, postparent,
                           local->incident_time);
    }

    if (local->loc.inode) {
        mdc_inode_iatt_set(this, local->loc.inode, buf, local->incident_time);
    }
out:
    MDC_STACK_UNWIND(symlink, frame, op_ret, op_errno, inode, buf, preparent,
                     postparent, xdata);
    return 0;
}

int
mdc_symlink(call_frame_t *frame, xlator_t *this, const char *linkname,
            loc_t *loc, mode_t umask, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = mdc_local_get(frame, loc->inode);

    loc_copy(&local->loc, loc);

    local->linkname = gf_strdup(linkname);

    STACK_WIND(frame, mdc_symlink_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->symlink, linkname, loc, umask, xdata);
    return 0;
}

int
mdc_rename_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *buf,
               struct iatt *preoldparent, struct iatt *postoldparent,
               struct iatt *prenewparent, struct iatt *postnewparent,
               dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = frame->local;
    if (!local)
        goto out;

    if (op_ret != 0) {
        if ((op_errno == ESTALE) || (op_errno == ENOENT)) {
            mdc_inode_iatt_invalidate(this, local->loc.inode);
            mdc_inode_iatt_invalidate(this, local->loc2.parent);
        }

        goto out;
    }

    if (local->loc.parent) {
        mdc_inode_iatt_set(this, local->loc.parent, postoldparent,
                           local->incident_time);
    }

    if (local->loc.inode) {
        /* TODO: fix dht_rename() not to return linkfile
           attributes before setting attributes here
        */

        mdc_inode_iatt_set(this, local->loc.inode, NULL, local->incident_time);
    }

    if (local->loc2.parent) {
        mdc_inode_iatt_set(this, local->loc2.parent, postnewparent,
                           local->incident_time);
    }
out:
    MDC_STACK_UNWIND(rename, frame, op_ret, op_errno, buf, preoldparent,
                     postoldparent, prenewparent, postnewparent, xdata);
    return 0;
}

int
mdc_rename(call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
           dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = mdc_local_get(frame, oldloc->inode);

    loc_copy(&local->loc, oldloc);
    loc_copy(&local->loc2, newloc);

    STACK_WIND(frame, mdc_rename_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->rename, oldloc, newloc, xdata);
    return 0;
}

int
mdc_link_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
             int32_t op_errno, inode_t *inode, struct iatt *buf,
             struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = frame->local;

    if (!local)
        goto out;

    if (op_ret != 0) {
        if ((op_errno == ENOENT) || (op_errno == ESTALE)) {
            mdc_inode_iatt_invalidate(this, local->loc.inode);
            mdc_inode_iatt_invalidate(this, local->loc2.parent);
        }

        goto out;
    }

    if (local->loc.inode) {
        mdc_inode_iatt_set(this, local->loc.inode, buf, local->incident_time);
    }

    if (local->loc2.parent) {
        mdc_inode_iatt_set(this, local->loc2.parent, postparent,
                           local->incident_time);
    }
out:
    MDC_STACK_UNWIND(link, frame, op_ret, op_errno, inode, buf, preparent,
                     postparent, xdata);
    return 0;
}

int
mdc_link(call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
         dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = mdc_local_get(frame, oldloc->inode);

    loc_copy(&local->loc, oldloc);
    loc_copy(&local->loc2, newloc);

    STACK_WIND(frame, mdc_link_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->link, oldloc, newloc, xdata);
    return 0;
}

int
mdc_create_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
               struct iatt *buf, struct iatt *preparent,
               struct iatt *postparent, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = frame->local;

    if (!local)
        goto out;

    if (op_ret != 0) {
        if ((op_errno == ESTALE) || (op_errno == ENOENT)) {
            mdc_inode_iatt_invalidate(this, local->loc.parent);
        }

        goto out;
    }

    if (local->loc.parent) {
        mdc_inode_iatt_set(this, local->loc.parent, postparent,
                           local->incident_time);
    }

    if (local->loc.inode) {
        mdc_inode_iatt_set(this, inode, buf, local->incident_time);
    }
out:
    MDC_STACK_UNWIND(create, frame, op_ret, op_errno, fd, inode, buf, preparent,
                     postparent, xdata);
    return 0;
}

int
mdc_create(call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
           mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = mdc_local_get(frame, loc->inode);

    loc_copy(&local->loc, loc);
    local->xattr = dict_ref(xdata);

    STACK_WIND(frame, mdc_create_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->create, loc, flags, mode, umask, fd,
               xdata);
    return 0;
}

static int
mdc_open_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
             int32_t op_errno, fd_t *fd, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = frame->local;

    if (!local)
        goto out;

    if (op_ret != 0) {
        if ((op_errno == ESTALE) || (op_errno == ENOENT))
            mdc_inode_iatt_invalidate(this, local->loc.inode);
        goto out;
    }

    if (local->fd->flags & O_TRUNC) {
        /* O_TRUNC modifies file size. Hence invalidate the
         * cache entry to fetch latest attributes. */
        mdc_inode_iatt_invalidate(this, local->fd->inode);
    }

out:
    MDC_STACK_UNWIND(open, frame, op_ret, op_errno, fd, xdata);
    return 0;
}

static int
mdc_open(call_frame_t *frame, xlator_t *this, loc_t *loc, int flags, fd_t *fd,
         dict_t *xdata)
{
    mdc_local_t *local = NULL;

    if (!fd || !IA_ISREG(fd->inode->ia_type) || !(fd->flags & O_TRUNC)) {
        goto out;
    }

    local = mdc_local_get(frame, loc->inode);

    local->fd = fd_ref(fd);

out:
    STACK_WIND(frame, mdc_open_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->open, loc, flags, fd, xdata);
    return 0;
}

int
mdc_readv_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iovec *vector, int32_t count,
              struct iatt *stbuf, struct iobref *iobref, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = frame->local;
    if (!local)
        goto out;

    if (op_ret < 0) {
        if ((op_errno == ENOENT) || (op_errno == ESTALE))
            mdc_inode_iatt_invalidate(this, local->fd->inode);
        goto out;
    }

    mdc_inode_iatt_set(this, local->fd->inode, stbuf, local->incident_time);

out:
    MDC_STACK_UNWIND(readv, frame, op_ret, op_errno, vector, count, stbuf,
                     iobref, xdata);

    return 0;
}

int
mdc_readv(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
          off_t offset, uint32_t flags, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = mdc_local_get(frame, fd->inode);

    local->fd = fd_ref(fd);

    STACK_WIND(frame, mdc_readv_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->readv, fd, size, offset, flags, xdata);
    return 0;
}

int
mdc_writev_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
               struct iatt *postbuf, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = frame->local;
    if (!local)
        goto out;

    if (op_ret == -1) {
        if ((op_errno == ENOENT) || (op_errno == ESTALE))
            mdc_inode_iatt_invalidate(this, local->fd->inode);
        goto out;
    }

    mdc_inode_iatt_set_validate(this, local->fd->inode, prebuf, postbuf,
                                _gf_true, local->incident_time);

out:
    MDC_STACK_UNWIND(writev, frame, op_ret, op_errno, prebuf, postbuf, xdata);

    return 0;
}

int
mdc_writev(call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
           int count, off_t offset, uint32_t flags, struct iobref *iobref,
           dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = mdc_local_get(frame, fd->inode);

    local->fd = fd_ref(fd);

    STACK_WIND(frame, mdc_writev_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->writev, fd, vector, count, offset,
               flags, iobref, xdata);
    return 0;
}

int
mdc_setattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                struct iatt *postbuf, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = frame->local;

    if (op_ret != 0) {
        mdc_inode_iatt_set(this, local->loc.inode, NULL, local->incident_time);
        goto out;
    }

    if (!local)
        goto out;

    mdc_inode_iatt_set_validate(this, local->loc.inode, prebuf, postbuf,
                                _gf_true, local->incident_time);
    mdc_inode_xatt_update(this, local->loc.inode, xdata);

out:
    MDC_STACK_UNWIND(setattr, frame, op_ret, op_errno, prebuf, postbuf, xdata);

    return 0;
}

int
mdc_setattr(call_frame_t *frame, xlator_t *this, loc_t *loc, struct iatt *stbuf,
            int valid, dict_t *xdata)
{
    mdc_local_t *local = NULL;
    dict_t *xattr_alloc = NULL;
    int ret = 0;
    struct mdc_conf *conf = this->private;

    local = mdc_local_get(frame, loc->inode);

    loc_copy(&local->loc, loc);

    if ((valid & GF_SET_ATTR_MODE) && conf->cache_glusterfs_acl) {
        if (!xdata)
            xdata = xattr_alloc = dict_new();
        if (xdata) {
            ret = dict_set_int8(xdata, GF_POSIX_ACL_ACCESS, 0);
            if (!ret)
                ret = dict_set_int8(xdata, GF_POSIX_ACL_DEFAULT, 0);
            if (ret)
                mdc_inode_xatt_invalidate(this, local->loc.inode);
        }
    }

    if ((valid & GF_SET_ATTR_MODE) && conf->cache_posix_acl) {
        if (!xdata)
            xdata = xattr_alloc = dict_new();
        if (xdata) {
            ret = dict_set_int8(xdata, POSIX_ACL_ACCESS_XATTR, 0);
            if (!ret)
                ret = dict_set_int8(xdata, POSIX_ACL_DEFAULT_XATTR, 0);
            if (ret)
                mdc_inode_xatt_invalidate(this, local->loc.inode);
        }
    }

    STACK_WIND(frame, mdc_setattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->setattr, loc, stbuf, valid, xdata);

    if (xattr_alloc)
        dict_unref(xattr_alloc);
    return 0;
}

int
mdc_fsetattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                 struct iatt *postbuf, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = frame->local;
    if (!local)
        goto out;

    if (op_ret != 0) {
        if ((op_errno == ESTALE) || (op_errno == ENOENT))
            mdc_inode_iatt_invalidate(this, local->fd->inode);
        goto out;
    }

    mdc_inode_iatt_set_validate(this, local->fd->inode, prebuf, postbuf,
                                _gf_true, local->incident_time);
    mdc_inode_xatt_update(this, local->fd->inode, xdata);

out:
    MDC_STACK_UNWIND(fsetattr, frame, op_ret, op_errno, prebuf, postbuf, xdata);

    return 0;
}

int
mdc_fsetattr(call_frame_t *frame, xlator_t *this, fd_t *fd, struct iatt *stbuf,
             int valid, dict_t *xdata)
{
    mdc_local_t *local = NULL;
    dict_t *xattr_alloc = NULL;
    int ret = 0;
    struct mdc_conf *conf = this->private;

    local = mdc_local_get(frame, fd->inode);

    local->fd = fd_ref(fd);

    if ((valid & GF_SET_ATTR_MODE) && conf->cache_glusterfs_acl) {
        if (!xdata)
            xdata = xattr_alloc = dict_new();
        if (xdata) {
            ret = dict_set_int8(xdata, GF_POSIX_ACL_ACCESS, 0);
            if (!ret)
                ret = dict_set_int8(xdata, GF_POSIX_ACL_DEFAULT, 0);
            if (ret)
                mdc_inode_xatt_invalidate(this, local->fd->inode);
        }
    }

    if ((valid & GF_SET_ATTR_MODE) && conf->cache_posix_acl) {
        if (!xdata)
            xdata = xattr_alloc = dict_new();
        if (xdata) {
            ret = dict_set_int8(xdata, POSIX_ACL_ACCESS_XATTR, 0);
            if (!ret)
                ret = dict_set_int8(xdata, POSIX_ACL_DEFAULT_XATTR, 0);
            if (ret)
                mdc_inode_xatt_invalidate(this, local->fd->inode);
        }
    }

    STACK_WIND(frame, mdc_fsetattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fsetattr, fd, stbuf, valid, xdata);

    if (xattr_alloc)
        dict_unref(xattr_alloc);
    return 0;
}

int
mdc_fsync_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iatt *prebuf, struct iatt *postbuf,
              dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = frame->local;
    if (!local)
        goto out;

    if (op_ret != 0) {
        if ((op_errno == ENOENT) || (op_errno == ESTALE))
            mdc_inode_iatt_invalidate(this, local->fd->inode);
        goto out;
    }

    mdc_inode_iatt_set_validate(this, local->fd->inode, prebuf, postbuf,
                                _gf_true, local->incident_time);

out:
    MDC_STACK_UNWIND(fsync, frame, op_ret, op_errno, prebuf, postbuf, xdata);

    return 0;
}

int
mdc_fsync(call_frame_t *frame, xlator_t *this, fd_t *fd, int datasync,
          dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = mdc_local_get(frame, fd->inode);

    local->fd = fd_ref(fd);

    STACK_WIND(frame, mdc_fsync_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fsync, fd, datasync, xdata);
    return 0;
}

int
mdc_setxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    mdc_local_t *local = NULL;
    struct iatt prestat = {
        0,
    };
    struct iatt poststat = {
        0,
    };
    int ret = 0;

    local = frame->local;
    if (!local)
        goto out;

    if (op_ret != 0) {
        if ((op_errno == ENOENT) || (op_errno == ESTALE))
            mdc_inode_iatt_invalidate(this, local->loc.inode);
        goto out;
    }

    mdc_inode_xatt_update(this, local->loc.inode, local->xattr);

    ret = dict_get_iatt(xdata, GF_PRESTAT, &prestat);
    if (ret >= 0) {
        ret = dict_get_iatt(xdata, GF_POSTSTAT, &poststat);
        mdc_inode_iatt_set_validate(this, local->loc.inode, &prestat, &poststat,
                                    _gf_true, local->incident_time);
    }

    if (ret < 0)
        mdc_inode_iatt_invalidate(this, local->loc.inode);

out:
    MDC_STACK_UNWIND(setxattr, frame, op_ret, op_errno, xdata);

    return 0;
}

int
mdc_setxattr(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xattr,
             int flags, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = mdc_local_get(frame, loc->inode);

    loc_copy(&local->loc, loc);
    local->xattr = dict_ref(xattr);

    STACK_WIND(frame, mdc_setxattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->setxattr, loc, xattr, flags, xdata);

    return 0;
}

int
mdc_fsetxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    mdc_local_t *local = NULL;
    struct iatt prestat = {
        0,
    };
    struct iatt poststat = {
        0,
    };
    int ret = 0;

    local = frame->local;
    if (!local)
        goto out;

    if (op_ret != 0) {
        if ((op_errno == ESTALE) || (op_errno == ENOENT))
            mdc_inode_iatt_invalidate(this, local->fd->inode);
        goto out;
    }

    mdc_inode_xatt_update(this, local->fd->inode, local->xattr);

    ret = dict_get_iatt(xdata, GF_PRESTAT, &prestat);
    if (ret >= 0) {
        ret = dict_get_iatt(xdata, GF_POSTSTAT, &poststat);
        mdc_inode_iatt_set_validate(this, local->fd->inode, &prestat, &poststat,
                                    _gf_true, local->incident_time);
    }

    if (ret < 0)
        mdc_inode_iatt_invalidate(this, local->fd->inode);

out:
    MDC_STACK_UNWIND(fsetxattr, frame, op_ret, op_errno, xdata);

    return 0;
}

int
mdc_fsetxattr(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xattr,
              int flags, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = mdc_local_get(frame, fd->inode);

    local->fd = fd_ref(fd);
    local->xattr = dict_ref(xattr);

    STACK_WIND(frame, mdc_fsetxattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fsetxattr, fd, xattr, flags, xdata);

    return 0;
}

int
mdc_getxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *xattr, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = frame->local;
    if (!local)
        goto out;

    if (op_ret < 0) {
        if ((op_errno == ENOENT) || (op_errno == ESTALE))
            mdc_inode_iatt_invalidate(this, local->loc.inode);
        goto out;
    }

    if (dict_get(xattr, "glusterfs.skip-cache")) {
        gf_msg(this->name, GF_LOG_DEBUG, 0, 0,
               "Skipping xattr update due to empty value");
        goto out;
    }

    mdc_inode_xatt_set(this, local->loc.inode, xdata);

out:
    MDC_STACK_UNWIND(getxattr, frame, op_ret, op_errno, xattr, xdata);

    return 0;
}

int
mdc_getxattr(call_frame_t *frame, xlator_t *this, loc_t *loc, const char *key,
             dict_t *xdata)
{
    int ret;
    int op_errno = ENODATA;
    mdc_local_t *local = NULL;
    dict_t *xattr = NULL;
    struct mdc_conf *conf = this->private;
    dict_t *xattr_alloc = NULL;
    gf_boolean_t key_satisfied = _gf_true;

    local = mdc_local_get(frame, loc->inode);
    if (!local)
        goto uncached;

    loc_copy(&local->loc, loc);

    if (!is_mdc_key_satisfied(this, key)) {
        key_satisfied = _gf_false;
        goto uncached;
    }

    ret = mdc_inode_xatt_get(this, loc->inode, &xattr);
    if (ret != 0)
        goto uncached;

    if (!xattr || !dict_get(xattr, (char *)key)) {
        ret = -1;
        op_errno = ENODATA;
    }

    GF_ATOMIC_INC(conf->mdc_counter.xattr_hit);
    MDC_STACK_UNWIND(getxattr, frame, ret, op_errno, xattr, xdata);

    if (xattr)
        dict_unref(xattr);

    return 0;

uncached:
    if (key_satisfied) {
        if (!xdata)
            xdata = xattr_alloc = dict_new();
        if (xdata)
            mdc_load_reqs(this, xdata);
    }

    GF_ATOMIC_INC(conf->mdc_counter.xattr_miss);
    STACK_WIND(frame, mdc_getxattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->getxattr, loc, key, xdata);

    if (xattr_alloc)
        dict_unref(xattr_alloc);
    return 0;
}

int
mdc_fgetxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *xattr,
                  dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = frame->local;
    if (!local)
        goto out;

    if (op_ret < 0) {
        if ((op_errno == ENOENT) || (op_errno == ESTALE))
            mdc_inode_iatt_invalidate(this, local->fd->inode);
        goto out;
    }

    if (dict_get(xattr, "glusterfs.skip-cache")) {
        gf_msg(this->name, GF_LOG_DEBUG, 0, 0,
               "Skipping xattr update due to empty value");
        goto out;
    }

    mdc_inode_xatt_set(this, local->fd->inode, xdata);

out:
    MDC_STACK_UNWIND(fgetxattr, frame, op_ret, op_errno, xattr, xdata);

    return 0;
}

int
mdc_fgetxattr(call_frame_t *frame, xlator_t *this, fd_t *fd, const char *key,
              dict_t *xdata)
{
    int ret;
    mdc_local_t *local = NULL;
    dict_t *xattr = NULL;
    int op_errno = ENODATA;
    struct mdc_conf *conf = this->private;
    dict_t *xattr_alloc = NULL;
    gf_boolean_t key_satisfied = _gf_true;

    local = mdc_local_get(frame, fd->inode);
    if (!local)
        goto uncached;

    local->fd = fd_ref(fd);

    if (!is_mdc_key_satisfied(this, key)) {
        key_satisfied = _gf_false;
        goto uncached;
    }

    ret = mdc_inode_xatt_get(this, fd->inode, &xattr);
    if (ret != 0)
        goto uncached;

    if (!xattr || !dict_get(xattr, (char *)key)) {
        ret = -1;
        op_errno = ENODATA;
    }

    GF_ATOMIC_INC(conf->mdc_counter.xattr_hit);
    MDC_STACK_UNWIND(fgetxattr, frame, ret, op_errno, xattr, xdata);

    if (xattr)
        dict_unref(xattr);

    return 0;

uncached:
    if (key_satisfied) {
        if (!xdata)
            xdata = xattr_alloc = dict_new();
        if (xdata)
            mdc_load_reqs(this, xdata);
    }

    GF_ATOMIC_INC(conf->mdc_counter.xattr_miss);
    STACK_WIND(frame, mdc_fgetxattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fgetxattr, fd, key, xdata);

    if (xattr_alloc)
        dict_unref(xattr_alloc);
    return 0;
}

int
mdc_removexattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    mdc_local_t *local = NULL;
    struct iatt prestat = {
        0,
    };
    struct iatt poststat = {
        0,
    };
    int ret = 0;

    local = frame->local;
    if (!local)
        goto out;

    if (op_ret != 0) {
        if ((op_errno == ENOENT) || (op_errno == ESTALE))
            mdc_inode_iatt_invalidate(this, local->loc.inode);
        goto out;
    }

    if (local->key)
        mdc_inode_xatt_unset(this, local->loc.inode, local->key);
    else
        mdc_inode_xatt_invalidate(this, local->loc.inode);

    ret = dict_get_iatt(xdata, GF_PRESTAT, &prestat);
    if (ret >= 0) {
        ret = dict_get_iatt(xdata, GF_POSTSTAT, &poststat);
        mdc_inode_iatt_set_validate(this, local->loc.inode, &prestat, &poststat,
                                    _gf_true, local->incident_time);
    }

    if (ret < 0)
        mdc_inode_iatt_invalidate(this, local->loc.inode);
out:
    MDC_STACK_UNWIND(removexattr, frame, op_ret, op_errno, xdata);

    return 0;
}

int
mdc_removexattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
                const char *name, dict_t *xdata)
{
    mdc_local_t *local = NULL;
    int op_errno = ENODATA;
    int ret = 0;
    dict_t *xattr = NULL;
    struct mdc_conf *conf = this->private;

    local = mdc_local_get(frame, loc->inode);

    loc_copy(&local->loc, loc);

    local->key = gf_strdup(name);

    if (!is_mdc_key_satisfied(this, name))
        goto uncached;

    ret = mdc_inode_xatt_get(this, loc->inode, &xattr);
    if (ret != 0)
        goto uncached;

    GF_ATOMIC_INC(conf->mdc_counter.xattr_hit);

    if (!xattr || !dict_get(xattr, (char *)name)) {
        ret = -1;
        op_errno = ENODATA;

        MDC_STACK_UNWIND(removexattr, frame, ret, op_errno, xdata);
    } else {
        STACK_WIND(frame, mdc_removexattr_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->removexattr, loc, name, xdata);
    }

    if (xattr)
        dict_unref(xattr);

    return 0;

uncached:
    GF_ATOMIC_INC(conf->mdc_counter.xattr_miss);
    STACK_WIND(frame, mdc_removexattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->removexattr, loc, name, xdata);
    return 0;
}

int
mdc_fremovexattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    mdc_local_t *local = NULL;
    struct iatt prestat = {
        0,
    };
    struct iatt poststat = {
        0,
    };
    int ret = 0;

    local = frame->local;
    if (!local)
        goto out;

    if (op_ret != 0) {
        if ((op_errno == ENOENT) || (op_errno == ESTALE))
            mdc_inode_iatt_invalidate(this, local->fd->inode);
        goto out;
    }

    if (local->key)
        mdc_inode_xatt_unset(this, local->fd->inode, local->key);
    else
        mdc_inode_xatt_invalidate(this, local->fd->inode);

    ret = dict_get_iatt(xdata, GF_PRESTAT, &prestat);
    if (ret >= 0) {
        ret = dict_get_iatt(xdata, GF_POSTSTAT, &poststat);
        mdc_inode_iatt_set_validate(this, local->fd->inode, &prestat, &poststat,
                                    _gf_true, local->incident_time);
    }

    if (ret < 0)
        mdc_inode_iatt_invalidate(this, local->fd->inode);

out:
    MDC_STACK_UNWIND(fremovexattr, frame, op_ret, op_errno, xdata);

    return 0;
}

int
mdc_fremovexattr(call_frame_t *frame, xlator_t *this, fd_t *fd,
                 const char *name, dict_t *xdata)
{
    mdc_local_t *local = NULL;
    int op_errno = ENODATA;
    int ret = 0;
    dict_t *xattr = NULL;
    struct mdc_conf *conf = this->private;

    local = mdc_local_get(frame, fd->inode);

    local->fd = fd_ref(fd);

    local->key = gf_strdup(name);

    if (!is_mdc_key_satisfied(this, name))
        goto uncached;

    ret = mdc_inode_xatt_get(this, fd->inode, &xattr);
    if (ret != 0)
        goto uncached;

    GF_ATOMIC_INC(conf->mdc_counter.xattr_hit);

    if (!xattr || !dict_get(xattr, (char *)name)) {
        ret = -1;
        op_errno = ENODATA;

        MDC_STACK_UNWIND(fremovexattr, frame, ret, op_errno, xdata);
    } else {
        STACK_WIND(frame, mdc_fremovexattr_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->fremovexattr, fd, name, xdata);
    }

    if (xattr)
        dict_unref(xattr);

    return 0;

uncached:
    GF_ATOMIC_INC(conf->mdc_counter.xattr_miss);
    STACK_WIND(frame, mdc_fremovexattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fremovexattr, fd, name, xdata);
    return 0;
}

int32_t
mdc_opendir_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = frame->local;
    if (!local)
        goto out;

    if (op_ret == 0)
        goto out;

    if ((op_errno == ESTALE) || (op_errno == ENOENT))
        mdc_inode_iatt_invalidate(this, local->loc.inode);

out:
    MDC_STACK_UNWIND(opendir, frame, op_ret, op_errno, fd, xdata);
    return 0;
}

int
mdc_opendir(call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
            dict_t *xdata)
{
    dict_t *xattr_alloc = NULL;
    mdc_local_t *local = NULL;

    local = mdc_local_get(frame, loc->inode);

    loc_copy(&local->loc, loc);

    if (!xdata)
        xdata = xattr_alloc = dict_new();

    if (xdata) {
        /* Tell readdir-ahead to include these keys in xdata when it
         * internally issues readdirp() in it's opendir_cbk */
        mdc_load_reqs(this, xdata);
    }

    STACK_WIND(frame, mdc_opendir_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->opendir, loc, fd, xdata);

    if (xattr_alloc)
        dict_unref(xattr_alloc);

    return 0;
}

int
mdc_readdirp_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                 int op_errno, gf_dirent_t *entries, dict_t *xdata)
{
    gf_dirent_t *entry = NULL;
    mdc_local_t *local = NULL;

    local = frame->local;
    if (!local)
        goto unwind;

    if (op_ret <= 0) {
        if ((op_ret == -1) && ((op_errno == ENOENT) || (op_errno == ESTALE)))
            mdc_inode_iatt_invalidate(this, local->fd->inode);
        goto unwind;
    }

    list_for_each_entry(entry, &entries->list, list)
    {
        if (!entry->inode)
            continue;
        mdc_inode_iatt_set(this, entry->inode, &entry->d_stat,
                           local->incident_time);
        mdc_inode_xatt_set(this, entry->inode, entry->dict);
    }

unwind:
    MDC_STACK_UNWIND(readdirp, frame, op_ret, op_errno, entries, xdata);
    return 0;
}

int
mdc_readdirp(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t offset, dict_t *xdata)
{
    dict_t *xattr_alloc = NULL;
    mdc_local_t *local = NULL;

    local = mdc_local_get(frame, fd->inode);
    if (!local)
        goto out;

    local->fd = fd_ref(fd);

    if (!xdata)
        xdata = xattr_alloc = dict_new();
    if (xdata)
        mdc_load_reqs(this, xdata);

    STACK_WIND(frame, mdc_readdirp_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->readdirp, fd, size, offset, xdata);
    if (xattr_alloc)
        dict_unref(xattr_alloc);
    return 0;
out:
    MDC_STACK_UNWIND(readdirp, frame, -1, ENOMEM, NULL, NULL);
    return 0;
}

int
mdc_readdir_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                int op_errno, gf_dirent_t *entries, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = frame->local;
    if (!local)
        goto out;

    if (op_ret == 0)
        goto out;

    if ((op_errno == ESTALE) || (op_errno == ENOENT))
        mdc_inode_iatt_invalidate(this, local->fd->inode);
out:
    MDC_STACK_UNWIND(readdir, frame, op_ret, op_errno, entries, xdata);
    return 0;
}

int
mdc_readdir(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
            off_t offset, dict_t *xdata)
{
    int need_unref = 0;
    mdc_local_t *local = NULL;
    struct mdc_conf *conf = this->private;

    local = mdc_local_get(frame, fd->inode);
    if (!local)
        goto unwind;

    local->fd = fd_ref(fd);

    if (!conf->force_readdirp) {
        STACK_WIND(frame, mdc_readdir_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->readdir, fd, size, offset, xdata);
        return 0;
    }

    if (!xdata) {
        xdata = dict_new();
        need_unref = 1;
    }

    if (xdata)
        mdc_load_reqs(this, xdata);

    STACK_WIND(frame, mdc_readdirp_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->readdirp, fd, size, offset, xdata);

    if (need_unref && xdata)
        dict_unref(xdata);

    return 0;
unwind:
    MDC_STACK_UNWIND(readdir, frame, -1, ENOMEM, NULL, NULL);
    return 0;
}

int
mdc_fallocate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                  struct iatt *postbuf, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = frame->local;
    if (!local)
        goto out;

    if (op_ret != 0) {
        if ((op_errno == ENOENT) || (op_errno == ESTALE))
            mdc_inode_iatt_invalidate(this, local->fd->inode);
        goto out;
    }

    mdc_inode_iatt_set_validate(this, local->fd->inode, prebuf, postbuf,
                                _gf_true, local->incident_time);

out:
    MDC_STACK_UNWIND(fallocate, frame, op_ret, op_errno, prebuf, postbuf,
                     xdata);

    return 0;
}

int
mdc_fallocate(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t mode,
              off_t offset, size_t len, dict_t *xdata)
{
    mdc_local_t *local;

    local = mdc_local_get(frame, fd->inode);
    local->fd = fd_ref(fd);

    STACK_WIND(frame, mdc_fallocate_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fallocate, fd, mode, offset, len,
               xdata);

    return 0;
}

int
mdc_discard_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                struct iatt *postbuf, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = frame->local;
    if (!local)
        goto out;

    if (op_ret != 0) {
        if ((op_errno == ENOENT) || (op_errno == ESTALE))
            mdc_inode_iatt_invalidate(this, local->fd->inode);
        goto out;
    }

    mdc_inode_iatt_set_validate(this, local->fd->inode, prebuf, postbuf,
                                _gf_true, local->incident_time);

out:
    MDC_STACK_UNWIND(discard, frame, op_ret, op_errno, prebuf, postbuf, xdata);

    return 0;
}

int
mdc_discard(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
            size_t len, dict_t *xdata)
{
    mdc_local_t *local;

    local = mdc_local_get(frame, fd->inode);
    local->fd = fd_ref(fd);

    STACK_WIND(frame, mdc_discard_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->discard, fd, offset, len, xdata);

    return 0;
}

int
mdc_zerofill_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                 struct iatt *postbuf, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = frame->local;
    if (!local)
        goto out;

    if (op_ret != 0) {
        if ((op_errno == ENOENT) || (op_errno == ESTALE))
            mdc_inode_iatt_invalidate(this, local->fd->inode);
        goto out;
    }

    mdc_inode_iatt_set_validate(this, local->fd->inode, prebuf, postbuf,
                                _gf_true, local->incident_time);

out:
    MDC_STACK_UNWIND(zerofill, frame, op_ret, op_errno, prebuf, postbuf, xdata);

    return 0;
}

int
mdc_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
             off_t len, dict_t *xdata)
{
    mdc_local_t *local;

    local = mdc_local_get(frame, fd->inode);
    local->fd = fd_ref(fd);

    STACK_WIND(frame, mdc_zerofill_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->zerofill, fd, offset, len, xdata);

    return 0;
}

int32_t
mdc_readlink_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, const char *path,
                 struct iatt *buf, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = frame->local;
    if (!local)
        goto out;

    if (op_ret == 0)
        goto out;

    if ((op_errno == ENOENT) || (op_errno == ESTALE))
        mdc_inode_iatt_invalidate(this, local->loc.inode);

out:
    MDC_STACK_UNWIND(readlink, frame, op_ret, op_errno, path, buf, xdata);
    return 0;
}

int32_t
mdc_readlink(call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size,
             dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = mdc_local_get(frame, loc->inode);
    if (!local)
        goto unwind;

    loc_copy(&local->loc, loc);

    STACK_WIND(frame, mdc_readlink_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->readlink, loc, size, xdata);
    return 0;

unwind:
    MDC_STACK_UNWIND(readlink, frame, -1, ENOMEM, NULL, NULL, NULL);
    return 0;
}

int32_t
mdc_fsyncdir_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = frame->local;
    if (!local)
        goto out;

    if (op_ret == 0)
        goto out;

    if ((op_errno == ESTALE) || (op_errno == ENOENT))
        mdc_inode_iatt_invalidate(this, local->fd->inode);

out:
    MDC_STACK_UNWIND(fsyncdir, frame, op_ret, op_errno, xdata);
    return 0;
}

int32_t
mdc_fsyncdir(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags,
             dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = mdc_local_get(frame, fd->inode);
    if (!local)
        goto unwind;

    local->fd = fd_ref(fd);

    STACK_WIND(frame, mdc_fsyncdir_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fsyncdir, fd, flags, xdata);
    return 0;

unwind:
    MDC_STACK_UNWIND(fsyncdir, frame, -1, ENOMEM, NULL);
    return 0;
}

int32_t
mdc_access_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = frame->local;
    if (!local)
        goto out;

    if (op_ret == 0)
        goto out;

    if ((op_errno == ESTALE) || (op_errno == ENOENT))
        mdc_inode_iatt_invalidate(this, local->loc.inode);

out:
    MDC_STACK_UNWIND(access, frame, op_ret, op_errno, xdata);
    return 0;
}

int32_t
mdc_access(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask,
           dict_t *xdata)
{
    mdc_local_t *local = NULL;

    local = mdc_local_get(frame, loc->inode);
    if (!local)
        goto unwind;

    loc_copy(&local->loc, loc);

    STACK_WIND(frame, mdc_access_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->access, loc, mask, xdata);
    return 0;

unwind:
    MDC_STACK_UNWIND(access, frame, -1, ENOMEM, NULL);
    return 0;
}

int
mdc_priv_dump(xlator_t *this)
{
    struct mdc_conf *conf = NULL;
    char key_prefix[GF_DUMP_MAX_BUF_LEN];

    conf = this->private;

    snprintf(key_prefix, GF_DUMP_MAX_BUF_LEN, "%s.%s", this->type, this->name);
    gf_proc_dump_add_section("%s", key_prefix);

    gf_proc_dump_write("stat_hit_count", "%" PRId64,
                       GF_ATOMIC_GET(conf->mdc_counter.stat_hit));
    gf_proc_dump_write("stat_miss_count", "%" PRId64,
                       GF_ATOMIC_GET(conf->mdc_counter.stat_miss));
    gf_proc_dump_write("xattr_hit_count", "%" PRId64,
                       GF_ATOMIC_GET(conf->mdc_counter.xattr_hit));
    gf_proc_dump_write("xattr_miss_count", "%" PRId64,
                       GF_ATOMIC_GET(conf->mdc_counter.xattr_miss));
    gf_proc_dump_write("nameless_lookup_count", "%" PRId64,
                       GF_ATOMIC_GET(conf->mdc_counter.nameless_lookup));
    gf_proc_dump_write("negative_lookup_count", "%" PRId64,
                       GF_ATOMIC_GET(conf->mdc_counter.negative_lookup));
    gf_proc_dump_write("stat_invalidations_received", "%" PRId64,
                       GF_ATOMIC_GET(conf->mdc_counter.stat_invals));
    gf_proc_dump_write("xattr_invalidations_received", "%" PRId64,
                       GF_ATOMIC_GET(conf->mdc_counter.xattr_invals));

    return 0;
}

static int32_t
mdc_dump_metrics(xlator_t *this, int fd)
{
    struct mdc_conf *conf = NULL;

    conf = this->private;
    if (!conf)
        goto out;

    dprintf(fd, "%s.stat_cache_hit_count %" PRId64 "\n", this->name,
            GF_ATOMIC_GET(conf->mdc_counter.stat_hit));
    dprintf(fd, "%s.stat_cache_miss_count %" PRId64 "\n", this->name,
            GF_ATOMIC_GET(conf->mdc_counter.stat_miss));
    dprintf(fd, "%s.xattr_cache_hit_count %" PRId64 "\n", this->name,
            GF_ATOMIC_GET(conf->mdc_counter.xattr_hit));
    dprintf(fd, "%s.xattr_cache_miss_count %" PRId64 "\n", this->name,
            GF_ATOMIC_GET(conf->mdc_counter.xattr_miss));
    dprintf(fd, "%s.nameless_lookup_count %" PRId64 "\n", this->name,
            GF_ATOMIC_GET(conf->mdc_counter.nameless_lookup));
    dprintf(fd, "%s.negative_lookup_count %" PRId64 "\n", this->name,
            GF_ATOMIC_GET(conf->mdc_counter.negative_lookup));
    dprintf(fd, "%s.stat_cache_invalidations_received %" PRId64 "\n",
            this->name, GF_ATOMIC_GET(conf->mdc_counter.stat_invals));
    dprintf(fd, "%s.xattr_cache_invalidations_received %" PRId64 "\n",
            this->name, GF_ATOMIC_GET(conf->mdc_counter.xattr_invals));
out:
    return 0;
}

int
mdc_forget(xlator_t *this, inode_t *inode)
{
    mdc_inode_wipe(this, inode);

    return 0;
}

int
is_strpfx(const char *str1, const char *str2)
{
    /* is one of the string a prefix of the other? */
    int i;

    for (i = 0; str1[i] == str2[i]; i++) {
        if (!str1[i] || !str2[i])
            break;
    }

    return !(str1[i] && str2[i]);
}

static int
mdc_key_unload_all(struct mdc_conf *conf)
{
    conf->mdc_xattr_str = NULL;

    return 0;
}

int
mdc_xattr_list_populate(struct mdc_conf *conf, char *tmp_str)
{
    char *mdc_xattr_str = NULL;
    size_t max_size = 0;
    int ret = 0;

    max_size = SLEN(
                   "security.capability,security.selinux,security."
                   "ima," POSIX_ACL_ACCESS_XATTR "," POSIX_ACL_DEFAULT_XATTR
                   "," GF_POSIX_ACL_ACCESS "," GF_POSIX_ACL_DEFAULT
                   ","
                   "user.swift.metadata,user.DOSATTRIB,user.DosStream.*"
                   ",user.org.netatalk.Metadata,security.NTACL,"
                   "user.org.netatalk.ResourceFork") +
               strlen(tmp_str) + 5; /*Some buffer bytes*/

    mdc_xattr_str = GF_MALLOC(max_size, gf_common_mt_char);
    GF_CHECK_ALLOC(mdc_xattr_str, ret, out);
    mdc_xattr_str[0] = '\0';

    if (conf->cache_capability)
        strcat(mdc_xattr_str, "security.capability,");

    if (conf->cache_selinux)
        strcat(mdc_xattr_str, "security.selinux,");

    if (conf->cache_ima)
        strcat(mdc_xattr_str, "security.ima,");

    if (conf->cache_posix_acl)
        strcat(mdc_xattr_str,
               POSIX_ACL_ACCESS_XATTR "," POSIX_ACL_DEFAULT_XATTR ",");

    if (conf->cache_glusterfs_acl)
        strcat(mdc_xattr_str, GF_POSIX_ACL_ACCESS "," GF_POSIX_ACL_DEFAULT ",");

    if (conf->cache_swift_metadata)
        strcat(mdc_xattr_str, "user.swift.metadata,");

    if (conf->cache_samba_metadata)
        strcat(mdc_xattr_str,
               "user.DOSATTRIB,user.DosStream.*,"
               "user.org.netatalk.Metadata,user.org.netatalk."
               "ResourceFork,security.NTACL,");

    strcat(mdc_xattr_str, tmp_str);

    LOCK(&conf->lock);
    {
        /* This is not freed, else is_mdc_key_satisfied, which is
         * called by every fop has to take lock, and will lead to
         * lock contention
         */     // 这个不释放，否则is_mdc_key_satisfied，每个fop调用的都得取锁，会导致锁争用
        conf->mdc_xattr_str = mdc_xattr_str;
    }
    UNLOCK(&conf->lock);

out:
    return ret;
}

struct set {
    inode_t *inode;
    xlator_t *this;
};

static int
mdc_inval_xatt(dict_t *d, char *k, data_t *v, void *tmp)
{
    struct set *tmp1 = NULL;
    int ret = 0;

    tmp1 = (struct set *)tmp;
    ret = mdc_inode_xatt_unset(tmp1->this, tmp1->inode, k);
    return ret;
}

static int
mdc_invalidate(xlator_t *this, void *data)
{
    struct gf_upcall *up_data = NULL;           // 上调的数据结构
    struct gf_upcall_cache_invalidation *up_ci = NULL;  // 上调缓存失效与否
    inode_t *inode = NULL;
    int ret = 0;
    struct set tmp = {
        0,
    };
    inode_table_t *itable = NULL;
    struct mdc_conf *conf = this->private;
    uint64_t gen = 0;

    up_data = (struct gf_upcall *)data;             // data为upcall类型的数据

    if (up_data->event_type != GF_UPCALL_CACHE_INVALIDATION)        // 如果不是upcall_cache_invalidation类型的event，out
        goto out;

    up_ci = (struct gf_upcall_cache_invalidation *)up_data->data;   // upcall中的data对应upcall_cache_invalidation

    itable = ((xlator_t *)this->graph->top)->itable;        // 获取inode table
    inode = inode_find(itable, up_data->gfid);              // 查找上调文件的gfid，在inode表格中
    if (!inode) {
        ret = -1;
        goto out;
    }

    if (up_ci->flags & UP_PARENT_DENTRY_FLAGS) {            // 父目录信息失效(对该gfid的操作 rename, unlink, rmdir,mkdir, create)
        mdc_update_gfid_stat(this, &up_ci->p_stat);
        if (up_ci->flags & UP_RENAME_FLAGS)                 // 这里的rename包含移动文件？？
            mdc_update_gfid_stat(this, &up_ci->oldp_stat);
    }

    if (up_ci->flags & UP_EXPLICIT_LOOKUP) {                // 明确需要lookup
        mdc_inode_set_need_lookup(this, inode, _gf_true);   // 从inode的ctx中取出mdc， 将其need_lookup置为true
        goto out;
    }

    if (up_ci->flags &
        (UP_NLINK | UP_RENAME_FLAGS | UP_FORGET | UP_INVAL_ATTR)) {         // 对于nlink,rename,forget,inval_attr等标识位，使其iatt和xatt失效
        mdc_inode_iatt_invalidate(this, inode);
        mdc_inode_xatt_invalidate(this, inode);
        GF_ATOMIC_INC(conf->mdc_counter.stat_invals);       // 失效计数
        goto out;
    }

    if (up_ci->flags & IATT_UPDATE_FLAGS) {             // iatt更新flag
        gen = mdc_inc_generation(this, inode);      // 增加版本计数
        ret = mdc_inode_iatt_set_validate(this, inode, NULL, &up_ci->stat,
                                          _gf_false, gen);
        /* one of the scenarios where ret < 0 is when this invalidate
         * is older than the current stat, in that case do not
         * update the xattrs as well
         */
        /*ret < 0 的场景之一是当此无效比当前状态更旧时，在这种情况下也不要更新 xattrs*/
        if (ret < 0)
            goto out;
        GF_ATOMIC_INC(conf->mdc_counter.stat_invals);
    }

    if (up_ci->flags & UP_XATTR) {
        if (up_ci->dict)
            ret = mdc_inode_xatt_update(this, inode, up_ci->dict);
        else
            ret = mdc_inode_xatt_invalidate(this, inode);

        GF_ATOMIC_INC(conf->mdc_counter.xattr_invals);
    } else if (up_ci->flags & UP_XATTR_RM) {
        tmp.inode = inode;
        tmp.this = this;
        ret = dict_foreach(up_ci->dict, mdc_inval_xatt, &tmp);

        GF_ATOMIC_INC(conf->mdc_counter.xattr_invals);
    }

out:
    if (inode)
        inode_unref(inode);

    return ret;
}

struct mdc_ipc {
    xlator_t *this;
    dict_t *xattr;
};

static int
mdc_send_xattrs_cbk(int ret, call_frame_t *frame, void *data)
{
    struct mdc_ipc *tmp = data;

    if (ret < 0) {
        mdc_key_unload_all(THIS->private);
        gf_msg("md-cache", GF_LOG_INFO, 0, MD_CACHE_MSG_NO_XATTR_CACHE,
               "Disabled cache for all xattrs, as registering for "
               "xattr cache invalidation failed");
    }
    STACK_DESTROY(frame->root);
    dict_unref(tmp->xattr);
    GF_FREE(tmp);

    return 0;
}

static int
mdc_send_xattrs(void *data)
{
    int ret = 0;
    struct mdc_ipc *tmp = data;

    ret = syncop_ipc(FIRST_CHILD(tmp->this), GF_IPC_TARGET_UPCALL, tmp->xattr,
                     NULL);
    DECODE_SYNCOP_ERR(ret);
    if (ret < 0) {
        gf_msg(tmp->this->name, GF_LOG_WARNING, errno,
               MD_CACHE_MSG_IPC_UPCALL_FAILED,
               "Registering the list "
               "of xattrs that needs invalidaton, with upcall, failed");
    }

    return ret;
}
// 注册的xattr 失效
static int
mdc_register_xattr_inval(xlator_t *this)
{
    dict_t *xattr = NULL;
    int ret = 0;
    struct mdc_conf *conf = NULL;
    call_frame_t *frame = NULL;
    struct mdc_ipc *data = NULL;

    conf = this->private;

    LOCK(&conf->lock);
    {
        if (!conf->mdc_invalidation) {          // off 收到换出失效通知不做处理
            UNLOCK(&conf->lock);
            goto out;
        }
    }
    UNLOCK(&conf->lock);

    xattr = dict_new();
    if (!xattr) {
        gf_msg(this->name, GF_LOG_WARNING, ENOMEM, MD_CACHE_MSG_NO_MEMORY,
               "dict_new failed");
        ret = -1;
        goto out;
    }
    // cache list 加载到字典中，默认值为0
    mdc_load_reqs(this, xattr);

    frame = create_frame(this, this->ctx->pool);
    if (!frame) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, MD_CACHE_MSG_NO_MEMORY,
               "failed to create the frame");
        ret = -1;
        goto out;
    }

    data = GF_CALLOC(1, sizeof(struct mdc_ipc), gf_mdc_mt_mdc_ipc);
    if (!data) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, MD_CACHE_MSG_NO_MEMORY,
               "failed to allocate memory");
        ret = -1;
        goto out;
    }

    data->this = this;
    data->xattr = xattr;
    ret = synctask_new(this->ctx->env, mdc_send_xattrs, mdc_send_xattrs_cbk,
                       frame, data);                // 新建协程任务，
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_WARNING, errno,
               MD_CACHE_MSG_IPC_UPCALL_FAILED,
               "Registering the list "
               "of xattrs that needs invalidaton, with upcall, failed");    /*注册需要 invalidaton 的 xattrs 列表，使用 upcall，失败*/
    }

out:
    if (ret < 0) {
        mdc_key_unload_all(conf);               // 清空cache list
        if (xattr)                              // 释放资源
            dict_unref(xattr);
        if (frame)
            STACK_DESTROY(frame->root);
        GF_FREE(data);
        gf_msg(this->name, GF_LOG_INFO, 0, MD_CACHE_MSG_NO_XATTR_CACHE,
               "Disabled cache for all xattrs, as registering for "
               "xattr cache invalidation failed");
    }

    return ret;
}

int
mdc_reconfigure(xlator_t *this, dict_t *options)
{
    struct mdc_conf *conf = NULL;
    int timeout = 0;
    char *tmp_str = NULL;

    conf = this->private;

    GF_OPTION_RECONF("md-cache-timeout", timeout, options, int32, out);

    GF_OPTION_RECONF("cache-selinux", conf->cache_selinux, options, bool, out);

    GF_OPTION_RECONF("cache-capability-xattrs", conf->cache_capability, options,
                     bool, out);

    GF_OPTION_RECONF("cache-ima-xattrs", conf->cache_ima, options, bool, out);

    GF_OPTION_RECONF("cache-posix-acl", conf->cache_posix_acl, options, bool,
                     out);

    GF_OPTION_RECONF("cache-glusterfs-acl", conf->cache_glusterfs_acl, options,
                     bool, out);

    GF_OPTION_RECONF("cache-swift-metadata", conf->cache_swift_metadata,
                     options, bool, out);

    GF_OPTION_RECONF("cache-samba-metadata", conf->cache_samba_metadata,
                     options, bool, out);

    GF_OPTION_RECONF("force-readdirp", conf->force_readdirp, options, bool,
                     out);

    GF_OPTION_RECONF("cache-invalidation", conf->mdc_invalidation, options,
                     bool, out);

    GF_OPTION_RECONF("global-cache-invalidation", conf->global_invalidation,
                     options, bool, out);

    GF_OPTION_RECONF("pass-through", this->pass_through, options, bool, out);

    GF_OPTION_RECONF("md-cache-statfs", conf->cache_statfs, options, bool, out);

    GF_OPTION_RECONF("xattr-cache-list", tmp_str, options, str, out);
    mdc_xattr_list_populate(conf, tmp_str);

    /* If timeout is greater than 60s (default before the patch that added
     * cache invalidation support was added) then, cache invalidation
     * feature for md-cache needs to be enabled, if not set timeout to the
     * previous max which is 60s
     */
    if ((timeout > 60) && (!conf->mdc_invalidation)) {
        conf->timeout = 60;
        goto out;
    }
    conf->timeout = timeout;

    (void)mdc_register_xattr_inval(this);
out:
    return 0;
}

int32_t
mdc_mem_acct_init(xlator_t *this)
{
    int ret = -1;

    ret = xlator_mem_acct_init(this, gf_mdc_mt_end + 1);
    return ret;
}

int
mdc_init(xlator_t *this)
{
    struct mdc_conf *conf = NULL;
    int timeout = 0;
    char *tmp_str = NULL;

    conf = GF_CALLOC(sizeof(*conf), 1, gf_mdc_mt_mdc_conf_t);
    if (!conf) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, MD_CACHE_MSG_NO_MEMORY,
               "out of memory");
        return -1;
    }

    LOCK_INIT(&conf->lock);

    GF_OPTION_INIT("md-cache-timeout", timeout, int32, out);

    GF_OPTION_INIT("cache-selinux", conf->cache_selinux, bool, out);

    GF_OPTION_INIT("cache-capability-xattrs", conf->cache_capability, bool,
                   out);

    GF_OPTION_INIT("cache-ima-xattrs", conf->cache_ima, bool, out);

    GF_OPTION_INIT("cache-posix-acl", conf->cache_posix_acl, bool, out);

    GF_OPTION_INIT("cache-glusterfs-acl", conf->cache_glusterfs_acl, bool, out);

    GF_OPTION_INIT("cache-swift-metadata", conf->cache_swift_metadata, bool,
                   out);

    GF_OPTION_INIT("cache-samba-metadata", conf->cache_samba_metadata, bool,
                   out);

    GF_OPTION_INIT("force-readdirp", conf->force_readdirp, bool, out);

    GF_OPTION_INIT("cache-invalidation", conf->mdc_invalidation, bool, out);

    GF_OPTION_INIT("global-cache-invalidation", conf->global_invalidation, bool,
                   out);

    GF_OPTION_INIT("pass-through", this->pass_through, bool, out);

    pthread_mutex_init(&conf->statfs_cache.lock, NULL);
    GF_OPTION_INIT("md-cache-statfs", conf->cache_statfs, bool, out);

    GF_OPTION_INIT("xattr-cache-list", tmp_str, str, out);
    mdc_xattr_list_populate(conf, tmp_str);             // 把缓存的元数据放在conf->mdc_xattr_str中，，隔开

    time(&conf->last_child_down);                       // 获取自1970-01-01至今天的秒数
    /* initialize gf_atomic_t counters */
    GF_ATOMIC_INIT(conf->mdc_counter.stat_hit, 0);
    GF_ATOMIC_INIT(conf->mdc_counter.stat_miss, 0);
    GF_ATOMIC_INIT(conf->mdc_counter.xattr_hit, 0);
    GF_ATOMIC_INIT(conf->mdc_counter.xattr_miss, 0);
    GF_ATOMIC_INIT(conf->mdc_counter.negative_lookup, 0);
    GF_ATOMIC_INIT(conf->mdc_counter.nameless_lookup, 0);
    GF_ATOMIC_INIT(conf->mdc_counter.stat_invals, 0);
    GF_ATOMIC_INIT(conf->mdc_counter.xattr_invals, 0);
    GF_ATOMIC_INIT(conf->mdc_counter.need_lookup, 0);
    GF_ATOMIC_INIT(conf->generation, 0);

    /* If timeout is greater than 60s (default before the patch that added
     * cache invalidation support was added) then, cache invalidation
     * feature for md-cache needs to be enabled, if not set timeout to the
     * previous max which is 60s
     */
    /* 如果超时大于 60 秒（默认在添加缓存失效支持的补丁之前），
     * 则需要启用 md-cache 的缓存失效功能，如果没有将超时设置为之前的最大值，
     * 即 60 秒*/
    if ((timeout > 60) && (!conf->mdc_invalidation)) {      // mdc_invalidation on 表示接受缓存失效的通知。不接受通知改为默认的60s超时
        conf->timeout = 60;
        goto out;
    }
    conf->timeout = timeout;

out:
    this->private = conf;

    return 0;
}

void
mdc_update_child_down_time(xlator_t *this, time_t *now)
{
    struct mdc_conf *conf = NULL;

    conf = this->private;

    LOCK(&conf->lock);
    {
        conf->last_child_down = *now;           // 更新child down 时间戳
    }
    UNLOCK(&conf->lock);
}

int
mdc_notify(xlator_t *this, int event, void *data, ...)
{
    int ret = 0;
    struct mdc_conf *conf = NULL;
    time_t now = 0;

    conf = this->private;
    switch (event) {
        case GF_EVENT_CHILD_DOWN:
        case GF_EVENT_SOME_DESCENDENT_DOWN:    // DESCENDENT 后裔
            time(&now);
            mdc_update_child_down_time(this, &now);
            break;
        case GF_EVENT_UPCALL:
            if (conf->mdc_invalidation)        // 针对向上调用， 失效缓存
                ret = mdc_invalidate(this, data);
            break;
        case GF_EVENT_CHILD_UP:
        case GF_EVENT_SOME_DESCENDENT_UP:
            ret = mdc_register_xattr_inval(this);       // 注册xattr无效？
            break;
        default:
            break;
    }

    if (default_notify(this, event, data) != 0)
        ret = -1;

    return ret;
}

void
mdc_fini(xlator_t *this)
{
    GF_FREE(this->private);
}

struct xlator_fops mdc_fops = {
    .lookup = mdc_lookup,
    .stat = mdc_stat,
    .fstat = mdc_fstat,
    .truncate = mdc_truncate,
    .ftruncate = mdc_ftruncate,
    .mknod = mdc_mknod,
    .mkdir = mdc_mkdir,
    .unlink = mdc_unlink,
    .rmdir = mdc_rmdir,
    .symlink = mdc_symlink,
    .rename = mdc_rename,
    .link = mdc_link,
    .create = mdc_create,
    .open = mdc_open,
    .readv = mdc_readv,
    .writev = mdc_writev,
    .setattr = mdc_setattr,
    .fsetattr = mdc_fsetattr,
    .fsync = mdc_fsync,
    .setxattr = mdc_setxattr,
    .fsetxattr = mdc_fsetxattr,
    .getxattr = mdc_getxattr,
    .fgetxattr = mdc_fgetxattr,
    .removexattr = mdc_removexattr,
    .fremovexattr = mdc_fremovexattr,
    .opendir = mdc_opendir,
    .readdirp = mdc_readdirp,
    .readdir = mdc_readdir,
    .fallocate = mdc_fallocate,
    .discard = mdc_discard,
    .zerofill = mdc_zerofill,
    .statfs = mdc_statfs,
    .readlink = mdc_readlink,
    .fsyncdir = mdc_fsyncdir,
    .access = mdc_access,
};

struct xlator_cbks mdc_cbks = {
    .forget = mdc_forget,
};

struct xlator_dumpops mdc_dumpops = {
    .priv = mdc_priv_dump,
};

struct volume_options mdc_options[] = {
    {
        .key = {"md-cache"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "off",
        .description = "enable/disable md-cache",           // enable/disable md-cache
        .op_version = {GD_OP_VERSION_6_0},
        .flags = OPT_FLAG_SETTABLE,
    },
    {
        .key = {"cache-selinux"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "false",
        .op_version = {2},
        .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
        .description = "Cache selinux xattr(security.selinux) on client side",      // 缓存security.selinux
    },
    {
        .key = {"cache-capability-xattrs"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "true",
        .op_version = {GD_OP_VERSION_3_10_0},
        .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
        .description = "Cache capability xattr(security.capability) on "
                       "client side",           // 客户端缓存xattr能力 (security.capability)
    },
    {
        .key = {"cache-ima-xattrs"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "true",
        .op_version = {GD_OP_VERSION_3_10_0},
        .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
        .description = "Cache Linux integrity subsystem xattr(security.ima) "       // 缓存 Linux 完整性子系统 xattr
                       "on client side",
    },
    {
        .key = {"cache-swift-metadata"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "true",
        .op_version = {GD_OP_VERSION_3_7_10},
        .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
        .description = "Cache swift metadata (user.swift.metadata xattr)",
    },
    {
        .key = {"cache-samba-metadata"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "false",
        .op_version = {GD_OP_VERSION_3_9_0},
        .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
        .description = "Cache samba metadata (user.DOSATTRIB, security.NTACL,"
                       " org.netatalk.Metadata, org.netatalk.ResourceFork, "
                       "and user.DosStream. xattrs)",
    },
    {
        .key = {"cache-posix-acl"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "false",
        .op_version = {2},
        .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
        .description = "Cache posix ACL xattrs (system.posix_acl_access, "
                       "system.posix_acl_default) on client side",
                       /*在客户端缓存 posix ACL xattrs (system.posix_acl_access, system.posix_acl_default)*/
    },
    {
        .key = {"cache-glusterfs-acl"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "false",
        .op_version = {GD_OP_VERSION_6_0},
        .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
        .description = "Cache virtual glusterfs ACL xattrs "
                       "(glusterfs.posix.acl, glusterfs.posix.default_acl) "
                       "on client side",
                       /*在客户端缓存虚拟 glusterfs ACL xattrs (glusterfs.posix.acl, glusterfs.posix.default_acl)*/
    },
    {
        .key = {"md-cache-timeout"},
        .type = GF_OPTION_TYPE_INT,
        .min = 0,
        .max = 600,
        .default_value = SITE_H_MD_CACHE_TIMEOUT,
        .op_version = {2},
        .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
        .description = "Time period after which cache has to be refreshed",     // 必须刷新缓存的时间段
    },
    {
        .key = {"force-readdirp"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "true",
        .op_version = {2},
        .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
        .description = "Convert all readdir requests to readdirplus to "
                       "collect stat info on each entry.",      // 将所有 readdir 请求转换为 readdirplus 以收集每个条目的统计信息
    },
    {
        .key = {"cache-invalidation"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "false",
        .op_version = {GD_OP_VERSION_3_9_0},
        .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
        .description = "When \"on\", invalidates/updates the metadata cache,"
                       " on receiving the cache-invalidation notifications",    // when on 收到缓存失效的通知时，失效/更新元数据缓存
    },
    {
        .key = {"global-cache-invalidation"},           // 多客户端一致性 需求 ，on
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "true",
        .op_version = {GD_OP_VERSION_6_0},
        .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
        .description =
            "When \"on\", purges all read caches in kernel and glusterfs stack "
            "whenever a stat change is detected. Stat changes can be detected "
            "while processing responses to file operations (fop) or through "
            "upcall notifications. Since purging caches can be an expensive "
            "operation, it's advised to have this option \"on\" only when a "
            "file "
            "can be accessed from multiple different Glusterfs mounts and "
            "caches across these different mounts are required to be coherent. "
            "If a file is not accessed across different mounts "
            "(simple example is having only one mount for a volume), its "
            "advised to keep "
            "this option \"off\" as all file modifications go through caches "
            "keeping them "
            "coherent. This option overrides value of "
            "performance.cache-invalidation.",
            /*当“on”时，每当检测到统计更改时，清除内核和 glusterfs 堆栈中的所有读取缓存。在处理对文件操作 (fop) 的响应或通过调用通知时，
            可以检测到状态更改。由于清除缓存可能是一项昂贵的操作，因此建议仅当可以从多个不同的 Glusterfs 
            挂载访问文件并且跨这些不同挂载的缓存需要保持一致时才使用此选项“on”。如果一个文件没有通过不同的挂载访问
            （简单的例子是一个卷只有一个挂载），建议保持这个选项“关闭”，
            因为所有文件修改都通过缓存来保持它们的一致性。此选项会覆盖 performance.cache-invalidation 的值。*/
    },
    {
        .key = {"md-cache-statfs"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "off",
        .op_version = {GD_OP_VERSION_4_0_0},
        .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
        .description = "Cache statfs information of filesystem on the client",  // 缓存客户端文件系统的statfs信息
    },
    {
        .key = {"xattr-cache-list"},            // 自定义要缓存的元数据，隔开，只允许*通配符
        .type = GF_OPTION_TYPE_STR,
        .default_value = "",
        .op_version = {GD_OP_VERSION_4_0_0},
        .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
        .description = "A comma separated list of xattrs that shall be "
                       "cached by md-cache. The only wildcard allowed is '*'",  /*应由 md-cache 缓存的逗号分隔的 xattrs 列表。唯一允许的通配符是 '*' */
    },
    {.key = {"pass-through"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "false",
     .op_version = {GD_OP_VERSION_4_1_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC | OPT_FLAG_CLIENT_OPT,
     .tags = {"md-cache"},
     .description = "Enable/Disable md cache translator"},      // 启用/禁用 md 缓存xlator
    {.key = {NULL}},
};

xlator_api_t xlator_api = {
    .init = mdc_init,
    .fini = mdc_fini,
    .notify = mdc_notify,
    .reconfigure = mdc_reconfigure,
    .mem_acct_init = mdc_mem_acct_init,
    .dump_metrics = mdc_dump_metrics,
    .op_version = {1}, /* Present from the initial version */
    .dumpops = &mdc_dumpops,
    .fops = &mdc_fops,
    .cbks = &mdc_cbks,
    .options = mdc_options,
    .identifier = "md-cache",
    .category = GF_MAINTAINED,
};
