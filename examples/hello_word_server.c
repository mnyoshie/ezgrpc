#include "ezgrpc.h"

int whatever_service1(void *req, void *res, void *userdata){
  printf("called service1\n");
  return 0;
}

int another_service2(void *req, void *res, void *userdata){
  printf("called service2\n");
  return 0;
}

int main(){
  EZGRPCServer *server_handle = ezgrpc_server_init();
  assert(server_handle != NULL);

  ezgrpc_server_add_service(server_handle, "/test.yourAPI/whatever_service1", whatever_service1);
  ezgrpc_server_add_service(server_handle, "/test.yourAPI/another_service2", another_service2);

  ezgrpc_server_start(server_handle);

  return 0;
}
