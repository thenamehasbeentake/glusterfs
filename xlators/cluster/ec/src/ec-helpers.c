/*
  Copyright (c) 2012-2014 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <libgen.h>

#include "byte-order.h"

#include "ec.h"
#include "ec-mem-types.h"
#include "ec-messages.h"
#include "ec-fops.h"
#include "ec-method.h"
#include "ec-helpers.h"

static const char * ec_fop_list[] =
{
    [-EC_FOP_HEAL] = "HEAL"
};

const char * ec_bin(char * str, size_t size, uint64_t value, int32_t digits)
{
    str += size;

    if (size-- < 1)
    {
        goto failed;
    }
    *--str = 0;

    while ((value != 0) || (digits > 0))
    {
        if (size-- < 1)
        {
            goto failed;
        }
        *--str = '0' + (value & 1);
        digits--;
        value >>= 1;
    }

    return str;

failed:
    return "<buffer too small>";
}

const char * ec_fop_name(int32_t id)
{
    if (id >= 0)
    {
        return gf_fop_list[id];
    }

    return ec_fop_list[-id];
}

void ec_trace(const char * event, ec_fop_data_t * fop, const char * fmt, ...)
{
    char str1[32], str2[32], str3[32];
    char * msg;
    ec_t * ec = fop->xl->private;
    va_list args;
    int32_t ret;

    va_start(args, fmt);
    ret = vasprintf(&msg, fmt, args);
    va_end(args);

    if (ret < 0)
    {
        msg = "<memory allocation error>";
    }

    gf_msg_trace ("ec", 0, "%s(%s) %p(%p) [refs=%d, winds=%d, jobs=%d] "
                               "frame=%p/%p, min/exp=%d/%d, err=%d state=%d "
                               "{%s:%s:%s} %s",
           event, ec_fop_name(fop->id), fop, fop->parent, fop->refs,
           fop->winds, fop->jobs, fop->req_frame, fop->frame, fop->minimum,
           fop->expected, fop->error, fop->state,
           ec_bin(str1, sizeof(str1), fop->mask, ec->nodes),
           ec_bin(str2, sizeof(str2), fop->remaining, ec->nodes),
           ec_bin(str3, sizeof(str3), fop->good, ec->nodes), msg);

    if (ret >= 0)
    {
        free(msg);
    }
}

int32_t ec_bits_consume(uint64_t * n)
{
    uint64_t tmp;

    tmp = *n;
    tmp &= -tmp;
    *n ^= tmp;

    return gf_bits_index(tmp);
}

size_t ec_iov_copy_to(void * dst, struct iovec * vector, int32_t count,
                      off_t offset, size_t size)
{
    int32_t i = 0;
    size_t total = 0, len = 0;

    while (i < count)
    {
        if (offset < vector[i].iov_len)
        {
            while ((i < count) && (size > 0))
            {
                len = size;
                if (len > vector[i].iov_len - offset)
                {
                    len = vector[i].iov_len - offset;
                }
                memcpy(dst, vector[i++].iov_base + offset, len);
                offset = 0;
                dst += len;
                total += len;
                size -= len;
            }

            break;
        }

        offset -= vector[i].iov_len;
        i++;
    }

    return total;
}

int32_t ec_buffer_alloc(xlator_t *xl, size_t size, struct iobref **piobref,
                        void **ptr)
{
    struct iobref *iobref = NULL;
    struct iobuf *iobuf = NULL;
    int32_t ret = -ENOMEM;

    iobuf = iobuf_get_page_aligned (xl->ctx->iobuf_pool, size,
                                    EC_METHOD_WORD_SIZE);
    if (iobuf == NULL) {
        goto out;
    }

    iobref = *piobref;
    if (iobref == NULL) {
        iobref = iobref_new();
        if (iobref == NULL) {
            goto out;
        }
    }

    ret = iobref_add(iobref, iobuf);
    if (ret != 0) {
        if (iobref != *piobref) {
            iobref_unref(iobref);
        }
        iobref = NULL;

        goto out;
    }

    GF_ASSERT(EC_ALIGN_CHECK(iobuf->ptr, EC_METHOD_WORD_SIZE));

    *ptr = iobuf->ptr;

out:
    if (iobuf != NULL) {
        iobuf_unref(iobuf);
    }

    if (iobref != NULL) {
        *piobref = iobref;
    }

    return ret;
}

int32_t ec_dict_set_array(dict_t *dict, char *key, uint64_t value[],
                          int32_t size)
{
    int         ret = -1;
    uint64_t   *ptr = NULL;
    int32_t     vindex;

    if (value == NULL) {
        return -EINVAL;
    }

    ptr = GF_MALLOC(sizeof(uint64_t) * size, gf_common_mt_char);
    if (ptr == NULL) {
        return -ENOMEM;
    }
    for (vindex = 0; vindex < size; vindex++) {
         ptr[vindex] = hton64(value[vindex]);
    }
    ret = dict_set_bin(dict, key, ptr, sizeof(uint64_t) * size);
    if (ret)
         GF_FREE (ptr);
    return ret;
}


int32_t
ec_dict_get_array (dict_t *dict, char *key, uint64_t value[], int32_t size)
{
    void    *ptr;
    int32_t len;
    int32_t vindex;
    int32_t old_size = 0;
    int32_t err;

    if (dict == NULL) {
        return -EINVAL;
    }
    err = dict_get_ptr_and_len(dict, key, &ptr, &len);
    if (err != 0) {
        return err;
    }

    if (len > (size * sizeof(uint64_t)) || (len % sizeof (uint64_t))) {
        return -EINVAL;
    }

    memset (value, 0, size * sizeof(uint64_t));
    /* 3.6 version ec would have stored version in 64 bit. In that case treat
     * metadata versions same as data*/
    old_size = min (size, len/sizeof(uint64_t));
    for (vindex = 0; vindex < old_size; vindex++) {
         value[vindex] = ntoh64(*((uint64_t *)ptr + vindex));
    }

    if (old_size < size) {
            for (vindex = old_size; vindex < size; vindex++) {
                 value[vindex] = value[old_size-1];
            }
    }

    return 0;
}

