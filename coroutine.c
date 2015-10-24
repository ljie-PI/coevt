#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <ucontext.h>
#include "defs.h"
#include "coroutine.h"

struct ce_scheduler {
    int capacity;
    int size;
    char *run_stack;
    int stack_size;
    ucontext_t ctx;
    ce_coroutine **coroutine_list;
    int cur_running;
};

struct ce_coroutine {
    ucontext_t ctx;
    char *stack;
    int stack_size;
    coroutine_func func;
    void *arg;
    int status;
    int self_id;
};

/*
  unique scheduler in the process
  use multiple processes in multicore arch
*/
static ce_scheduler scheduler;

int ce_init_scheduler(int stack_size, int init_cap)
{
    size_t size_in_bytes;

    scheduler.capacity = init_cap;
    scheduler.size = 0;
    size_in_bytes = sizeof(char) * stack_size;
    scheduler.run_stack = (char *)malloc(size_in_bytes);
    if (scheduler.run_stack == NULL) {
        printf("ERROR: Failed to allocate space for stack of scheduler\n");
        return CE_FAILURE;
    }
    scheduler.stack_size = size_in_bytes;
    memset(scheduler.run_stack, 0, size_in_bytes);

    size_in_bytes = sizeof(ce_coroutine *) * init_cap;
    scheduler.coroutine_list = (ce_coroutine **)malloc(size_in_bytes);
    if (scheduler.coroutine_list == NULL) {
        printf("ERROR: Failed to allocate space for coroutine list\n");
        return CE_FAILURE;
    }
    memset(scheduler.coroutine_list, 0, size_in_bytes);

    scheduler.cur_running = CE_DUMMY_COROUTINE_ID;
    return CE_SUCCESS;
}

int ce_close_scheduler()
{
    int i;
    for (i = 0; i < scheduler.size; i++) {
        ce_coroutine *crtn = scheduler.coroutine_list[i];
        free(crtn->stack);
        free(crtn);
    }
    free(scheduler.run_stack);

    return CE_SUCCESS;
}

int ce_cur_coroutine()
{
    return scheduler.cur_running;
}

int ce_coroutine_cnt()
{
    return scheduler.size;
}

static int enlarge_coroutine_list()
{
    size_t size_in_bytes
        = sizeof(ce_coroutine *) * scheduler.capacity * 2;
    scheduler.coroutine_list = (ce_coroutine **)realloc(scheduler.coroutine_list,
                                                        size_in_bytes);
    if (scheduler.coroutine_list == NULL) {
        printf("ERROR: Failed to re-allocate space for coroutine list\n");
        return CE_FAILURE;
    }
    memset(scheduler.coroutine_list + scheduler.capacity,
           0, size_in_bytes / 2);
    scheduler.capacity *= 2;

    return CE_SUCCESS;
}

int ce_coroutine_create(coroutine_func func, void *arg)
{
    size_t size_in_bytes;
    ce_coroutine *new_crtn;
    int new_id;

    if (!scheduler.capacity) {
        if (ce_init_scheduler(STACK_SIZE, INIT_CAPACITY) != 0) {
            printf("ERROR: Failed to initialize scheduler\n");
            return CE_DUMMY_COROUTINE_ID;
        }
    }

    if (scheduler.size == scheduler.capacity) {
        if (enlarge_coroutine_list() != 0) {
            printf("ERROR: Failed to enlarge coroutine list\n");
            return CE_DUMMY_COROUTINE_ID;
        }
    }

    size_in_bytes = sizeof(ce_coroutine);
    new_crtn = (ce_coroutine *)malloc(size_in_bytes);
    if (new_crtn == NULL) {
        printf("ERROR: Failed to allocate space for new coroutine\n");
        return CE_DUMMY_COROUTINE_ID;
    }
    new_crtn->stack = NULL;
    new_crtn->stack_size = 0;
    new_crtn->func = func;
    new_crtn->arg = arg;
    new_crtn->status = CE_COROUTINE_READY;
    new_id = scheduler.size++;
    new_crtn->self_id = new_id;
    scheduler.coroutine_list[new_id] = new_crtn;

    return new_id;
}

static void fill_slot_with_last(int idx)
{
    if (scheduler.size >= 1) {
        if (idx < scheduler.size - 1) {
            scheduler.coroutine_list[idx]
                = scheduler.coroutine_list[scheduler.size - 1];
            scheduler.coroutine_list[idx]->self_id = idx;
        }
        scheduler.coroutine_list[scheduler.size--] = NULL;
    } else {
        // last coroutine finished
        scheduler.coroutine_list[0] = NULL;
    }
}

