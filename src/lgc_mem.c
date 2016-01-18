/*******************************************************************************

 File Name: lgc_mem.c
 Author: Lsp
 mail: CD_for_ever@163.com
 Created Time: 2016年01月12日 星期二 11时05分52秒

*******************************************************************************/

#include <lgc_mem.h>
#include <lgc_impl.h>
#include <lgc_pool.h>

#include <malloc.h>
#include <string.h>
#include <assert.h>

//i-v table for handle-memory mapping
typedef struct iv_table_s iv_table_t;

static const uint32_t primes[] = {
	17,           37,            79,            163,            331,
    673,          1361,          2729,          5471,           10949,
    21911,        43853,         87719,         175447,         350899,
    701819,       1403641,       2807303,       5614657,        11229331,
    22458671,     44917381,      89834777,      179669557,      359339171,
    718678369,    1437356741,    2147483647
};

static const int primes_num = sizeof(primes) / sizeof(primes[0]);

#define IV_TABLE_MAX_SIZE (primes[primes_num-1])
#define IV_TABLE_MIN_SIZE (primes[0])

//typedef struct thunk_s thunk_t;

typedef struct handle_value_s{
	lgc_handle_t handle;
	uint32_t size;
	uint8_t* memory;
	struct handle_value_s* next;
} handle_value_t;

struct iv_table_s {
	handle_value_t** values;
	handle_value_t* storage;
	uint32_t size;
	uint32_t num;
	lgc_handle_t iter;
};

static uint32_t prime_size(uint32_t size)
{
	int i;
	for (i = 0; i < primes_num; ++i) {
		if (primes[i] > size) {
			return primes[i];
		}
	}
	return IV_TABLE_MAX_SIZE;
}

//operate functions for i-v table
static iv_table_t* iv_table_new()
{
	iv_table_t* ret = (iv_table_t*) malloc (sizeof(iv_table_t));
	memset(ret, 0, sizeof(iv_table_t));
	ret->size = IV_TABLE_MIN_SIZE;

	ret->values = (handle_value_t**) malloc (sizeof(handle_value_t*)*ret->size);
	memset(ret->values, 0, sizeof(handle_value_t*) * ret->size);
	return ret;
}

static void iv_table_destroy(iv_table_t* table)
{
	uint32_t i;
	for (i = 0; i < table->size; ++i) {
		handle_value_t* node = table->values[i];
		while (node) {
			handle_value_t* next = node->next;
			free(node);
			node = next;
		}
	}
	free(table->values);
	while (table->storage) {
		handle_value_t* next = table->storage->next;
		free(table->storage);
		table->storage = next;
	}
	free(table);
}

static void iv_table_resize(iv_table_t* table)
{
	uint32_t s = table->size;
	uint32_t n = table->num;
	if ((s >= 3 * n && s > IV_TABLE_MIN_SIZE) ||
		(3 * s <= n && s < IV_TABLE_MAX_SIZE)) {
		uint32_t new_size = prime_size(n);
		handle_value_t** new_values = (handle_value_t**)
			malloc (sizeof(handle_value_t*) * new_size);
		memset(new_values, 0, sizeof(handle_value_t*) * new_size);

		uint32_t i;
		for (i = 0; i < s; ++i) {
			handle_value_t *node, *next;
			for (node = table->values[i]; node; node = next) {
				next = node->next;
				uint32_t hash = node->handle % new_size;
				node->next = new_values[hash];
				new_values[hash] = node;
			}
		}

		free(table->values);
		table->values = new_values;
		table->size = new_size;
	}
}

static void iv_table_insert(iv_table_t* table, lgc_handle_t handle,
							uint32_t size, uint8_t* mem)
{
	uint32_t hash = handle % table->size;
	handle_value_t* node = table->values[hash];
	handle_value_t* insert;
	if (table->storage) {
		insert = table->storage;
		table->storage = insert->next;
	} else {
		insert = (handle_value_t*) malloc (sizeof(handle_value_t));
	}
	table->values[hash] = insert;
	insert->next = node;

	insert->handle = handle;
	insert->size = size;
	insert->memory = mem;

	table->num++;
	iv_table_resize(table);
}

static void iv_table_delete(iv_table_t* table, lgc_handle_t handle)
{
	uint32_t hash = handle % table->size;
	handle_value_t* node = table->values[hash];
	handle_value_t* delete = NULL;
	if (node) {
		if (node->handle == handle) {
			delete = node;
			table->values[hash] = node->next;
		} else {
			while (node->next) {
				if (node->next->handle == handle) {
					delete = node->next;
					node->next = delete->next;
					break;
				}
				node = node->next;
			}
		}

		if (delete) {
			delete->next = table->storage;
			table->storage = delete;
			table->num--;
		}
	}
}

