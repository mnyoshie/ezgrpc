/* the author disclaims copyright to this example source code
 * and releases it into the public domain
 */

#include "ezgrpc.h"

int whatever_service1(ezgrpc_message_t *req, ezgrpc_message_t **res, void *userdata){
  ezgrpc_message_t *msg = calloc(1, sizeof(ezgrpc_message_t));
  printf("called service1. received %u bytes\n", req->data_len);

  msg->is_compressed = 0;
  msg->data_len = 3;
  msg->data = malloc(3);
  /* protobuf serialized message */
  msg->data[0] = 0x08;
  msg->data[1] = 0x96;
  msg->data[2] = 0x02;
  msg->next = NULL;

  *res = msg;
  //sleep(2);
  return 0;
}

int another_service2(ezgrpc_message_t *req, ezgrpc_message_t **res, void *userdata){
  printf("called service2\n");
  return 0;
}

int main(){

  int pfd[2];
  if (pipe(pfd))
    assert(0);
  
  sigset_t sig_mask;
  ezhandler_arg ezarg = {&sig_mask, pfd[1]};
  if (ezgrpc_init(ezserver_signal_handler, &ezarg)) {
    fprintf(stderr, "fatal: couldn't init ezgrpc\n");
    return 1;
  }

  EZGRPCServer *server_handle = ezgrpc_server_init();
  assert(server_handle != NULL);

  ezgrpc_server_add_service(server_handle, "/test.yourAPI/whatever_service1", 0, whatever_service1);
  ezgrpc_server_add_service(server_handle, "/test.yourAPI/another_service2", 0, another_service2);

  ezgrpc_server_set_listen_port(server_handle, 19009);
  ezgrpc_server_set_shutdownfd(server_handle, pfd[0]);

  /* when a SIGINT/SIGTERM is received. this should return */
  ezgrpc_server_start(server_handle);

  ezgrpc_server_free(server_handle);

  return 0;
}
