/*******************************************************************************

 File Name: lgc_mem.c
 Author: Lsp
 mail: CD_for_ever@163.com
 Created Time: 2016年01月12日 星期二 11时05分52秒

*******************************************************************************/

#include <lgc_mem.h>
#include <lgc_impl.h>

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

typedef struct thunk_s thunk_t;

typedef struct handle_value_s{
	lgc_handle_t handle;
	thunk_t* thunk;
	void* memory;
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
							thunk_t* thunk, void* mem)
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
	insert->thunk = thunk;
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

//memory thunks
typedef struct thunk_s{
	struct thunk_s* prev;
	struct thunk_s* next;
	uint8_t* ptr; //+-
	uint32_t len;
	uint32_t offset;
	uint32_t alloc_size;
	lgc_mem_t* mem;
} thunk_t;

//mem-chunks manager
struct lgc_mem_s {
	thunk_t* thunks;
	iv_table_t* table;
	lgc_state_t* gc;
	lgc_handle_t iter;
};

static uint32_t thunk_align_size(uint32_t size)
{
	uint32_t ret = size & 0xfffffff8; //enough align for double
	if (size - ret > 0) {
		ret += 8;
	}
	return ret;
}

#define THUNK_DEFAULT_SIZE 512*1024

static thunk_t* thunk_new(lgc_mem_t* mem, uint32_t size)
{
	thunk_t* ret = (thunk_t*) malloc (sizeof(thunk_t));
	memset(ret, 0, sizeof(thunk_t));
	ret->ptr = malloc (size);
	memset(ret->ptr, 0, size);
	ret->len = size;
	ret->mem = mem;
	return ret;
}

static void thunk_destroy(thunk_t* thunk)
{
	assert(thunk->alloc_size == 0);
	free(thunk->ptr);
	free(thunk);
}

static void thunk_merge(thunk_t* thunk)
{
	uint8_t* dest = thunk->ptr;
	uint32_t* src = (uint32_t*)dest;
	while ((uint8_t*)src < (thunk->ptr + thunk->len)) {
		while (*src == 0) {
			++src;
		}
		uint32_t size = thunk_align_size(*src + 2 * sizeof(uint32_t));
		//move
		if (dest != (uint8_t*)src) {
			memcpy(dest, src, size);
			handle_value_t* node = iv_table_find(thunk->mem->table, *(dest+1));
			assert(node);
			node->memory = dest + 8; //new address
			lgc_object_t* obj = (lgc_object_t*) node->memory;
			obj->prev->next = obj;
			obj->next->prev = obj;
		}
		dest += size;
		src += size;
	}
	memset(dest, 0, thunk->len - (dest - thunk->ptr));
	thunk->offset = dest - thunk->ptr;
}


static void* thunk_move(thunk_t* thunk, thunk_t* from, uint8_t* src)
{
	uint32_t* size = (uint32_t*) src;
	uint32_t alloc_size = thunk_align_size(*size + 2 * sizeof(uint32_t));
	if (thunk->len - thunk->offset >= alloc_size) {
		uint8_t* dest = thunk->ptr + thunk->offset;
		memcpy(dest, src, alloc_size);
		memset(src, 0, alloc_size);
		from->alloc_size -= alloc_size;
		thunk->offset += alloc_size;
		thunk->alloc_size += alloc_size;
		
		lgc_object_t* obj = (lgc_object_t*)(dest + 8);
		obj->prev->next = obj;
		obj->next->prev = obj;
		return obj;
	}
	return NULL;
}

//format: [4bytes size][4bytes handle][Xbytes memory];
static void* thunk_malloc(thunk_t* thunk, lgc_handle_t handle, uint32_t size)
{
	if (thunk->len - thunk->offset >= size + 2 * sizeof(uint32_t)) {
		uint32_t* head = (uint32_t*) (thunk->ptr + thunk->offset);
		*head = size;
		*(head+1) = handle;
		uint32_t alloc_size = thunk_align_size(size + 2 * sizeof(uint32_t));
		thunk->offset += alloc_size;
		thunk->alloc_size += alloc_size;

		iv_table_insert(thunk->mem->table, handle, thunk, head + 2);
		
		return head + 2; //also enough align for 'double'
	} else {
		//if (thunk->len - thunk->alloc_size >= size + 2 * sizeof(uint32_t)) {
		//	thunk_merge(thunk);
		//	return thunk_malloc(thunk, handle, size);
		//} else {
		return NULL;
		//}
	}
}

