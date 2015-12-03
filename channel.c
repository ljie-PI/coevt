#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "defs.h"
#include "coroutine.h"
#include "channel.h"

#define CE_SEND 1
#define CE_RECV 2

typedef struct ce_blkd {
    int crtn_id;
    void **data_ptr;
    struct ce_blkd *next;
} ce_blkd;

typedef struct ce_blkd_q {
    ce_blkd *head;
    ce_blkd *tail;
    int size;
} ce_blkd_q;

struct ce_channel {
    int cap;
    int head;
    int size;
    ce_blkd_q send_q;
    ce_blkd_q recv_q;
    void **buf;
};

ce_channel *ce_chan_create(int bufsize)
{
    size_t size_in_bytes;
    ce_channel *chan;

    size_in_bytes = sizeof(ce_channel);
    chan = (ce_channel *)malloc(size_in_bytes);
    if (chan == NULL) {
        printf("ERROR: Failed to allocate space for ce_channel\n");
        return NULL;
    }
    memset(chan, 0, size_in_bytes);
    chan->cap = bufsize;
    if (bufsize > 0) {
        size_in_bytes = sizeof(void *) * bufsize;
        chan->buf = (void **)malloc(size_in_bytes);
        if (chan->buf == NULL) {
            printf("ERROR: Failed to allocate space for buffer of channel\n");
            free(chan);
            return NULL;
        }
    }

    return chan;
}

static void enqueue(ce_blkd_q *q, ce_blkd *ele)
{
    if (q->size == 0) {
        q->head = q->tail = ele;
    } else {
        q->tail->next = ele;
        q->tail = ele;
    }
    ++q->size;
}

static ce_blkd *peek(ce_blkd_q *q)
{
    return q->head;
}

static ce_blkd *dequeue(ce_blkd_q *q)
{
    ce_blkd *ele;
    if (q->size == 0) {
        printf("ERROR: Failed to dequeue, queue is empty\n");
        return NULL;
    }
    ele = q->head;
    q->head = ele->next;
    ele->next = NULL;
    --q->size;
    return ele;
}

static void unblock_task(ce_channel *chan, int op_type)
{
    ce_blkd_q *q;
    int crtn_id;
    ce_blkd *ele;

    if (op_type == CE_SEND) {
        q = &(chan->send_q);
    } else {
        q = &(chan->recv_q);
    }
    if (q->size == 0) {
        printf("ERROR: Could not unblock a task of op_type %d\n", op_type);
        return;
    }
    ele = dequeue(q);
    crtn_id = ele->crtn_id;
    free(ele);
    ce_set_coroutine_status(crtn_id, CE_COROUTINE_SUSPENDED);
}


static void unblock_one_recver(ce_channel *chan)
{
    unblock_task(chan, CE_RECV);
}

static void send_to_recver(ce_channel *chan, void *data)
{
    ce_blkd *recver = peek(&(chan->recv_q));
    if (recver == NULL) {
        printf("ERROR: There's no receiver, so could not send data to any receiver\n");
        return;
    }
    *(recver->data_ptr) = data;
}

static void send_to_buffer(ce_channel *chan, void *data)
{
    int offset;

    if (chan->size == chan->cap) {
        printf("ERROR: Buffer of channel is full, could not send data to buffer\n");
        return;
    }

    offset = (chan->head + chan->size) % chan->cap;
    *(chan->buf + offset) = data;
    ++chan->size;
}

static void blkd_wait(ce_channel *chan, void **data_ptr, int op_type)
{
    ce_blkd *ele = (ce_blkd *)malloc(sizeof(ce_blkd));
    if (ele == NULL) {
        printf("ERROR: Could not allocate space for a ce_blkd\n");
        return;
    }
    ele->crtn_id = ce_cur_coroutine();
    ele->data_ptr = data_ptr;
    ele->next = NULL;
    if (op_type == CE_SEND) {
        enqueue(&(chan->send_q), ele);
    } else {
        enqueue(&(chan->recv_q), ele);
    }
    ce_coroutine_block();
}

static void sender_wait(ce_channel *chan)
{
    blkd_wait(chan, NULL, CE_SEND);
}

int ce_chan_send(ce_channel *chan, void *data)
{
    while (TRUE) {

        // if channel is destroyed, return CE_FAILURE
        if (chan == NULL) {
            return CE_FAILURE;
        }

        // if there are recevers waiting,
        // send to one directly and unblock it
        if (chan->recv_q.size > 0) {
            send_to_recver(chan, data);
            unblock_one_recver(chan);
            return CE_SUCCESS;
        }

        // if channel is buffered and the buffer is not full,
        // send data to buffer
        if (chan->cap > 0 && chan->size < chan->cap) {
            send_to_buffer(chan, data);
            return CE_SUCCESS;
        }

        // if the channel is unbuffered or buffer is full,
        // block the current coroutine and add to senders waiting queue
        sender_wait(chan);
    }
}

static void unblock_one_sender(ce_channel *chan)
{
    unblock_task(chan, CE_SEND);
}

static void *recv_from_buffer(ce_channel *chan)
{
    int offset;

    if (chan->cap == 0) {
        printf("ERROR: Channel is unbeffered, could not receive data from buffer\n");
    }
    if (chan->size == 0) {
        printf("ERROR: Channel is empty, could not receive data from buffer\n");
    }

    offset = chan->head;
    chan->head = (chan->head + 1) % chan->cap;
    return *(chan->buf + offset);
}

static void recver_wait(ce_channel *chan, void **data_ptr)
{
    blkd_wait(chan, data_ptr, CE_SEND);
}

int ce_chan_recv(ce_channel *chan, void *data)
{
    // if channel is destroyed, return FAILURE
    if (chan == NULL) {
        return CE_FAILURE;
    }

    // if channel is buffered and buffer is not empty,
    // receive data from buffer,
    // and unblock a sender if there is.
    if (chan->cap > 0 && chan->size > 0) {
        data = recv_from_buffer(chan);
        if (chan->send_q.size > 0) {
            unblock_one_sender(chan);
        }
        return CE_SUCCESS;
    }

    // else block current coroutine until a sender passes data and unblock it,
    // but before that should first unblock a sender if there is,
    // otherwise waiting senders will be blocked forever.
    if (chan->send_q.size > 0) {
        unblock_one_sender(chan);
    }
    recver_wait(chan, &data);
    // check again whether channel is destroyed
    if (chan == NULL) {
        return CE_FAILURE;
    }
    return CE_SUCCESS;
}

void ce_chan_destroy(ce_channel **chan_ptr)
{
    if (*chan_ptr == NULL) {
        return;
    }
    // release buf
    if ((*chan_ptr)->cap > 0) {
        free((*chan_ptr)->buf);
    }
    // unblock all pending coroutines,
    // when they resume, they will find chan is null and return CE_FAILURE
    while ((*chan_ptr)->recv_q.size > 0) {
        unblock_one_recver(*chan_ptr);
    }
    while ((*chan_ptr)->send_q.size > 0) {
        unblock_one_sender(*chan_ptr);
    }
    free(*chan_ptr);

    // this is important,
    // because it set the channel pointer to NULL,
    // so that pending tasks can return CE_FAILURE instead of being blocked forever
    *chan_ptr = NULL;
}

int ce_chan_sendl(ce_channel *chan, long n)
{
    return ce_chan_send(chan, (void *)n);
}
int ce_chan_recvl(ce_channel *chan, long *p)
{
    void *n = NULL;
    int ret = ce_chan_recv(chan, n);
    *p = (long)n;
    return ret;
}