static void wrap_crtn_func(uint32_t low_bits, uint32_t high_bits)
{
    uintptr_t arg_ptr = (uintptr_t)low_bits | (uintptr_t)high_bits << 32;
    ce_coroutine *crtn = (ce_coroutine *)arg_ptr;
    int crtn_id;

    crtn->func(crtn->arg);
    free(crtn->stack);
    free(crtn);

    // after coroutine finished, swap the last coroutine to this slot,
    // so can avoid looking for idle slot when create new coroutine
    crtn_id = scheduler.cur_running;
    fill_slot_with_last(crtn_id);

    scheduler.cur_running = CE_DUMMY_COROUTINE_ID;
}

void ce_coroutine_resume(int crtn_id)
{
    ce_coroutine *crtn;

    if (crtn_id == CE_DUMMY_COROUTINE_ID) {
        printf("ERROR: Could not resume a dummy coroutine\n");
        return;
    }
    crtn = scheduler.coroutine_list[crtn_id];
    if (crtn == NULL) {
        printf("ERROR: Could not resume a null coroutine\n");
        return;
    }

    switch (crtn->status) {
    case CE_COROUTINE_READY:
        getcontext(&crtn->ctx);
        crtn->ctx.uc_stack.ss_size = scheduler.stack_size;
        crtn->ctx.uc_stack.ss_sp = scheduler.run_stack;
        crtn->ctx.uc_link = &scheduler.ctx;
        crtn->status = CE_COROUTINE_RUNNING;
        scheduler.cur_running = crtn_id;
        uintptr_t arg_ptr = (uintptr_t)crtn;
        // split crtn ptr to low bits and high bits,
        // so that it can work on both 32bits arch and 64bits arch
        // but it is said that compatibility is guaranteed since glibc 2.8
        makecontext(&crtn->ctx,
                    (void (*)(void))wrap_crtn_func,
                    2,
                    (uint32_t)arg_ptr,
                    (uint32_t)(arg_ptr >> 32));
        swapcontext(&scheduler.ctx, &crtn->ctx);
        break;
    case CE_COROUTINE_SUSPENDED:
        memcpy(scheduler.run_stack + scheduler.stack_size - crtn->stack_size,
               crtn->stack,
               crtn->stack_size);
        crtn->status = CE_COROUTINE_RUNNING;
        scheduler.cur_running = crtn_id;
        swapcontext(&scheduler.ctx, &crtn->ctx);
        break;
    default:
        // other status that shouldn't be resumed
        break;
    }
}

static int save_stack(ce_coroutine *crtn, ce_scheduler *sched)
{
    char dummy = 0;
    free(crtn->stack);
    crtn->stack_size = sched->run_stack + sched->stack_size - &dummy;
    crtn->stack = (char *)malloc(sizeof(char) * crtn->stack_size);
    if (crtn->stack == NULL) {
        printf("Failed to new space to save stack\n");
        return CE_FAILURE;
    }
    memcpy(crtn->stack, &dummy, sizeof(char) * crtn->stack_size);

    return CE_SUCCESS;
}

static void ce_coroutine_pause(int to_status)
{
    int crtn_id = scheduler.cur_running;
    ce_coroutine *crtn = scheduler.coroutine_list[crtn_id];
    if ((char *)&crtn <= scheduler.run_stack) {
        // current coroutine has run out of available stack
        printf("ERROR: Current coroutine has run out of available stack\n");
        return;
    }
    if (save_stack(crtn, &scheduler) != 0) {
        printf("ERROR: Failed to save stack before pausing a coroutine\n");
        return;
    }
    crtn->status = to_status;
    scheduler.cur_running = CE_DUMMY_COROUTINE_ID;
    swapcontext(&crtn->ctx, &scheduler.ctx);
}

void ce_coroutine_yield()
{
    ce_coroutine_pause(CE_COROUTINE_SUSPENDED);
}

void ce_coroutine_block()
{
    ce_coroutine_pause(CE_COROUTINE_BLOCKED);
}

void ce_coroutine_exit(int crtn_id)
{
    ce_coroutine *crtn = scheduler.coroutine_list[crtn_id];
    if (crtn != NULL) {
        free(crtn->stack);
        free(crtn);
        fill_slot_with_last(crtn_id);
    }
}

int ce_get_coroutine_status(int crtn_id)
{
    ce_coroutine *crtn;

    if (crtn_id < 0) {
        return CE_COROUTINE_IDLE;
    }

    crtn = scheduler.coroutine_list[crtn_id];
    if (crtn == NULL) {
        return CE_COROUTINE_IDLE;
    }

    return crtn->status;
}

int ce_set_coroutine_status(int crtn_id, int status)
{
    ce_coroutine *crtn = scheduler.coroutine_list[crtn_id];
    if (crtn == NULL) {
        return CE_FAILURE;
    }
    crtn->status = status;
    
    return CE_SUCCESS;
}