static void thunk_free(thunk_t* thunk, void* p)
{
	uint8_t* ptr = (uint8_t*)p;
	assert(ptr >= (thunk->ptr + 8) && ptr < (thunk->ptr + thunk->len));
	uint32_t* size = (uint32_t*) (ptr - 8);
	assert((ptr + *size) <= (thunk->ptr + thunk->len));
	iv_table_delete(thunk->mem->table, *(size + 1));
	uint32_t alloc_size = thunk_align_size(*size + 2 * sizeof(uint32_t));
	memset(size, 0, alloc_size);
	thunk->alloc_size -= alloc_size;
}

static void lgc_free(lgc_mem_t* mem, lgc_handle_t handle)
{
	handle_value_t* node = iv_table_find(mem->table, handle);
	if (node) {
		uint32_t* size = (uint32_t*) (node->memory - 8);
		assert(*(size+1) == handle);
		//directly free for large memory
		if (*size > (THUNK_DEFAULT_SIZE / 2 - 2 * sizeof(uint32_t))) {
			free(size);
			iv_table_delete(mem->table, handle);
		} else {
			thunk_free(node->thunk, node->memory);
		}
	}
}

lgc_mem_t* lgc_mem_new()
{
	lgc_mem_t* ret = (lgc_mem_t*) malloc (sizeof(lgc_mem_t));
	memset(ret, 0, sizeof(lgc_mem_t));

	ret->thunks = thunk_new(ret, THUNK_DEFAULT_SIZE);
	ret->thunks->prev = ret->thunks;
	ret->thunks->next = ret->thunks;
	ret->table = iv_table_new();
	ret->gc = lgc_state_new(ret, lgc_free);
	return ret;
}

void lgc_mem_destroy(lgc_mem_t* mem)
{
	thunk_t* tail = mem->thunks->prev;
	do {
		thunk_t* tmp = mem->thunks->next;
		thunk_destroy(mem->thunks);
		mem->thunks = tmp;
	} while (mem->thunks != tail);

	iv_table_destroy(mem->table);
	lgc_state_destroy(mem->gc);
}

static void memory_reorganize(lgc_mem_t* mem)
{
	switch (lgc_state_flag(mem->gc)) {
	case LGC_NONE: break;
	case LGC_GEN0:
		{
			if (mem->thunks->alloc_size < mem->thunks->offset / 2) {
				thunk_merge(mem->thunks);
			}
		}; break;
	case LGC_GEN1:
		{
			thunk_t* tail = mem->thunks->prev;
			if (mem->thunks != tail) {

			}

		}; break;
	case LGC_GEN2:
		{

		}; break;
	default:;
	}
}

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

	if (size > (THUNK_DEFAULT_SIZE / 2 - 2 * sizeof(uint32_t))) {
		//directly malloc for large memory
		uint32_t* ptr = (uint32_t*) malloc (size + 2 * sizeof(uint32_t));
		if (ptr == NULL) {
			return INVALID_HANDLE;
		}
		*ptr = size;
		*(ptr+1) = handle;
		
		obj = (lgc_object_t*) (ptr + 2);
		iv_table_insert(mem->table, handle, NULL, obj);
	} else {
		obj = (lgc_object_t*) thunk_malloc (mem->thunks, handle, size);
		//failed, alloc a new thunk
		if (obj == NULL) {
			thunk_t* new_thunk = thunk_new(mem, THUNK_DEFAULT_SIZE);
			if (new_thunk == NULL) {
				return INVALID_HANDLE;
			}
			new_thunk->next = mem->thunks;
			new_thunk->prev = mem->thunks->prev;
			mem->thunks->prev->next = new_thunk;
			mem->thunks->prev = new_thunk;
			mem->thunks = new_thunk;
			obj = (lgc_object_t*)thunk_malloc(new_thunk, handle, size);
			assert(obj);
		}
	}

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
