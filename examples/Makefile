all: hello_world_server multi_server

hello_world_server : hello_world_server.c ../ezgrpc.c
	gcc $^ -I.. -ggdb3 -lnghttp2 -lpthread -o $@

multi_server : multi_server.c ../ezgrpc.c
	gcc $^ -I.. -ggdb3 -lnghttp2 -lpthread -o $@

clean :
	rm hello_world_server multi_server

