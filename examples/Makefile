all: hello_world_server multi_server

hello_world_server : hello_world_server.c
	gcc $^ ../ezgrpc.c -I.. -lnghttp2 -lpthread -o $@

multi_server : multi_server.c
	gcc $^ ../ezgrpc.c -I.. -lnghttp2 -lpthread -o $@

clean :
	rm hello_world_server multi_server

