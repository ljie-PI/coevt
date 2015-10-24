#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

#include "coevt.h"

#define CHAN_SIZE 1024
#define WORKER_NUM 128

typedef struct fd_arg {
    int fd;
} fd_arg;

typedef struct channel { // TODO: implement mechanism like golang channels to replace this
    int first;
    int last;
    int cap;
    int is_empty;
    int eles[CHAN_SIZE];
} channel;

static channel *chan;
static void init_channal()
{
    chan = (channel *)malloc(sizeof(channel));
    memset(chan, -1, sizeof(channel));
    chan->first = 0;
    chan->last = 0;
    chan->is_empty = 1;
    chan->cap = CHAN_SIZE;
}

static int put(int ele)
{
    if (chan->last == chan->first && chan->is_empty == 0) {
        printf("WARNING: channel is full, will discard a request\n");
        return -1;
    }
    chan->is_empty = 0;
    chan->eles[chan->last] = ele;
    chan->last = (chan->last + 1) % chan->cap;
    return 0;
}

static int take()
{
    int ret;
    if (chan->is_empty) {
        return -1;
    }
    ret = chan->eles[chan->first];
    chan->first = (chan->first + 1) % chan->cap;
    if (chan->first == chan->last) {
        chan->is_empty = 1;
    }
    return ret;
}

void process_io(void *arg)
{
    while (1) {
        int cli_fd;
        char rd_buf[1024];
        char wt_buf[1024 + 128];
        int bytes;

        cli_fd = take();
        if (cli_fd == -1) {
            ce_yield();
            continue;
        }
        while (1) {
            bytes = ce_read(cli_fd, rd_buf, sizeof(rd_buf));
            if (bytes <= 0) {
                printf("ERROR: Failed to read data, will close socket %d\n", cli_fd);
                ce_close(cli_fd);
                break;
            }
            sprintf(wt_buf, "echo from server(coroutine id: %d):\n%s\n",
                    ce_cur_task(), rd_buf);
            ce_write(cli_fd, wt_buf, strlen(wt_buf) + 1);
        }
    }
}

void process_request(void *arg)
{
    fd_arg *p_arg = (fd_arg *)arg;
    fd_arg cli_fd_t = { -1 };
    struct sockaddr_in addr;

    socklen_t len = sizeof(addr);
    memset(&addr, 0, sizeof(addr));

    while (1) {
        cli_fd_t.fd = accept(p_arg->fd, (struct sockaddr *)&addr, &len);
        if (cli_fd_t.fd != -1) {
            printf("INFO: accept connection, return fd %d\n", cli_fd_t.fd);
            put(cli_fd_t.fd);
        }
        ce_yield();
    }
}

int main()
{
    int sock_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
    fd_arg fd_t = { sock_fd };
    struct sockaddr_in addr;
    int w_id;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(3000);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        printf("ERROR: Failed to bind address for socket\n");
        if (errno == EADDRINUSE) {
            printf("ERROR: The port is already in use\n");
        }
        close(sock_fd);
        return -1;
    }
    if (listen(sock_fd, 1024) != 0) {
        printf("ERROR: Failed to listen to port\n");
        close(sock_fd);
        return -1;
    }

    init_channal();

    for (w_id = 0; w_id < WORKER_NUM; w_id++) {
        ce_task(process_io, NULL);
    }
    ce_task(process_request, &fd_t);
    ce_run();

    return 0;
}