static handle_value_t* iv_table_find(iv_table_t* table, lgc_handle_t handle)
{
	uint32_t hash = handle % table->size;
	handle_value_t* node = table->values[hash];
	while (node && node->handle != handle) {
		node = node->next;
	}
	return node;
}

typedef lgc_pool_t* lgc_pool_ptr_t;

#define MAX_POOL_SIZE 256
#define MIN_POOL_SIZE 64
#define POOL_INC_STEP 8

#define POOLS_SIZE ((MAX_POOL_SIZE - MIN_POOL_SIZE) / POOL_INC_STEP + 1)

//mem-chunks manager
struct lgc_mem_s {
	// 64-256 bytes memory pools
	lgc_pool_ptr_t pools[POOLS_SIZE];
	iv_table_t* table;
	lgc_state_t* gc;
	lgc_handle_t iter;
};

static void lgc_free(lgc_mem_t* mem, lgc_handle_t handle);

lgc_mem_t* lgc_mem_new()
{
	lgc_mem_t* ret = (lgc_mem_t*) malloc (sizeof(lgc_mem_t));
	memset(ret, 0, sizeof(lgc_mem_t));

	uint32_t psize = MIN_POOL_SIZE;
	int i = 0;
	while (psize <= MAX_POOL_SIZE) {
		ret->pools[i] = lgc_pool_new(psize);
		psize += POOL_INC_STEP;
		++i;
	}

	ret->table = iv_table_new();
	ret->gc = lgc_state_new(ret, lgc_free);
	return ret;
}

void lgc_mem_destroy(lgc_mem_t* mem)
{
	int i;
	for (i = 0; i < POOLS_SIZE; ++i) {
		lgc_pool_destroy(mem->pools[i]);
	}

	iv_table_destroy(mem->table);
	lgc_state_destroy(mem->gc);
}

static void lgc_free(lgc_mem_t* mem, lgc_handle_t handle)
{
	if (handle == INVALID_HANDLE) {
		return;
	}

	handle_value_t* node = iv_table_find(mem->table, handle);
	assert(node);
	//directly free for large memory
	if (node->size > MAX_POOL_SIZE) {
		free(node->memory);
	} else {
		lgc_pool_free(node->memory);
	}
	iv_table_delete(mem->table, handle);
}


static void memory_reorganize(lgc_mem_t* mem)
{
	switch (lgc_state_flag(mem->gc)) {
	case LGC_NONE: break;
	case LGC_GEN0: //same as gen1
	case LGC_GEN1:
		{
			int i;
			for (i = 0; i < POOLS_SIZE; ++i) {
				lgc_pool_reorganize(mem->pools[i], 0);
			}
		}; break;
	case LGC_GEN2:
		{
			int i;
			for (i = 0; i < POOLS_SIZE; ++i) {
				lgc_pool_reorganize(mem->pools[i], 1);
			}
		}; break;
	default:;
	}
}

#define LGC_ALIGN(n, a) (((n) + (a) - 1) & ~((a) - 1))

lgc_handle_t lgc_malloc(lgc_mem_t* mem, uint32_t size, lgc_traverse_t traverse,
						lgc_destructor_t destructor)
{
	lgc_handle_t handle;
	lgc_object_t* obj;
	do {
		handle = mem->iter++;
		if (handle == INVALID_HANDLE) {
			handle = mem->iter++;
		}
	} while (iv_table_find(mem->table, handle)); //unused handle

	if (size > MAX_POOL_SIZE) {
		//directly malloc for large memory
		obj = (lgc_object_t*) malloc (size);
		if (obj == NULL) {
			return INVALID_HANDLE;
		}
	} else {
		uint32_t asize = LGC_ALIGN(size, POOL_INC_STEP);
		int offset = 0;
		if (asize > MIN_POOL_SIZE) {
			offset = (asize - MIN_POOL_SIZE) / POOL_INC_STEP;
		}

		obj = (lgc_object_t*) lgc_pool_malloc(mem->pools[offset]);
		//failed, alloc a new thunk
		if (obj == NULL) {
			return INVALID_HANDLE;
		}
	}
	iv_table_insert(mem->table, handle, size, (uint8_t*)obj);
	lgc_state_register(mem->gc, obj);
	obj->traverse = traverse;
	obj->destructor = destructor;
	memory_reorganize(mem);
	
	return handle;
}

void lgc_incref(lgc_mem_t* mem, lgc_handle_t handle)
{
	assert(mem && handle != INVALID_HANDLE);
	lgc_object_t* obj = TPTR(lgc_object_t, mem, handle);
	assert(obj);
	++ obj->ref_count;
}

void lgc_decref(lgc_mem_t* mem, lgc_handle_t handle)
{
	assert(mem && handle != INVALID_HANDLE);
	lgc_object_t* obj = TPTR(lgc_object_t, mem, handle);
	assert(obj && obj->ref_count != 0);
	-- obj->ref_count;
}
