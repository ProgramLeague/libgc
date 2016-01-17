/*******************************************************************************

 File Name: lgc_mem.h
 Author: Lsp
 mail: CD_for_ever@163.com
 Created Time: 2016年01月12日 星期二 11时05分36秒

*******************************************************************************/

#ifndef __LGC_MEM_H__
#define __LGC_MEM_H__

#include <stdint.h> //for uint32_t
#include <lgc.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lgc_mem_s lgc_mem_t;

typedef uint32_t lgc_handle_t;

lgc_mem_t* lgc_mem_new();

void lgc_mem_destroy(lgc_mem_t* mem);

lgc_handle_t lgc_malloc(lgc_mem_t* mem, uint32_t size, lgc_traverse_t traverse,
						lgc_destructor_t destructor);

void lgc_incref(lgc_mem_t* mem, lgc_handle_t handle);

void lgc_decref(lgc_mem_t* mem, lgc_handle_t handle);

void* lgc_pointer(lgc_mem_t* mem, lgc_handle_t handle);

#define TPTR(type, mem, handle) ((type*)lgc_pointer((mem), (handle)))

//set 0 as invalid handle
#define INVALID_HANDLE 0

typedef void (*lgc_free_t)(lgc_mem_t* mem, lgc_handle_t handle);

#ifdef __cplusplus
}
#endif
	
#endif
