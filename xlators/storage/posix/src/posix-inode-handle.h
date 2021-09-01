/*
   Copyright (c) 2011-2017 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _POSIX_INODE_HANDLE_H
#define _POSIX_INODE_HANDLE_H

#include <limits.h>
#include <sys/types.h>
#include <glusterfs/gf-dirent.h>
#include "posix.h"

/* From Open Group Base Specifications Issue 6 */
#ifndef _XOPEN_PATH_MAX
#define _XOPEN_PATH_MAX 1024
#endif

#define TRASH_DIR "landfill"

#define UUID0_STR "00000000-0000-0000-0000-000000000000"
#define SLEN(str) (sizeof(str) - 1)

#define LOC_HAS_ABSPATH(loc) (loc && (loc->path) && (loc->path[0] == '/'))
#define LOC_IS_DIR(loc)                                                        \
    (loc && (loc->inode) && (loc->inode->ia_type == IA_IFDIR))
// var_len = this->private->base_path_length +strlen(path)
// var的长度等于工作目录长度 + 相对目录长度
// 如果长度超过了限制值，以path去掉前缀的'/'(相对路径名)赋值给var
// 否则var为绝对路径
#define MAKE_REAL_PATH(var, this, path)                                        \
    do {                                                                       \
        size_t path_len = strlen(path);                                        \
        size_t var_len = path_len + POSIX_BASE_PATH_LEN(this) + 1;             \
        if (POSIX_PATH_MAX(this) != -1 && var_len >= POSIX_PATH_MAX(this)) {   \
            var = alloca(path_len + 1);                                        \
            strcpy(var, (path[0] == '/') ? path + 1 : path);                   \
        } else {                                                               \
            var = alloca(var_len);                                             \
            strcpy(var, POSIX_BASE_PATH(this));                                \
            strcpy(&var[POSIX_BASE_PATH_LEN(this)], path);                     \
        }                                                                      \
    } while (0)
// posix_handle_path 第一次获取路径长度，第二次赋值
#define MAKE_HANDLE_PATH(var, this, gfid, base)                                \
    do {                                                                       \
        int __len;                                                             \
        __len = posix_handle_path(this, gfid, base, NULL, 0);                  \
        if (__len <= 0)                                                        \
            break;                                                             \
        var = alloca(__len);                                                   \
        __len = posix_handle_path(this, gfid, base, var, __len);               \
        if (__len <= 0)                                                        \
            var = NULL;                                                        \
    } while (0)

/* TODO: it is not a good idea to change a variable which
   is not passed to the macro.. Fix it later */
/* 如果loc是目录并且是绝对路径，  rpath被赋值为posix基本path + loc的路径， posix_pstat(根据path stat)， break
   否则， posix_istat(根据inode stat) 参数不带 ipath
   errno != ELOOP(链接数过多)， 通过posix_handle_path 获取路径名
    */
#define MAKE_INODE_HANDLE(rpath, this, loc, iatt_p)                            \
    do {                                                                       \
        if (!this->private) {                                                  \
            op_ret = -1;                                                       \
            gf_msg("make_inode_handle", GF_LOG_ERROR, 0,                       \
                   P_MSG_INODE_HANDLE_CREATE,                                  \
                   "private is NULL, fini is already called");                 \
            break;                                                             \
        }                                                                      \
        if (gf_uuid_is_null(loc->gfid)) {                                      \
            op_ret = -1;                                                       \
            gf_msg(this->name, GF_LOG_ERROR, 0, P_MSG_INODE_HANDLE_CREATE,     \
                   "null gfid for path %s", (loc)->path);                      \
            break;                                                             \
        }                                                                      \
        if (LOC_IS_DIR(loc) && LOC_HAS_ABSPATH(loc)) {                         \
            MAKE_REAL_PATH(rpath, this, (loc)->path);                          \
            op_ret = posix_pstat(this, (loc)->inode, (loc)->gfid, rpath,       \
                                 iatt_p, _gf_false);                           \
            break;                                                             \
        }                                                                      \
        errno = 0;                                                             \
        op_ret = posix_istat(this, loc->inode, loc->gfid, NULL, iatt_p);       \
        if (errno != ELOOP) {                                                  \
            MAKE_HANDLE_PATH(rpath, this, (loc)->gfid, NULL);                  \
            if (!rpath) {                                                      \
                op_ret = -1;                                                   \
                gf_msg(this->name, GF_LOG_ERROR, errno,                        \
                       P_MSG_INODE_HANDLE_CREATE,                              \
                       "Failed to create inode handle "                        \
                       "for path %s",                                          \
                       (loc)->path);                                           \
            }                                                                  \
            break;                                                             \
        } /* __ret == -1 && errno == ELOOP */                                  \
        else {                                                                 \
            op_ret = -1;                                                       \
        }                                                                      \
    } while (0)

#define POSIX_ANCESTRY_PATH (1 << 0)
#define POSIX_ANCESTRY_DENTRY (1 << 1)

int
posix_handle_path(xlator_t *this, uuid_t gfid, const char *basename, char *buf,
                  size_t len);

int
posix_make_ancestryfromgfid(xlator_t *this, char *path, int pathsize,
                            gf_dirent_t *head, int type, uuid_t gfid,
                            const size_t handle_size,
                            const char *priv_base_path, inode_table_t *table,
                            inode_t **parent, dict_t *xdata, int32_t *op_errno);

int
posix_handle_init(xlator_t *this);

int
posix_handle_trash_init(xlator_t *this);

#endif /* !_POSIX_INODE_HANDLE_H */
