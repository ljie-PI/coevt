/* 
 * echo server using channel to pass messages
 * */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

#include "channel.h"
#include "coevt.h"

#define CHAN_SIZE 1024
#define WORKER_NUM 128

void process_io(void *arg)
{
    ce_channel *chan = (ce_channel *)arg;
    while (1) {
        long cli_fd;
        char rd_buf[1024];
        char wt_buf[1024 + 128];
        int bytes;

        ce_chan_recvl(chan, &cli_fd);
        if (cli_fd == -1) {
            ce_yield();
            continue;
        }
        while (1) {
            bytes = ce_read(cli_fd, rd_buf, sizeof(rd_buf));
            if (bytes <= 0) {
                printf("ERROR: Failed to read data, will close socket %ld\n", cli_fd);
                ce_close(cli_fd);
                break;
            }
            rd_buf[bytes] = '\0';
            sprintf(wt_buf, "echo from server(coroutine id: %d):\n%s\n",
                    ce_cur_task(), rd_buf);
            ce_write(cli_fd, wt_buf, strlen(wt_buf) + 1);
        }
    }
}

void process_request(void *arg)
{
    ce_channel *chan = (ce_channel *)arg;
    long listen_fd;
    int cli_fd;
    struct sockaddr_in addr;

    ce_chan_recvl(chan, &listen_fd); // the 1st element sent into channel is the listening fd
    socklen_t len = sizeof(addr);
    memset(&addr, 0, sizeof(addr));

    while (1) {
        cli_fd = accept(listen_fd, (struct sockaddr *)&addr, &len);
        if (cli_fd != -1) {
            printf("INFO: accept connection, return fd %d\n", cli_fd);
            ce_chan_sendl(chan, cli_fd);
        }
        ce_yield();
    }
}

int main()
{
    int sock_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
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

    ce_channel *chan = ce_chan_create(CHAN_SIZE);
    for (w_id = 0; w_id < WORKER_NUM; w_id++) {
        ce_task(process_io, chan);
    }
    ce_task(process_request, chan);
    ce_chan_sendl(chan, sock_fd);
    ce_run();

    return 0;
}
