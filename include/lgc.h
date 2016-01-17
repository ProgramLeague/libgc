/*******************************************************************************

 File Name: lgc.h
 Author: Lsp
 mail: CD_for_ever@163.com
 Created Time: 2016年01月12日 星期二 11时55分38秒

*******************************************************************************/

#ifndef __LGC_H__
#define __LGC_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lgc_object_s lgc_object_t;

typedef void (*lgc_mark_t)(lgc_object_t* obj, void* ctx);
typedef void (*lgc_traverse_t)(lgc_object_t* self, lgc_mark_t func, void* ctx);
typedef void (*lgc_destructor_t)(lgc_object_t* self);

//extends LIST_HEAD
#define GC_HEAD									\
	lgc_object_t* prev;							\
	lgc_object_t* next;							\
	lgc_traverse_t traverse;					\
	lgc_destructor_t destructor;				\
	uint32_t ref_count;							\
	uint32_t gc_count;							\

struct lgc_object_s {
	GC_HEAD;
};

#ifdef __cplusplus
}
#endif

#endif