int32_t
ec_dict_del_array (dict_t *dict, char *key, uint64_t value[], int32_t size)
{
    int ret = 0;

    ret = ec_dict_get_array (dict, key, value, size);
    if (ret == 0)
            dict_del(dict, key);

    return ret;
}


int32_t ec_dict_set_number(dict_t * dict, char * key, uint64_t value)
{
    int        ret = -1;
    uint64_t * ptr;

    ptr = GF_MALLOC(sizeof(value), gf_common_mt_char);
    if (ptr == NULL) {
        return -ENOMEM;
    }

    *ptr = hton64(value);

    ret = dict_set_bin(dict, key, ptr, sizeof(value));
    if (ret)
        GF_FREE (ptr);

    return ret;
}

int32_t ec_dict_del_number(dict_t * dict, char * key, uint64_t * value)
{
    void * ptr;
    int32_t len, err;

    if (dict == NULL) {
        return -EINVAL;
    }
    err = dict_get_ptr_and_len(dict, key, &ptr, &len);
    if (err != 0) {
        return err;
    }
    if (len != sizeof(uint64_t)) {
        return -EINVAL;
    }

    *value = ntoh64(*(uint64_t *)ptr);

    dict_del(dict, key);

    return 0;
}

int32_t ec_dict_set_config(dict_t * dict, char * key, ec_config_t * config)
{
    int ret = -1;
    uint64_t * ptr, data;

    if (config->version > EC_CONFIG_VERSION)
    {
        gf_msg ("ec", GF_LOG_ERROR, EINVAL,
                EC_MSG_UNSUPPORTED_VERSION,
                "Trying to store an unsupported config "
                "version (%u)", config->version);

        return -EINVAL;
    }

    ptr = GF_MALLOC(sizeof(uint64_t), gf_common_mt_char);
    if (ptr == NULL)
    {
        return -ENOMEM;
    }

    data = ((uint64_t)config->version) << 56;
    data |= ((uint64_t)config->algorithm) << 48;
    data |= ((uint64_t)config->gf_word_size) << 40;
    data |= ((uint64_t)config->bricks) << 32;
    data |= ((uint64_t)config->redundancy) << 24;
    data |= config->chunk_size;

    *ptr = hton64(data);

    ret = dict_set_bin(dict, key, ptr, sizeof(uint64_t));
    if (ret)
        GF_FREE (ptr);

    return ret;
}

