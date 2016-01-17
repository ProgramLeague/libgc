/*******************************************************************************

 File Name: lgc_list.h
 Author: Lsp
 mail: CD_for_ever@163.com
 Created Time: 2016年01月17日 星期日 15时13分07秒

*******************************************************************************/

#ifndef __LGC_LIST_H__
#define __LGC_LIST_H__

#include <stdint.h>
#include <malloc.h>
#include <assert.h>

typedef struct lgc_list_s lgc_list_t;

#define LIST_HEAD lgc_list_t* prev; lgc_list_t* next

struct lgc_list_s {
	LIST_HEAD;
};

typedef struct {
	LIST_HEAD;
	uint32_t size;
} lgc_list_head_t;

static inline
lgc_list_head_t* lgc_list_new()
{
	lgc_list_head_t* ret = (lgc_list_head_t*) malloc (sizeof(lgc_list_head_t));
	ret->prev = ret->next = (lgc_list_t*) ret;
	ret->size = 0;
	return ret;
}

static inline
void lgc_list_destroy(lgc_list_head_t* head)
{
	assert(head->size == 0); //assert list is empty
	free(head);
}

static inline
void lgc_list_push_back(lgc_list_head_t* head, lgc_list_t* obj)
{
	head->prev->next = obj;
	obj->prev = head->prev;
	head->prev = obj;
	obj->next = (lgc_list_t*) head;
	++ head->size;
}

static inline
void lgc_list_push_front(lgc_list_head_t* head, lgc_list_t* obj)
{
	head->next->prev = obj;
	obj->next = head->next;
	head->next = obj;
	obj->prev = (lgc_list_t*) head;
	++ head->size;
}

static inline
lgc_list_t* lgc_list_front(lgc_list_head_t* head)
{
	if (head->next == (lgc_list_t*)head) {
		return NULL;
	} else {
		return head->next;
	}
}

static inline
void lgc_list_erase(lgc_list_head_t* head, lgc_list_t* obj)
{
	obj->prev->next = obj->next;
	obj->next->prev = obj->prev;
	-- head->size;
}

static inline
void lgc_list_combine(lgc_list_head_t* dest, lgc_list_head_t* src)
{
	dest->prev->next = src->next;
	src->next->prev = dest->prev;
	src->prev->next = (lgc_list_t*) dest;
	dest->prev = src->prev;
	src->prev = src->next = (lgc_list_t*) src;
	dest->size += src->size;
	src->size = 0;
}

typedef void (*lgc_list_cb_t)(lgc_list_t* obj, void* data);

static inline
void lgc_list_foreach(lgc_list_head_t* head, lgc_list_cb_t cb, void* data)
{
	lgc_list_t* iter = head->next;
	while (iter != (lgc_list_t*) head) {
		lgc_list_t* tmp = iter->next;
		cb(iter, data);
		iter = tmp;
	}
}

#define LGC_LIST_FOREACH(head, cb)				\
	do {										\
		lgc_list_t* iter = head->next;			\
		while (iter != (lgc_list_t*) head) {	\
			lgc_list_t* tmp = iter->next;		\
			cb(iter);							\
			iter = tmp;							\
		}										\
	} while (0)

#endif
