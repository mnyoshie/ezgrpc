/* the author disclaims copyright to this example source code
 * and releases it into the public domain
 */

#include "ezgrpc.h"

int whatever_service1(ezgrpc_message_t *req, ezgrpc_message_t **res, void *userdata){
  ezgrpc_message_t *msg_tail = NULL;
  ezgrpc_message_t **msg_head = &msg_tail;
  /* Let's send 10 messages as a reply */
  for (int msg = 0; msg < 10; msg++) {
    ezgrpc_message_t *msg = calloc(1, sizeof(ezgrpc_message_t));
    msg->is_compressed = 0;
    msg->data_len = 3;
    msg->data = malloc(3);
    /* protobuf serialized message */
    msg->data[0] = 0x08;
    msg->data[1] = 0x96;
    msg->data[2] = 0x02;
    msg->next = NULL;

    *msg_head = msg;
    msg_head = &msg->next;
  }

  *res = msg_tail;
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

  int flags = EZGRPC_SERVICE_FLAG_SERVER_STREAMING | EZGRPC_SERVICE_FLAG_EDGET;
  ezgrpc_server_add_service(server_handle, "/test.yourAPI/whatever_service1", whatever_service1, NULL, NULL, flags);
  ezgrpc_server_add_service(server_handle, "/test.yourAPI/another_service2", another_service2, NULL, NULL, 0);

  ezgrpc_server_set_ipv4_bind_port(server_handle, 19009);
  ezgrpc_server_set_shutdownfd(server_handle, pfd[0]);

  /* when a SIGINT/SIGTERM is received. this should return */
  ezgrpc_server_start(server_handle);

  ezgrpc_server_free(server_handle);

  return 0;
}
