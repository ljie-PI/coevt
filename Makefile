CC = gcc
CFLAGS += -Wall -fPIC -g -O2

SHARED_OPT = -shared
LIB_OBJS = coroutine.o poller.o channel.o coevt.o

all: libcoevt.so echo_server

libcoevt.so: $(LIB_OBJS)
	$(CC) $(CFLAGS) $(SHARED_OPT) -o $@ $(LIB_OBJS)

echo_server: echo_server.o
	$(CC) $(CFLAGS) -o $@ echo_server.o $(LIB_OBJS)

coevt.o: coevt.c defs.h poller.h coroutine.h coevt.h
	$(CC) $(CFLAGS) -c $< -o $@
channel.o: channel.c defs.h coroutine.h channel.h
	$(CC) $(CFLAGS) -c $< -o $@
poller.o: poller.c defs.h coroutine.h poller.h
	$(CC) $(CFLAGS) -c $< -o $@
coroutine.o: coroutine.c defs.h coroutine.h
	$(CC) $(CFLAGS) -c $< -o $@
echo_server.o: echo_server.c coroutine.h coevt.h
	$(CC) $(CFLAGS) -c $< -o $@


clean:
	rm *.o libcoevt.so echo_server
