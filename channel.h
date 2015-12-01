#ifndef _COEVT_CHANNEL_H_
#define _COEVT_CHANNEL_H_

typedef struct ce_channel ce_channel;

ce_channel *ce_chan_create(int bufsize);
int ce_chan_send(ce_channel *chan, void *data);
int ce_chan_recv(ce_channel *chan, void *data);
void ce_chan_destroy(ce_channel **chan_ptr);

#endif
