CFLAGS+=-O2
CFLAGS+=-ggdb3
CFLAGS+=-Wall
CFLAGS+=-Wextra
CFLAGS+=-Wno-unused-parameter
CFLAGS+=-D_FORTIFY_SOURCE=2
CFLAGS+=-I..
#CFLAGS+=../ezgrpc.c

LDFLAGS+=-lnghttp2
LDFLAGS+=-lpthread

all: hello_world_server multi_server streaming_server_server

%: %.c ../ezgrpc.c
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

clean :
	rm hello_world_server multi_server streaming_server_server

