/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _LLIST_H
#define _LLIST_H

// 双向链表
struct list_head {
	struct list_head *next;
	struct list_head *prev;
};

// 初始化head，prev和next指向自己
#define INIT_LIST_HEAD(head) do {			\
		(head)->next = (head)->prev = head;	\
	} while (0)

// 在head与head->next之间插入new
// head -> heal->next
//变成head -> new -> head->next
static inline void
list_add (struct list_head *new, struct list_head *head)
{
	// new的前驱后继
	new->prev = head;
	new->next = head->next;

	// new前后节点的前驱后继
	new->prev->next = new;
	new->next->prev = new;
}

// 在head->pre和head之间插入new
// 为什么是add tail， 因为head的前一个就是tail
static inline void
list_add_tail (struct list_head *new, struct list_head *head)
{
	new->next = head;
	new->prev = head->prev;

	new->prev->next = new;
	new->next->prev = new;
}


/* This function will insert the element to the list in a order.
   Order will be based on the compare function provided as a input.
   If element to be inserted in ascending order compare should return:
    0: if both the arguments are equal
   >0: if first argument is greater than second argument
   <0: if first argument is less than second argument */
// 从head往前遍历，找到第一个符合compare(new, pos) >= 0，在pos后面插入new
// 双向链表，head可以为链表头，这样就相当于从链表尾部开始向前遍历
// 如果compre函数返回值一直<0,遍历一圈回到head，整个函数将变成list_add(new, head)
// 所有说如果链表是有序的，head最好为链表中最大值且向前遍历递减(或者说head值无效？？)，否则当new小于链表最小值，会导致插入head和head->next之间使链表变为无序 
static inline void
list_add_order (struct list_head *new, struct list_head *head,
                int (*compare)(struct list_head *, struct list_head *))
{
        struct list_head *pos = head->prev;

        while ( pos != head ) {
                if (compare(new, pos) >= 0)
                        break;

                /* Iterate the list in the reverse order. This will have
                   better efficiency if the elements are inserted in the
                   ascending order */
                pos = pos->prev;
        }

        list_add (new, pos);
}

// 删除当前节点
// 被删除节点后继0xbabebabe,后继0xcafecafe
static inline void
list_del (struct list_head *old)
{
	// 更新当前节点前驱的后继和后继的前驱
	old->prev->next = old->next;
	old->next->prev = old->prev;

	old->next = (void *)0xbabebabe;
	old->prev = (void *)0xcafecafe;
}

// 删除并初始化节点
// 被删除节点前驱后继更新为自己
static inline void
list_del_init (struct list_head *old)
{
	old->prev->next = old->next;
	old->next->prev = old->prev;

	old->next = old;
	old->prev = old;
}

// 将list从链表中删除，再插入到head之后
static inline void
list_move (struct list_head *list, struct list_head *head)
{
	list_del (list);
	list_add (list, head);
}

// 将list从链表删除，并插入到head之前
static inline void
list_move_tail (struct list_head *list, struct list_head *head)
{
	list_del (list);
	list_add_tail (list, head);
}

// 判断链表是否为空
static inline int
list_empty (struct list_head *head)
{
	return (head->next == head);
}

// list和head在同一条链表上：
// 将list->pre与head->next拼接成一条链表， head和list->next拼接成一条链，list->pre和list->next分别指向两条链
// list和head在不同链表上：将两条链表合成一条，list将不处于链表中，可以指向分割前list所在链表的前后
// 
static inline void
__list_splice (struct list_head *list, struct list_head *head)
{
	// 将list->pre与head->next连载一起
	(list->prev)->next = (head->next);
	(head->next)->prev = (list->prev);

	// 将head->next 与 list->next连在一起
	(head)->next = (list->next);
	(list->next)->prev = (head);
}

// 判断list非空再分割
static inline void
list_splice (struct list_head *list, struct list_head *head)
{
	if (list_empty (list))
		return;

	__list_splice (list, head);
}


/* Splice moves @list to the head of the list at @head. */
// 切分list和head，并初始化list为新的链表的头
static inline void
list_splice_init (struct list_head *list, struct list_head *head)
{
	if (list_empty (list))
		return;

	__list_splice (list, head);
	INIT_LIST_HEAD (list);
}

// head->prev 和 list->next连接， head与list->prev连接
// 与__list_splice类似
static inline void
__list_append (struct list_head *list, struct list_head *head)
{
	(head->prev)->next = (list->next);
        (list->next)->prev = (head->prev);
        (head->prev) = (list->prev);
        (list->prev)->next = head;
}

// 与list_splice类似
static inline void
list_append (struct list_head *list, struct list_head *head)
{
	if (list_empty (list))
		return;

	__list_append (list, head);
}


/* Append moves @list to the end of @head */
static inline void
list_append_init (struct list_head *list, struct list_head *head)
{
	if (list_empty (list))
		return;

	__list_append (list, head);
	INIT_LIST_HEAD (list);
}

