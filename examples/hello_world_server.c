/* the author disclaims copyright to this example source code
 * and releases it into the public domain
 */

#include "ezgrpc.h"


void *handler(void *userdata) {
  /* userdata = (void**){(sigset_t*)signal_mask, (int*)shutdownfd};
   */
  ezhandler_arg *ezarg = userdata;
  sigset_t *signal_mask = ezarg->signal_mask;
  int shutdownfd = ezarg->shutdownfd;

  int sig, res;
  while(1) {
    res = sigwait(signal_mask, &sig);
    if (res != 0)
      assert(0);
    if (sig & (SIGINT | SIGTERM)) {
      write(1, "SIGINT/SIGTERM received!\n", 25);
      if (write(shutdownfd, "shutdown", 8) < 0)
        perror("write");
    } else
      write(2, "unknown signal\n", 15);
  }
}

int whatever_service1(ezvec_t req, ezvec_t *res, void *userdata){
  printf("called service1. received %zu bytes\n", req.data_len);
  res->data_len = 3;
  res->data = malloc(3);

  /* protobuf serialized message */
  res->data[0] = 0x08;
  res->data[1] = 0x96;
  res->data[2] = 0x02;
  //sleep(2);
  return 0;
}

int another_service2(ezvec_t req, ezvec_t *res, void *userdata){
  printf("called service2\n");
  return 0;
}

int main(){

  int pfd[2];
  if (pipe(pfd))
    assert(0);
  
  sigset_t sig_mask;
  ezhandler_arg ezarg = {&sig_mask, pfd[1]};
  if (ezgrpc_init(handler, &ezarg)) {
    fprintf(stderr, "fatal: couldn't init ezgrpc\n");
    return 1;
  }


  EZGRPCServer *server_handle = ezgrpc_server_init();
  assert(server_handle != NULL);

  ezgrpc_server_add_service(server_handle, "/test.yourAPI/whatever_service1", whatever_service1);
  ezgrpc_server_add_service(server_handle, "/test.yourAPI/another_service2", another_service2);

  ezgrpc_server_set_listen_port(server_handle, 19009);
  ezgrpc_server_set_shutdownfd(server_handle, pfd[0]);

  /* when a SIGINT/SIGTERM is received. this should return */
  ezgrpc_server_start(server_handle);

  ezgrpc_server_free(server_handle);

  return 0;
}
