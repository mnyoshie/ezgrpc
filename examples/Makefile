CFLAGS+=-O2
CFLAGS+=-ggdb3
CFLAGS+=-Wall
CFLAGS+=-Wextra
CFLAGS+=-Wno-unused-parameter
CFLAGS+=-D_FORTIFY_SOURCE=2
CFLAGS+=-Wno-missing-field-initializers
CFLAGS+=-I..
#CFLAGS+=../ezgrpc.c

LDFLAGS+=-lnghttp2
LDFLAGS+=-lpthread
ifeq ($(OS),Windows_NT)
  LDFLAGS += -lWs2_32
endif

all: hello_world_server.bin multi_server.bin streaming_server_server.bin

%.bin: %.o ../ezgrpc.o
	$(Q)$(CC) $^ $(LDFLAGS) -o $@

%.o : %.c
	$(CC) -c $(CFLAGS) $< -o $@

clean :
	rm hello_world_server.bin\
		multi_server.bin\
		streaming_server_server.bin\
		../ezgrpc.o