// 是否是最后一个节点
static inline int
list_is_last (struct list_head *list, struct list_head *head)
{
        return (list->next == head);
}

// 包含head两个节点
static inline int
list_is_singular(struct list_head *head)
{
        return !list_empty(head) && (head->next == head->prev);
}

/**
 * list_replace - replace old entry by new one
 * @old : the element to be replaced
 * @new : the new element to insert
 *
 * If @old was empty, it will be overwritten.
 */
// old节点替换为new
static inline void list_replace(struct list_head *old,
				struct list_head *new)
{
	new->next = old->next;
	new->next->prev = new;
	new->prev = old->prev;
	new->prev->next = new;
}

// old节点replace 为new且初始话old
static inline void list_replace_init(struct list_head *old,
                                     struct list_head *new)
{
	list_replace(old, new);
	INIT_LIST_HEAD(old);
}

/**
 * list_rotate_left - rotate the list to the left
 * @head: the head of the list
 */
// head->next移动到head与head->prev之间
static inline void list_rotate_left (struct list_head *head)
{
	struct list_head *first;

	if (!list_empty (head)) {
		first = head->next;
		list_move_tail (first, head);
	}
}
// list的kv属性，传入struct中的某成员的ptr， struct type及该成员指针的变量名
// 返回该指针所在结构体的指针
#define list_entry(ptr, type, member)					\
	((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))

// 遍历list的下一个kv， ptr应该是head，head->next的struct与ptr所在struct可能不是同一个， 所以是第一个
#define list_first_entry(ptr, type, member)     \
        list_entry((ptr)->next, type, member)

// 遍历list的上一个kv， ptr应该是head，head->next的struct与ptr所在struct可能不是同一个， 所以是最后一个
#define list_last_entry(ptr, type, member)     \
        list_entry((ptr)->prev, type, member)

// 该pos->member中的变量名字为next
// pos指针所指结构体有member指针，member指针所指结构体有list_head的变量next, 
// 返回了pos指针本身？？
// 传入含有list变量，且list结构体变量名为member的原始指针pos， 返回list链表下一个pos
#define list_next_entry(pos, member) \
        list_entry((pos)->member.next, typeof(*(pos)), member)

// 同上
// 例子， 获取链表下一个，并赋值
// rlist->rvec = list_next_entry (rlist->rvec, list);
// typedef struct rbuf_iovec {
//         struct iovec iov;

//         struct list_head list;
// } rbuf_iovec_t;
#define list_prev_entry(pos, member) \
        list_entry((pos)->member.prev, typeof(*(pos)), member)

// 从head->next开始，直到返回到head，遍历head
#define list_for_each(pos, head)                                        \
	for (pos = (head)->next; pos != (head); pos = pos->next)

// head为双向链表头，pos为链表主体所在结构指针，member为head中list结构变量名，遍历双向链表
#define list_for_each_entry(pos, head, member)				\
	for (pos = list_entry((head)->next, typeof(*pos), member);	\
	     &pos->member != (head); 					\
	     pos = list_entry(pos->member.next, typeof(*pos), member))

// 对比list_for_each_entry函数，多了一个临时变量n，next下一个，改遍历中可能会对当前pos做一些插入删除操作，大概
#define list_for_each_entry_safe(pos, n, head, member)			\
	for (pos = list_entry((head)->next, typeof(*pos), member),	\
		n = list_entry(pos->member.next, typeof(*pos), member);	\
	     &pos->member != (head); 					\
	     pos = n, n = list_entry(n->member.next, typeof(*n), member))

// 向前遍历
#define list_for_each_entry_reverse(pos, head, member)                  \
	for (pos = list_entry((head)->prev, typeof(*pos), member);      \
	     &pos->member != (head);                                    \
	     pos = list_entry(pos->member.prev, typeof(*pos), member))

// 向前遍历+临时n
#define list_for_each_entry_safe_reverse(pos, n, head, member)          \
	for (pos = list_entry((head)->prev, typeof(*pos), member),      \
	        n = list_entry(pos->member.prev, typeof(*pos), member); \
	     &pos->member != (head);                                    \
	     pos = n, n = list_entry(n->member.prev, typeof(*n), member))

/*
 * This list implementation has some advantages, but one disadvantage: you
 * can't use NULL to check whether you're at the head or tail.  Thus, the
 * address of the head has to be an argument for these macros.
 */
// list结构的缺点: 无法通过判空来判断结束，需要把head传进去来判断是否结束了
// list的下一个，如果到结尾了，返回空
#define list_next(ptr, head, type, member)      \
        (((ptr)->member.next == head) ? NULL    \
                                 : list_entry((ptr)->member.next, type, member))

// list的上一个，如果到结尾了返回NULL
#define list_prev(ptr, head, type, member)      \
        (((ptr)->member.prev == head) ? NULL    \
                                 : list_entry((ptr)->member.prev, type, member))

#endif /* _LLIST_H */