int32_t ec_dict_del_config(dict_t * dict, char * key, ec_config_t * config)
{
    void * ptr;
    uint64_t data;
    int32_t len, err;

    if (dict == NULL) {
        return -EINVAL;
    }
    err = dict_get_ptr_and_len(dict, key, &ptr, &len);
    if (err != 0) {
        return err;
    }
    if (len != sizeof(uint64_t)) {
        return -EINVAL;
    }

    data = ntoh64(*(uint64_t *)ptr);
    /* Currently we need to get the config xattr for entries of type IA_INVAL.
     * These entries can later become IA_DIR entries (after inode_link()),
     * which don't have a config xattr. However, since the xattr is requested
     * using an xattrop() fop, it will always return a config full of 0's
     * instead of saying that it doesn't exist.
     *
     * We need to filter out this case and consider that a config xattr == 0 is
     * the same as a non-existent xattr. Otherwise ec_config_check() will fail.
     */
    if (data == 0) {
        return -ENODATA;
    }

    config->version = (data >> 56) & 0xff;
    if (config->version > EC_CONFIG_VERSION)
    {
        gf_msg ("ec", GF_LOG_ERROR, EINVAL,
                EC_MSG_UNSUPPORTED_VERSION,
                "Found an unsupported config version (%u)",
                config->version);

        return -EINVAL;
    }

    config->algorithm = (data >> 48) & 0xff;
    config->gf_word_size = (data >> 40) & 0xff;
    config->bricks = (data >> 32) & 0xff;
    config->redundancy = (data >> 24) & 0xff;
    config->chunk_size = data & 0xffffff;

    dict_del(dict, key);

    return 0;
}
// dst与src的uuid都不为空且不等才返回false
gf_boolean_t ec_loc_gfid_check(xlator_t *xl, uuid_t dst, uuid_t src)
{
    if (gf_uuid_is_null(src)) {
        return _gf_true;
    }

    if (gf_uuid_is_null(dst)) {
        gf_uuid_copy(dst, src);

        return _gf_true;
    }

    if (gf_uuid_compare(dst, src) != 0) {
        gf_msg (xl->name, GF_LOG_WARNING, 0,
                EC_MSG_GFID_MISMATCH,
                "Mismatching GFID's in loc");

        return _gf_false;
    }

    return _gf_true;
}
// 检查loc中inode 或者 设置它
int32_t ec_loc_setup_inode(xlator_t *xl, inode_table_t *table, loc_t *loc)
{
    int32_t ret = -EINVAL;

    if (loc->inode != NULL) {
        if (!ec_loc_gfid_check(xl, loc->gfid, loc->inode->gfid)) {  // loc的gfid与loc->inode的gfid不同  -无效值错误
            goto out;
        }
    } else if (table != NULL) {
        if (!gf_uuid_is_null(loc->gfid)) {          // loc->gfid不为空
            loc->inode = inode_find(table, loc->gfid);      // 在table中根据loc的gfid 找到inode
        } else if (loc->path && strchr (loc->path, '/')) {      // C 库函数 char *strchr(const char *str, int c) 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置。
            loc->inode = inode_resolve(table, (char *)loc->path);   // 在table中根据loc的loc->path， 由根目录的inode开始，strtok_r以'/'分隔符分割，层层查找知道找到inode
        }
    }

    ret = 0;

out:
    return ret;
}

