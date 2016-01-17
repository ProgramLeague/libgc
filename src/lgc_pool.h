/*******************************************************************************

 File Name: lgc_pool.h
 Author: Lsp
 mail: CD_for_ever@163.com
 Created Time: 2016年01月17日 星期日 18时32分53秒

*******************************************************************************/

#ifndef __LGC_POOL_H__
#define __LGC_POOL_H__

#include <stdint.h>

typedef struct lgc_pool_s lgc_pool_t;

lgc_pool_t* lgc_pool_new(uint32_t size);
uint8_t* lgc_pool_malloc(lgc_pool_t* pool);
void lgc_pool_free(uint8_t* ptr);
void lgc_pool_reorganize(lgc_pool_t* pool);
void lgc_pool_destroy(lgc_pool_t* pool);

#endif
