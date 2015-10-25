# coevt
Asynchronous IO, without callbacks

The example `echo_server.c` shows how to use it. The functions with prefix `ce_` are provided by this library.

## TODO
1. Add UT
2. Add mechanism like golang channel to pass messages among coroutines
3. Add kqueue support in Mac OSX and FreeBSD
4. Add more examples to cover all APIs