int32_t ec_loc_setup_parent(xlator_t *xl, inode_table_t *table, loc_t *loc)
{
    char *path, *parent;
    int32_t ret = -EINVAL;

    if (loc->parent != NULL) {
        if (!ec_loc_gfid_check(xl, loc->pargfid, loc->parent->gfid)) {
            goto out;
        }
    } else if (table != NULL) {
        if (!gf_uuid_is_null(loc->pargfid)) {                   // loc->父目录的uuid不为空
            loc->parent = inode_find(table, loc->pargfid);
        } else if (loc->path && strchr (loc->path, '/')) {      // 根据loc->path找pargfid的inode
            path = gf_strdup(loc->path);
            if (path == NULL) {
                gf_msg (xl->name, GF_LOG_ERROR, ENOMEM,
                        EC_MSG_NO_MEMORY,
                        "Unable to duplicate path '%s'",
                        loc->path);

                ret = -ENOMEM;

                goto out;
            }
            parent = dirname(path);
            loc->parent = inode_resolve(table, parent);
            if (loc->parent != NULL) {
                gf_uuid_copy(loc->pargfid, loc->parent->gfid);          // 根据inode得到uuid
            }
            GF_FREE(path);
        }
    }

    /* If 'pargfid' has not been determined, clear 'name' to avoid resolutions
       based on <gfid:pargfid>/name. */
    /*
    如果尚未确定 'pargfid'，则清除 'name' 以避免基于 <gfid:pargfid>/name 的解析。
    */
    if (gf_uuid_is_null(loc->pargfid)) {
        loc->name = NULL;
    }

    ret = 0;

out:
    return ret;
}
// 如果loc为根目录或者其直接下属，检查gfid。 如果loc->name为空通过loc->path设置loc->name
int32_t ec_loc_setup_path(xlator_t *xl, loc_t *loc)
{
    uuid_t root = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    char *name;
    int32_t ret = -EINVAL;

    if (loc->path != NULL) {
        name = strrchr(loc->path, '/');     //C 库函数 char *strrchr(const char *str, int c) 在参数 str 所指向的字符串中搜索最后一次出现字符 c（一个无符号字符）的位置。
        if (name == NULL) {     // loc->path中没有'/', 判断这个path是不是<gfid: 这种类型的path
            /* Allow gfid paths: <gfid:...> */
            if (strncmp(loc->path, "<gfid:", 6) == 0) {     // C 库函数 int strncmp(const char *str1, const char *str2, size_t n) 把 str1 和 str2 进行比较，最多比较前 n 个字节。
                ret = 0;
            }
            goto out;
        }
        if (name == loc->path) {    // 最后一个/就是第一个字符'/', 就是loc->path格式为/xxxx
            if (name[1] == 0) {     // loc->path为'/'
                if (!ec_loc_gfid_check(xl, loc->gfid, root)) {      // 只有loc->gfid与root不同才返回false
                    goto out;
                }
            } else {        // loc->path为'/xxx', 其loc->pargfid应该为'//
                if (!ec_loc_gfid_check(xl, loc->pargfid, root)) {   // 只有loc->pargfid与root不同才返回false
                    goto out;
                }
            }
        }
        name++; // name由指向最后一个/   变成   指向/之后的文件名？或者空气

        if (loc->name != NULL) {        // loc->name不为空
            if (strcmp(loc->name, name) != 0) {     // 比较二者， 如果不相等出错， 无效值
                gf_msg (xl->name, GF_LOG_ERROR, EINVAL,
                        EC_MSG_INVALID_LOC_NAME,
                        "Invalid name '%s' in loc",
                        loc->name);

                goto out;
            }
        } else {                // 否则更新loc的文件名
            loc->name = name;
        }
    }

    ret = 0;

out:
    return ret;
}

int32_t ec_loc_parent(xlator_t *xl, loc_t *loc, loc_t *parent)
{
    inode_table_t *table = NULL;
    char *str = NULL;
    int32_t ret = -ENOMEM;

    memset(parent, 0, sizeof(loc_t));

    if (loc->parent != NULL) {
        table = loc->parent->table;
        parent->inode = inode_ref(loc->parent);
    } else if (loc->inode != NULL) {
        table = loc->inode->table;
    }
    if (!gf_uuid_is_null(loc->pargfid)) {
        gf_uuid_copy(parent->gfid, loc->pargfid);
    }
    if (loc->path && strchr (loc->path, '/')) {
        str = gf_strdup(loc->path);
        if (str == NULL) {
                gf_msg (xl->name, GF_LOG_ERROR, ENOMEM,
                        EC_MSG_NO_MEMORY,
                        "Unable to duplicate path '%s'",
                        loc->path);

                goto out;
        }
        parent->path = gf_strdup(dirname(str));
        if (parent->path == NULL) {
                gf_msg (xl->name, GF_LOG_ERROR, ENOMEM,
                        EC_MSG_NO_MEMORY,
                        "Unable to duplicate path '%s'",
                        dirname(str));

                goto out;
        }
    }

    ret = ec_loc_setup_path(xl, parent);
    if (ret == 0) {
        ret = ec_loc_setup_inode(xl, table, parent);
    }
    if (ret == 0) {
        ret = ec_loc_setup_parent(xl, table, parent);
    }
    if (ret != 0) {
        goto out;
    }

    if ((parent->inode == NULL) && (parent->path == NULL) &&
        gf_uuid_is_null(parent->gfid)) {
        gf_msg (xl->name, GF_LOG_ERROR, EINVAL,
                EC_MSG_LOC_PARENT_INODE_MISSING,
                "Parent inode missing for loc_t");

        ret = -EINVAL;

        goto out;
    }

    ret = 0;

out:
    GF_FREE(str);

    if (ret != 0) {
        loc_wipe(parent);
    }

    return ret;
}
// 通过inode 和loc->path设置loc中 name, inode, parent等
int32_t ec_loc_update(xlator_t *xl, loc_t *loc, inode_t *inode,
                      struct iatt *iatt)
{
    inode_table_t *table = NULL;
    int32_t ret = -EINVAL;

