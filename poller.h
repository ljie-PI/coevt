#ifndef _COEVT_POLLER_H_
#define _COEVT_POLLER_H_

int ce_poller_init(int max_fd);
int ce_poller_initialized();
int ce_poller_lookup(int fd, int event);
int ce_poller_add(int fd, int event);
int ce_poller_remove(int fd, int event);
int ce_poller_poll(int timeout);
int ce_poller_react();

#endif
