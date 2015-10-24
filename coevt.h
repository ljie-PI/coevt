#ifndef _COEVT_H_
#define _COEVT_H_

#include <unistd.h>

typedef void (*task_func)(void *arg);
void ce_task(task_func func, void *arg);
int ce_cur_task();

int ce_listen(int fd, int event);
int ce_unlisten(int fd, int event);
void ce_yield();
int ce_wait();
int ce_run();

int ce_set_block(int fd);
ssize_t ce_read(int fd, void *buf, size_t count);
ssize_t ce_write(int fd, const void *buf, size_t count);
int ce_close(int fd);

#endif