    if (inode != NULL) {
        table = inode->table;
        if (loc->inode != inode) {
            if (loc->inode != NULL) {
                inode_unref(loc->inode);
            }
            loc->inode = inode_ref(inode);
            gf_uuid_copy(loc->gfid, inode->gfid);
        }
    } else if (loc->inode != NULL) {
        table = loc->inode->table;          // inode表
    } else if (loc->parent != NULL) {
        table = loc->parent->table;
    }

    if (iatt != NULL) {
        if (!ec_loc_gfid_check(xl, loc->gfid, iatt->ia_gfid)) {
            goto out;
        }
    }
    // 如果loc为根目录或者其直接下属，检查gfid。 如果loc->name为空通过loc->path设置loc->name
    ret = ec_loc_setup_path(xl, loc);
    if (ret == 0) {
        ret = ec_loc_setup_inode(xl, table, loc);   // 检查loc中的inode， 为空的话通过table和loc->path找到inode
    }
    if (ret == 0) {
        ret = ec_loc_setup_parent(xl, table, loc); // 设置loc中的pargfid
    }
    if (ret != 0) {
        goto out;
    }

out:
    return ret;
}
// 这里的fd中应该会保存ctx
int32_t ec_loc_from_fd(xlator_t * xl, loc_t * loc, fd_t * fd)
{
    ec_fd_t * ctx;
    int32_t ret = -ENOMEM;

    memset(loc, 0, sizeof(*loc));

    ctx = ec_fd_get(fd, xl);        // 从fd->_ctx中获取之前保存的ctx， 或者malloc一个新的， xl作为fd->_ctx中的xl_key
    if (ctx != NULL) {
        if (loc_copy(loc, &ctx->loc) != 0) {        // fd保存的ctx
            goto out;
        }
    }

    ret = ec_loc_update(xl, loc, fd->inode, NULL);      //  更新
    if (ret != 0) {
        goto out;
    }

out:
    if (ret != 0) {
        loc_wipe(loc);
    }

    return ret;
}
// loc的拷贝
int32_t ec_loc_from_loc(xlator_t * xl, loc_t * dst, loc_t * src)
{
    int32_t ret = -ENOMEM;

    memset(dst, 0, sizeof(*dst));
    // loc 对src各项进行copy
    if (loc_copy(dst, src) != 0) {
        goto out;
    }
    // 更新loc
    ret = ec_loc_update(xl, dst, NULL, NULL);
    if (ret != 0) {
        goto out;
    }

out:
    if (ret != 0) {
        loc_wipe(dst);
    }

    return ret;
}

void ec_owner_set(call_frame_t * frame, void * owner)
{
    set_lk_owner_from_ptr(&frame->root->lk_owner, owner);
}

void ec_owner_copy(call_frame_t *frame, gf_lkowner_t *owner)
{
    lk_owner_copy (&frame->root->lk_owner, owner);
}

ec_inode_t * __ec_inode_get(inode_t * inode, xlator_t * xl)
{
    ec_inode_t * ctx = NULL;
    uint64_t value = 0;
    // 将inode->_ctx[xl->xl_id].value1指针取出，存到value中
    if ((__inode_ctx_get(inode, xl, &value) != 0) || (value == 0))      // 取失败
    {
        ctx = GF_MALLOC(sizeof(*ctx), ec_mt_ec_inode_t);
        if (ctx != NULL)
        {
            memset(ctx, 0, sizeof(*ctx));       // inode的上下文
            INIT_LIST_HEAD(&ctx->heal);         // 修复列表？

            value = (uint64_t)(uintptr_t)ctx;   // value为ec_inode_t
            if (__inode_ctx_set(inode, xl, &value) != 0)    // 将inode->_ctx与ec xlator关联起来
            {
                GF_FREE(ctx);   // 失败

                return NULL;
            }
        }
    }
    else
    {
        ctx = (ec_inode_t *)(uintptr_t)value;       // 之前操作的inode，保存的ctx
    }

    return ctx;
}

