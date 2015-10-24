#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include "defs.h"
#include "poller.h"
#include "coroutine.h"
#include "coevt.h"

void ce_task(task_func func, void *arg)
{
    int crtn_id;
    if ((crtn_id = ce_coroutine_create(func, arg)) == CE_DUMMY_COROUTINE_ID) {
        return;
    }

    ce_coroutine_resume(crtn_id);
}

int ce_cur_task()
{
    return ce_cur_coroutine();
}

int ce_listen(int fd, int event)
{
    if (!ce_poller_initialized()) {
        if (ce_poller_init(MAX_FD_NUM) != 0) {
            return CE_FAILURE;
        }
    }

    if (ce_poller_lookup(fd, event) == CE_SUCCESS) {
        printf("INFO: Event already listened on fd %d\n", fd);
        return CE_SUCCESS;
    }

    if (ce_poller_add(fd, event) != CE_SUCCESS) {
        return CE_FAILURE;
    }

    return CE_SUCCESS;
}

int ce_unlisten(int fd, int event)
{
    if (!ce_poller_initialized()) {
        printf("INFO: Poller is not initialized\n");
        return CE_FAILURE;
    }

    if (ce_poller_lookup(fd, event) != CE_SUCCESS) {
        printf("INFO: Event is not listened on fd %d\n", fd);
        return CE_FAILURE;
    }

    if (ce_poller_remove(fd, event) != CE_SUCCESS) {
        return CE_FAILURE;
    }

    return CE_SUCCESS;
}

void ce_yield()
{
    ce_coroutine_yield();
}

int ce_wait()
{
    ce_coroutine_block();

    return CE_SUCCESS;
}

int ce_run()
{
    while (ce_coroutine_cnt() > 0) {
        int crtn_id;

        if (ce_poller_poll(POLL_TIMEOUT) != CE_SUCCESS) {
            return CE_FAILURE;
        }
        if (ce_poller_react() != CE_SUCCESS) {
            return CE_FAILURE;
        }
        for (crtn_id = 0; crtn_id < ce_coroutine_cnt(); crtn_id++) {
        // must use ce_coroutine_cnt to get coroutine counts dynamiclly 
            int status = ce_get_coroutine_status(crtn_id);
            if (status == CE_COROUTINE_READY
                || status == CE_COROUTINE_SUSPENDED) {
                ce_coroutine_resume(crtn_id);
            }
        }
    }

    return ce_close_scheduler();
}

int ce_set_nonblock(int fd)
{
    int flags;
    flags = fcntl(fd, F_GETFL, 0);
    flags |= O_NONBLOCK;
    flags |= O_NDELAY;
    if (fcntl(fd, F_SETFL, flags) != 0) {
        printf("ERROR: Failed to set fd %d to nonblock\n", fd);
        return CE_FAILURE;
    }

    return CE_SUCCESS;
}

ssize_t ce_read(int fd, void *buf, size_t count)
{
    ssize_t ret;

    if (ce_set_nonblock(fd) == CE_FAILURE) {
        printf("ERROR: Failed to set read fd nonblock\n");
        return CE_FAILURE;
    }
    ce_listen(fd, CE_READ);
    if (ce_wait() != CE_SUCCESS) {
        return CE_FAILURE;
    }

    ret = read(fd, buf, count);
    ce_unlisten(fd, CE_READ);

    return ret;
}

ssize_t ce_write(int fd, const void *buf, size_t count)
{
    ssize_t ret;

    if (ce_set_nonblock(fd) == CE_FAILURE) {
        printf("ERROR: Failed to set write fd nonblock\n");
        return CE_FAILURE;
    }
    ce_listen(fd, CE_WRITE);
    if (ce_wait() != CE_SUCCESS) {
        return CE_FAILURE;
    }

    ret = write(fd, buf, count);
    ce_unlisten(fd, CE_WRITE);

    return ret;
}

int ce_close(int fd)
{
    if (ce_poller_lookup(fd, CE_READ) != CE_SUCCESS
        && ce_poller_lookup(fd, CE_WRITE) != CE_SUCCESS) {
        if (close(fd) != 0) {
            printf("ERROR: Failed to close fd\n");
            return CE_FAILURE;
        }
    }

    return CE_SUCCESS;
}
