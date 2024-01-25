/* the author disclaims copyright to this example source code
 * and releases it into the public domain.
 *
 * COMES WITH NO LIABILITY AND/OR WARRANTY!
 *
 * A multi server running on different ports
 */

#include "ezgrpc.h"

volatile _Atomic int running_servers = 0;

void *start_server(void *userdata) {
  running_servers++;
  EZGRPCServer *server_handle = userdata;
  ezgrpc_server_start(server_handle);
  ezgrpc_server_free(server_handle);
  running_servers--;

  return userdata;
}

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
  
  /*
   * ezgrpc_init() must be executed before any other ez functions
   * are called
   *
   *
   * ezgrpc_init sets up 2 things:
   *
   *   (1) Sets up the ezserver_signal_handler() signal handler.
   *
   *   (2) Sets up so that any pthread_create after this won't
   *   received SIGINT/SIGTERM/SIGPIPE, making sure
   *   ezserver_signal_handler() only receives it.
   *
   * ezserver_signal_handler does mainly 2 things:
   *
   *   (1) When a SIGINT/SIGTERM is received, writes to pfd[1].
   *   all running servers polling pfd[0] will notice this
   *   and should return.
   *  
   *   (2) When a SIGPIPE is received, ignores it.
   *
   * NOTE: do not swap pfd[1] for pfd[0] and vice versa.
   */
  sigset_t sig_mask;
  ezhandler_arg ezarg = {&sig_mask, pfd[1]};
  if (ezgrpc_init(ezserver_signal_handler, &ezarg)) {
    fprintf(stderr, "fatal: couldn't init ezgrpc\n");
    return 1;
  }

  pthread_t sthread1, sthread2, sthread3;

  EZGRPCServer *server_handle1 = ezgrpc_server_init();
  assert(server_handle1 != NULL);
  EZGRPCServer *server_handle2 = ezgrpc_server_init();
  assert(server_handle2 != NULL);
  EZGRPCServer *server_handle3 = ezgrpc_server_init();
  assert(server_handle3 != NULL);

  /* NOTE: check the return values */
  ezgrpc_server_add_service(server_handle1, "/test.yourAPI/whatever_service1", whatever_service1, 0);
  ezgrpc_server_add_service(server_handle1, "/test.yourAPI/another_service2", another_service2, 0);
  ezgrpc_server_set_listen_port(server_handle1, 19009);
  ezgrpc_server_set_shutdownfd(server_handle1, pfd[0]);

  ezgrpc_server_add_service(server_handle2, "/test.yourAPI/whatever_service1", whatever_service1, 0);
  ezgrpc_server_add_service(server_handle2, "/test.yourAPI/another_service2", another_service2, 0);
  ezgrpc_server_set_listen_port(server_handle2, 19010);
  ezgrpc_server_set_shutdownfd(server_handle2, pfd[0]);

  ezgrpc_server_add_service(server_handle3, "/test.yourAPI/whatever_service1", whatever_service1, 0);
  ezgrpc_server_add_service(server_handle3, "/test.yourAPI/another_service2", another_service2, 0);
  ezgrpc_server_set_listen_port(server_handle3, 19011);
  ezgrpc_server_set_shutdownfd(server_handle3, pfd[0]);

  int res;
  res = pthread_create(&sthread1, NULL, start_server, server_handle1);
  if (res)
    printf("pthread_create %d\n", res);
  res = pthread_create(&sthread2, NULL, start_server, server_handle2); 
  if (res)
    printf("pthread_create %d\n", res);
  res = pthread_create(&sthread3, NULL, start_server, server_handle3);
  if (res)
    printf("pthread_create %d\n", res);

  /* sleep for a while. we're going to end up terminating the main thread
   * before the threads increment running_servers
   */
  sleep(2);
  while (running_servers) {;}

  close(pfd[0]);
  close(pfd[1]);

  return 0;
}