ec_inode_t * ec_inode_get(inode_t * inode, xlator_t * xl)
{
    ec_inode_t * ctx = NULL;

    LOCK(&inode->lock);

    ctx = __ec_inode_get(inode, xl);

    UNLOCK(&inode->lock);

    return ctx;
}

ec_fd_t * __ec_fd_get(fd_t * fd, xlator_t * xl)
{
    int i = 0;
    ec_fd_t * ctx = NULL;
    uint64_t value = 0;
    ec_t *ec = xl->private;

    if ((__fd_ctx_get(fd, xl, &value) != 0) || (value == 0)) {
        ctx = GF_MALLOC(sizeof(*ctx) + (sizeof (ec_fd_status_t) * ec->nodes),
                        ec_mt_ec_fd_t);
        if (ctx != NULL) {
            memset(ctx, 0, sizeof(*ctx));

            for (i = 0; i < ec->nodes; i++) {
                if (fd_is_anonymous (fd)) {
                        ctx->fd_status[i] = EC_FD_OPENED;       // 匿名文件状态为打开
                } else {
                        ctx->fd_status[i] = EC_FD_NOT_OPENED;
                }
            }

            value = (uint64_t)(uintptr_t)ctx;               // fd->_ctx的value 包含了所有节点的上下文
            if (__fd_ctx_set(fd, xl, value) != 0) {         // 在fd->_ctx 数组中找一个空闲的设置，或者有相同的xl_key,覆盖
                GF_FREE (ctx);
                return NULL;
            }
        }
    } else {
        ctx = (ec_fd_t *)(uintptr_t)value;
    }

    /* Treat anonymous fd specially */
    if (fd->anonymous) {
        /* Mark the fd open for all subvolumes. */
        ctx->open = -1;
        /* Try to populate ctx->loc with fd->inode information. */
        ec_loc_update(xl, &ctx->loc, fd->inode, NULL);      // 这里对旧的ctx，更新loc。 新malloc的ctx， loc为空
    }

    return ctx;
}

ec_fd_t * ec_fd_get(fd_t * fd, xlator_t * xl)
{
    ec_fd_t * ctx = NULL;

    LOCK(&fd->lock);

    ctx = __ec_fd_get(fd, xl);

    UNLOCK(&fd->lock);

    return ctx;
}

uint32_t ec_adjust_offset(ec_t * ec, off_t * offset, int32_t scale)
{
    off_t head, tmp;

    tmp = *offset;
    head = tmp % ec->stripe_size;
    tmp -= head;
    if (scale)
    {
        tmp /= ec->fragments;
    }

    *offset = tmp;

    return head;
}

uint64_t ec_adjust_size(ec_t * ec, uint64_t size, int32_t scale)
{
    size += ec->stripe_size - 1;
    size -= size % ec->stripe_size;
    if (scale)
    {
        size /= ec->fragments;
    }

    return size;
}

gf_boolean_t
ec_is_internal_xattr (dict_t *dict, char *key, data_t *value, void *data)
{
        if (key &&
            (strncmp (key, EC_XATTR_PREFIX, strlen (EC_XATTR_PREFIX)) == 0))
                return _gf_true;

        return _gf_false;
}

void
ec_filter_internal_xattrs (dict_t *xattr)
{
        dict_foreach_match (xattr, ec_is_internal_xattr, NULL,
                            dict_remove_foreach_fn, NULL);
}

gf_boolean_t
ec_is_data_fop (glusterfs_fop_t fop)
{
        switch (fop) {
        case GF_FOP_WRITE:
        case GF_FOP_TRUNCATE:
        case GF_FOP_FTRUNCATE:
        case GF_FOP_FALLOCATE:
        case GF_FOP_DISCARD:
        case GF_FOP_ZEROFILL:
                return _gf_true;
        default:
                return _gf_false;
        }
        return _gf_false;
}
/*
gf_boolean_t
ec_is_metadata_fop (int32_t lock_kind, glusterfs_fop_t fop)
{
        if (lock_kind == EC_LOCK_ENTRY) {
                return _gf_false;
        }

        switch (fop) {
        case GF_FOP_SETATTR:
        case GF_FOP_FSETATTR:
        case GF_FOP_SETXATTR:
        case GF_FOP_FSETXATTR:
        case GF_FOP_REMOVEXATTR:
        case GF_FOP_FREMOVEXATTR:
                return _gf_true;
        default:
                return _gf_false;
        }
        return _gf_false;
}*/
