#ifndef _COEVT_DEFS_H_
#define _COEVT_DEFS_H_

// contants for events
#define MAX_FD_NUM 1024
#define MAX_EPOLL_EVTS (MAX_FD_NUM * 2)
#define POLL_TIMEOUT 1
#define CE_READ 1
#define CE_WRITE 2

// contants for coroutines
#define STACK_SIZE (1024 * 1024)
#define INIT_CAPACITY 16

#define CE_COROUTINE_IDLE 0
#define CE_COROUTINE_READY 1
#define CE_COROUTINE_RUNNING 2
#define CE_COROUTINE_SUSPENDED 3
#define CE_COROUTINE_BLOCKED 4

#define CE_DUMMY_COROUTINE_ID -1

// global contants
#define CE_SUCCESS 0
#define CE_FAILURE -1
#define TRUE 1
#define FALSE 0

#endif
