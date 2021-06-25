/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _CB_H
#define _CB_H

#include "common-utils.h"
#include "logging.h"
#include "mem-types.h"

#define BUFFER_SIZE 10
#define TOTAL_SIZE BUFFER_SIZE + 1


struct _circular_buffer {
        struct timeval tv;              // add的时候时间
        void *data;
};

typedef struct _circular_buffer circular_buffer_t;

struct _buffer {
        unsigned int w_index;           // 下一次add指向的circular_buffer_t的index
        size_t  size_buffer;            // circular_buffer_t **cb指针数组的size
        gf_boolean_t use_once;          // 是否只用一次，true就是只用一次，不能循环写
        /* This variable is assigned the proper value at the time of initing */
        /* the buffer. It indicates, whether the buffer should be used once */
        /*  it becomes full. */

        int  used_len;          // 已经使用的长度
        /* indicates the amount of circular buffer used. */

        circular_buffer_t **cb;         // 指针数组， 每个指针指向_circular_buffer,data由外部传入，本身由接口申请释放
        void (*destroy_buffer_data) (void *data);       // circular_buffer_t释放之前调用函数，对data进行释放
        pthread_mutex_t   lock;         // __cb_add_entry_buffer，cb_buffer_show， cb_buffer_dump需要加锁
};

typedef struct _buffer buffer_t;

int
cb_add_entry_buffer (buffer_t *buffer, void *item);

void
cb_buffer_show (buffer_t *buffer);

buffer_t *
cb_buffer_new (size_t buffer_size,gf_boolean_t use_buffer_once,
               void (*destroy_data) (void *data));

void
cb_buffer_destroy (buffer_t *buffer);

void
cb_buffer_dump (buffer_t *buffer, void *data,
                int (fn) (circular_buffer_t *buffer, void *data));

#endif /* _CB_H */
