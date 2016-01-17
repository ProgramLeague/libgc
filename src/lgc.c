/*******************************************************************************

 File Name: lgc.c
 Author: Lsp
 mail: CD_for_ever@163.com
 Created Time: 2016年01月12日 星期二 11时55分43秒

*******************************************************************************/

#include <lgc_mem.h>
#include <lgc_impl.h>
#include <lgc_list.h>

#include <string.h>
#include <assert.h>
#include <malloc.h>

struct lgc_state_s {
	lgc_mem_t* mem;
	lgc_free_t lfree;
	lgc_list_head_t* gen0;
	uint32_t threshold0;
	lgc_list_head_t* gen1;
	uint32_t threshold1;
	lgc_list_head_t* gen2;
	uint32_t threshold2;
	lgc_list_head_t* gc_root;
	uint32_t count;

	LGC_STATE state;
};


lgc_state_t* lgc_state_new(lgc_mem_t* mem, lgc_free_t func)
{
	lgc_state_t* ret = (lgc_state_t*) malloc (sizeof(lgc_state_t));
	memset(ret, 0, sizeof(lgc_state_t));
	ret->mem = mem;
	ret->lfree = func;

	ret->gen0 = lgc_list_new();
	ret->gen1 = lgc_list_new();
	ret->gen2 = lgc_list_new();
	ret->gc_root = lgc_list_new();

	ret->threshold0 = 20;
	ret->threshold1 = 40;
	ret->threshold2 = 500;

	return ret;
}

void lgc_state_destroy(lgc_state_t* gc)
{
	lgc_list_destroy(gc->gen0);
	lgc_list_destroy(gc->gen1);
	lgc_list_destroy(gc->gen2);
	lgc_list_destroy(gc->gc_root);
	free(gc);
}

#define SET_GC_COUNT_CB(obj) \
	((lgc_object_t*)obj)->gc_count = ((lgc_object_t*)obj)->ref_count

//lgc_mark_t func
static void dec_gc_count(lgc_object_t* obj, void* data)
{
	assert(obj);
	if (obj->gc_count > 0) {
		-- obj->gc_count;
	}
}

static void visit_gc_count_cb(lgc_list_t* obj, void* data)
{
	lgc_object_t* gc_obj = (lgc_object_t*) obj;
	if (gc_obj->traverse) {
		gc_obj->traverse(gc_obj, dec_gc_count, NULL);
	}
}

typedef struct {
	lgc_list_head_t* dest;
	lgc_list_head_t* src;
} lgc_list_args_t;

static void collect_root_cb(lgc_list_t* obj, void* data)
{
	lgc_object_t* gc_obj = (lgc_object_t*) obj;
	if (gc_obj->gc_count > 0) {
		lgc_list_args_t* args = (lgc_list_args_t*) data;
		lgc_list_erase(args->src, obj);
		lgc_list_push_back(args->dest, obj);
	}
}

static void search_root(lgc_list_head_t* head, lgc_list_head_t* root)
{
	LGC_LIST_FOREACH(head, SET_GC_COUNT_CB);
	lgc_list_foreach(head, visit_gc_count_cb, NULL);

	lgc_list_args_t args = {root, head};
	lgc_list_foreach(head, collect_root_cb, &args);
}

static void collect_cb(lgc_list_t* obj, void* data)
{
	lgc_object_t* gc_obj = (lgc_object_t*) obj;
	if (gc_obj->traverse) {
		gc_obj->traverse(gc_obj, (lgc_mark_t)collect_root_cb, data);
		gc_obj->gc_count = 0;
	}
}

#define MARK_SRC_LIST_CB(obj) \
	((lgc_object_t*)obj)->gc_count = 1

static void collect_reachable(lgc_list_head_t* root, lgc_list_head_t* src)
{
	LGC_LIST_FOREACH(src, MARK_SRC_LIST_CB);
	
	lgc_list_args_t args = {root, src};
	lgc_list_foreach(root, collect_cb, &args);
}

typedef struct {
	lgc_state_t* gc;
	lgc_list_head_t* head;
} lgc_list_free_args_t;

static void free_cb(lgc_list_t* obj, void* data)
{
	lgc_object_t* gc_obj = (lgc_object_t*) obj;
	lgc_list_free_args_t* args = (lgc_list_free_args_t*) data;
	if (gc_obj->destructor) {
		gc_obj->destructor(gc_obj);
	}
	uint32_t* handle = (uint32_t*) obj;
	args->gc->lfree(args->gc->mem, *(handle - 1));
	-- args->gc->count;
	lgc_list_erase(args->head, obj);
}

static void free_unreachable(lgc_state_t* gc, lgc_list_head_t* list)
{
	lgc_list_free_args_t args = {gc, list};
	lgc_list_foreach(list, free_cb, &args);
}

static void full_gc(lgc_state_t* gc, int gen)
{
	lgc_list_head_t* head = gc->gen0;
	switch (gen) {
	case 2: lgc_list_combine(head, gc->gen2); //pass to 1
	case 1: lgc_list_combine(head, gc->gen1); //pass to 0
	case 0: break;
	default:
		assert(0);
	}

	search_root(head, gc->gc_root);
	collect_reachable(gc->gc_root, head);
	free_unreachable(gc, head);

	switch(gen) {
	case 2:
		{
			lgc_list_combine(gc->gen2, gc->gc_root);
			gc->state = LGC_GEN2;
		}; break;
	case 1:
		{
			lgc_list_combine(gc->gen2, gc->gc_root);
			gc->state = LGC_GEN1;
		}; break;
	case 0:
		{
			lgc_list_combine(gc->gen1, gc->gc_root);
			gc->state = LGC_GEN0;
		}; break;
	default:;
	}
}

void lgc_state_register(lgc_state_t* gc, lgc_object_t* obj)
{
	lgc_list_push_back(gc->gen0, (lgc_list_t*)obj);
	++ gc->count;
	if ((gc->count % gc->threshold2) == 0) {
		full_gc(gc, 2);
	} else if ((gc->count % gc->threshold1) == 0) {
		full_gc(gc, 1);
	} else if ((gc->count % gc->threshold0) == 0) {
		full_gc(gc, 0);
	}
}

LGC_STATE lgc_state_flag(lgc_state_t* gc)
{
	if (gc->state != LGC_NONE) {
		LGC_STATE tmp = gc->state;
		gc->state = LGC_NONE;
		return tmp;
	}
	return LGC_NONE;
}
