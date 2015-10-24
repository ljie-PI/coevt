#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include "defs.h"
#include "coroutine.h"
#include "poller.h"

typedef struct ce_fd_assoc {
    int fd;
    int rd_crtn;
    int wt_crtn;
    struct epoll_event rd_evt;
    struct epoll_event wt_evt;
} ce_fd_assoc;

/*
static variables in the process
use multiple processes in multicore arch
*/
static ce_fd_assoc *fd_assoc_arr[MAX_FD_NUM] = { 0 };
static int poller_fd = 0;
static int ready_cnt = 0;
static int polling_cnt = 0;
static struct epoll_event *poll_results;

int ce_poller_init(int max_fd)
{
    poller_fd = epoll_create(max_fd);
    if (poller_fd == -1) {
        printf("ERROR: Failed to initialize poller\n");
        return CE_FAILURE;
    }

    return CE_SUCCESS;
}

int ce_poller_initialized()
{
    if (poller_fd == 0) {
        return FALSE;
    }

    return TRUE;
}

int ce_poller_lookup(int fd, int event)
{
    ce_fd_assoc *fd_assoc = fd_assoc_arr[fd];
    if (fd_assoc == NULL) {
        return CE_FAILURE;
    }
    if (fd_assoc->rd_crtn != CE_DUMMY_COROUTINE_ID
        && event == CE_READ) {
        return CE_SUCCESS;
    }
    if (fd_assoc->wt_crtn != CE_DUMMY_COROUTINE_ID
        && event == CE_WRITE) {
        return CE_SUCCESS;
    }
    
    return CE_FAILURE;
}

int ce_poller_add(int fd, int event)
{
    ce_fd_assoc *fd_assoc = fd_assoc_arr[fd];
    int need_add = FALSE;
    int cur_crtn = ce_cur_coroutine();
    struct epoll_event *evt = NULL;

    if (fd_assoc == NULL) {
        size_t size_in_bytes = sizeof(ce_fd_assoc);
        fd_assoc = (ce_fd_assoc*)malloc(size_in_bytes);
        if (fd_assoc == NULL) {
            printf("ERROR: Failed to allocate space for struct ce_fd_assoc\n");
            return CE_FAILURE;
        }
        memset(fd_assoc, 0, size_in_bytes);
        fd_assoc->fd = fd;
        fd_assoc->rd_crtn = fd_assoc->wt_crtn = CE_DUMMY_COROUTINE_ID;
        fd_assoc_arr[fd] = fd_assoc;
        need_add = TRUE;
    }

    if (event == CE_READ) {
        fd_assoc->rd_crtn = cur_crtn;
        evt = &(fd_assoc->rd_evt);
    } else if (event == CE_WRITE){
        fd_assoc->wt_crtn = cur_crtn;
        evt = &(fd_assoc->wt_evt);
    }
    evt->data.ptr = fd_assoc;
    evt->events = (fd_assoc->rd_crtn != CE_DUMMY_COROUTINE_ID ? EPOLLIN : 0)
                | (fd_assoc->wt_crtn != CE_DUMMY_COROUTINE_ID ? EPOLLOUT : 0);
    if (need_add) {
        if (epoll_ctl(poller_fd, EPOLL_CTL_ADD, fd, evt) != 0) {
            printf("ERROR: Failed to add event in epoll_ctl\n");
            return CE_FAILURE;
        }
    } else {
        if (epoll_ctl(poller_fd, EPOLL_CTL_MOD, fd, evt) != 0) {
            printf("ERROR: Failed to modify to add event in epoll_ctl\n");
            return CE_FAILURE;
        }
    }

    polling_cnt++;
    return CE_SUCCESS;
}

int ce_poller_remove(int fd, int event)
{
    ce_fd_assoc *fd_assoc = fd_assoc_arr[fd];
    int should_del = FALSE;
    struct epoll_event *evt = NULL;

    if (fd_assoc == NULL) {
        printf("ERROR: event is not added in polling, could not remove\n");
        return CE_FAILURE;
    }

    if (event == CE_READ) {
        fd_assoc->rd_crtn = CE_DUMMY_COROUTINE_ID;
        evt = &(fd_assoc->rd_evt);
    } else if (event == CE_WRITE) {
        fd_assoc->wt_crtn = CE_DUMMY_COROUTINE_ID;
        evt = &(fd_assoc->wt_evt);
    }

    if (fd_assoc->rd_crtn == CE_DUMMY_COROUTINE_ID
        && fd_assoc->wt_crtn == CE_DUMMY_COROUTINE_ID) {
        should_del = TRUE;
    }
    evt->events = (fd_assoc->rd_crtn != CE_DUMMY_COROUTINE_ID ? EPOLLIN : 0)
                | (fd_assoc->wt_crtn != CE_DUMMY_COROUTINE_ID ? EPOLLOUT : 0);
    if (should_del) {
        if (epoll_ctl(poller_fd, EPOLL_CTL_DEL, fd, evt) != 0) {
            printf("ERROR: Failed to delete event in epoll_ctl\n");
            return CE_FAILURE;
        }
        fd_assoc_arr[fd] = NULL;
    } else {
        if (epoll_ctl(poller_fd, EPOLL_CTL_MOD, fd, evt) != 0) {
            printf("ERROR: Failed to modify to delete event in epoll_ctl\n");
            return CE_FAILURE;
        }
    }

    polling_cnt--;
    return CE_SUCCESS;
}

int ce_poller_poll(int timeout)
{
    size_t size_in_bytes = sizeof(struct epoll_event) * MAX_EPOLL_EVTS;
    if (poll_results == NULL) {
        poll_results = (struct epoll_event *)malloc(size_in_bytes);
        if (poll_results == NULL) {
            printf("ERROR: Failed to allocate space for poll_event\n");
            return CE_FAILURE;
        }
    }

    if (polling_cnt == 0) {
        return CE_SUCCESS;
    }

    ready_cnt = epoll_wait(poller_fd, poll_results, MAX_EPOLL_EVTS, timeout);
    if (ready_cnt == -1) {
        printf("ERROR: Failed to poll events\n");
        return CE_FAILURE;
    }

    return CE_SUCCESS;
}

int ce_poller_react()
{
    int i;
    int evt_flags;
    ce_fd_assoc *fd_assoc;

    for (i = 0; i < ready_cnt; i++) {
        evt_flags = poll_results[i].events;
        fd_assoc = (ce_fd_assoc *)poll_results[i].data.ptr;
        if (evt_flags & EPOLLIN
            && fd_assoc->rd_crtn != CE_DUMMY_COROUTINE_ID
            && ce_get_coroutine_status(fd_assoc->rd_crtn) == CE_COROUTINE_BLOCKED) {
            if (ce_set_coroutine_status(fd_assoc->rd_crtn,
                                        CE_COROUTINE_SUSPENDED) != CE_SUCCESS) {
                printf("ERROR: Failed to set reading coroutine status\n");
                return CE_FAILURE;
            }
        }
        if (evt_flags & EPOLLOUT
            && fd_assoc->wt_crtn != CE_DUMMY_COROUTINE_ID
            && ce_get_coroutine_status(fd_assoc->wt_crtn) == CE_COROUTINE_BLOCKED) {
            if (ce_set_coroutine_status(fd_assoc->wt_crtn,
                                        CE_COROUTINE_SUSPENDED) != CE_SUCCESS) {
                printf("ERROR: Failed to set writing coroutine status\n");
                return CE_FAILURE;
            }
        }
    }

    return CE_SUCCESS;
}
