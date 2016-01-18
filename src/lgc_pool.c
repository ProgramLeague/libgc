/*******************************************************************************

 File Name: lgc_pool.c
 Author: Lsp
 mail: CD_for_ever@163.com
 Created Time: 2016年01月17日 星期日 18时32分55秒

*******************************************************************************/

#include <lgc_list.h>
#include <lgc_pool.h>
#include <malloc.h>
#include <assert.h>

typedef struct lgc_page_s lgc_page_t;

#define LGC_PAGE_SIZE 63*1024 //63k/page

struct lgc_page_s {
	LIST_HEAD;
	uint8_t* unused;
	uint32_t block_size;
	uint32_t count;
	uint8_t p[];
};

#define PTR_SIZE sizeof(void*)

#define LGC_PAGE_FULL(page) ((page)->unused == NULL)
#define LGC_PAGE_EMPTY(page) ((page)->count == 0)

//block format: [next-ptr][page-ptr][blck-body]
static lgc_page_t* lgc_page_new(uint32_t block_size)
{
	assert(block_size % 8 == 0);
	lgc_page_t* ret = (lgc_page_t*) malloc (sizeof(lgc_page_t) + LGC_PAGE_SIZE);
	if (!ret) {
		return NULL;
	}
	ret->unused = ret->p;
	ret->block_size = block_size;
	ret->count = 0;

	uint32_t num = LGC_PAGE_SIZE / (block_size + 2 * PTR_SIZE);
	uint32_t i;
	uint8_t* ptr = ret->p;
	for (i = 0; i < num - 1; ++i) {
		uint8_t** next = (uint8_t**) ptr;
		ptr += block_size + 2 * PTR_SIZE;
		*next = ptr;
		*(next + 1) = (uint8_t*)ret;
	}
	uint8_t** next = (uint8_t**)ptr;
	*next = NULL;
	*(next + 1) = (uint8_t*)ret;

	return ret;
}

static uint8_t* lgc_page_malloc(lgc_page_t* page)
{
	uint8_t* ret = NULL;
	if (page->unused) {
		ret = page->unused + 2 * PTR_SIZE;
		page->unused = *((uint8_t**)(page->unused));
		++ page->count;
	}
	return ret;
}

static void lgc_page_free(lgc_page_t* page, uint8_t* ptr)
{
	ptr -= 2 * PTR_SIZE;
	assert(*((lgc_page_t**)(ptr + PTR_SIZE)) == page);
	assert(page->count);
	*((uint8_t**)ptr) = page->unused;
	page->unused = ptr;
	-- page->count;
}

static void lgc_page_destroy(lgc_page_t* page)
{
	assert(page->count == 0);
	free(page);
}



struct lgc_pool_s {
	uint32_t size;
	lgc_list_head_t* full_page;
	lgc_list_head_t* used_page;
	lgc_list_head_t* empty_page;
};

lgc_pool_t* lgc_pool_new(uint32_t size)
{
	lgc_pool_t* ret = (lgc_pool_t*) malloc (sizeof(lgc_pool_t));
	ret->size = size;
	ret->full_page = lgc_list_new();
	ret->used_page = lgc_list_new();
	ret->empty_page = lgc_list_new();
	return ret;
}

void lgc_pool_destroy(lgc_pool_t* pool)
{
	lgc_list_destroy(pool->full_page);
	lgc_list_destroy(pool->used_page);
	lgc_list_t* front;
	while ((front = lgc_list_front(pool->empty_page))) {
		lgc_page_t* page = (lgc_page_t*) front;
		lgc_list_erase(pool->empty_page, front);
		lgc_page_destroy(page);
	}
	lgc_list_destroy(pool->empty_page);
}

uint8_t* lgc_pool_malloc(lgc_pool_t* pool)
{
	lgc_page_t* page;
	uint8_t* ret;
	//malloc from used page
	while ((page = (lgc_page_t*) lgc_list_front(pool->used_page))) {
		ret = lgc_page_malloc(page);
		if (!ret) {
			lgc_list_erase(pool->used_page, (lgc_list_t*)page);
			lgc_list_push_back(pool->full_page, (lgc_list_t*)page);
		} else {
			return ret;
		}
	}
	//malloc from empty page
	if ((page = (lgc_page_t*) lgc_list_front(pool->empty_page)) == NULL) {
		page = lgc_page_new(pool->size);
		if (!page) {
			return NULL;
		}
		lgc_list_push_front(pool->used_page, (lgc_list_t*) page);
	}
	ret = lgc_page_malloc(page);
	assert(ret);
	return ret;
}

void lgc_pool_free(uint8_t* ptr)
{
	lgc_page_t** page = (lgc_page_t**)(ptr - PTR_SIZE);
	lgc_page_free(*page, ptr);
}

typedef struct {
	lgc_list_head_t* dest;
	lgc_list_head_t* src;
} lgc_page_args_t;

static void move_if_not_full(lgc_list_t* obj, void* data)
{
	lgc_page_t* page = (lgc_page_t*) obj;
	lgc_page_args_t* args = (lgc_page_args_t*) data;
	if (!LGC_PAGE_FULL(page)) {
		lgc_list_erase(args->src, obj);
		lgc_list_push_back(args->dest, obj);
	}
}

#define LGC_EMPTY_PAGE_SIZE 3

static void move_if_empty(lgc_list_t* obj, void* data)
{
	lgc_page_t* page = (lgc_page_t*) obj;
	lgc_page_args_t* args = (lgc_page_args_t*) data;
	if (LGC_PAGE_EMPTY(page)) {
		lgc_list_erase(args->src, obj);
		if (args->dest->size < LGC_EMPTY_PAGE_SIZE) {
			lgc_list_push_back(args->dest, obj);
		} else {
			lgc_page_destroy(page);
		}
	}
}

void lgc_pool_reorganize(lgc_pool_t* pool, int deep)
{
	lgc_page_args_t args1 = {pool->used_page, pool->full_page};
	lgc_list_foreach(pool->full_page, move_if_not_full, &args1);

	if (deep) {
		lgc_page_args_t args2 = {pool->empty_page, pool->used_page};
		lgc_list_foreach(pool->used_page, move_if_empty, &args2);
	}
}

