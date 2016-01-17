/*******************************************************************************

 File Name: lgc_impl.h
 Author: Lsp
 mail: CD_for_ever@163.com
 Created Time: 2016年01月17日 星期日 18时12分38秒

*******************************************************************************/

#ifndef __LGC_IMPL_H__
#define __LGC_IMPL_H__

#include <lgc.h>

typedef struct lgc_state_s lgc_state_t;

typedef enum {
	LGC_NONE,
	LGC_GEN0,
	LGC_GEN1,
	LGC_GEN2
} LGC_STATE;

//API for lgc_mem
lgc_state_t* lgc_state_new(lgc_mem_t* mem, lgc_free_t func);
void lgc_state_destroy(lgc_state_t* gc);
void lgc_state_register(lgc_state_t* gc, lgc_object_t* obj);
LGC_STATE lgc_state_flag(lgc_state_t* gc);

#endif
