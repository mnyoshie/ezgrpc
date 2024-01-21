all: hello_world_server multi_server streaming_server_server

%: %.c
	gcc $< ../ezgrpc.c -I.. -ggdb3 -lnghttp2 -lpthread -o $@

clean :
	rm hello_world_server multi_server streaming_server_server

