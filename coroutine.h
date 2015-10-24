#ifndef _COEVT_COROUTINE_H_
#define _COEVT_COROUTINE_H_

typedef struct ce_scheduler ce_scheduler;
typedef struct ce_coroutine ce_coroutine;
typedef void (*coroutine_func)(void *arg); 

int ce_init_scheduler(int stack_size, int init_cap);
int ce_close_scheduler();
int ce_cur_coroutine();
int ce_coroutine_cnt();

int ce_coroutine_create(coroutine_func func, void *arg);
void ce_coroutine_resume(int coroutine_id);
void ce_coroutine_yield();
void ce_coroutine_block();
void ce_coroutine_exit(int coroutine_id);

int ce_get_coroutine_status(int coroutine_id);
int ce_set_coroutine_status(int coroutine_id, int status);

#endif
