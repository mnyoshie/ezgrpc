/* the author disclaims copyright to this example source code
 * and releases it into the public domain.
 *
 * COMES WITH NO LIABILITY AND/OR WARRANTY!
 *
 * A multi server running on different ports
 */

#include "ezgrpc.h"

volatile _Atomic int running_servers = 0;

void *handler(void *userdata) {
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
      printf("unknown signal\n");
  }
}

void *start_server(void *userdata) {
  running_servers++;
  EZGRPCServer *server_handle = userdata;
  ezgrpc_server_start(server_handle);
  ezgrpc_server_free(server_handle);
  running_servers--;

  return userdata;
}

int whatever_service1(ezvec_t req, ezvec_t *res, void *userdata){
  printf("called service1. received %zu bytes\n", req.data_len);
  res->data_len = 3;
  res->data = malloc(3);

  /* protobuf serialized message */
  res->data[0] = 0x08;
  res->data[1] = 0x96;
  res->data[2] = 0x02;
//  sleep(1);
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

  pthread_t sthread1, sthread2, sthread3;

  EZGRPCServer *server_handle1 = ezgrpc_server_init();
  assert(server_handle1 != NULL);
  EZGRPCServer *server_handle2 = ezgrpc_server_init();
  assert(server_handle2 != NULL);
  EZGRPCServer *server_handle3 = ezgrpc_server_init();
  assert(server_handle3 != NULL);

  /* NOTE: check the return values */
  ezgrpc_server_add_service(server_handle1, "/test.yourAPI/whatever_service1", whatever_service1);
  ezgrpc_server_add_service(server_handle1, "/test.yourAPI/another_service2", another_service2);
  ezgrpc_server_set_listen_port(server_handle1, 19009);
  ezgrpc_server_set_shutdownfd(server_handle1, pfd[0]);

  ezgrpc_server_add_service(server_handle2, "/test.yourAPI/whatever_service1", whatever_service1);
  ezgrpc_server_add_service(server_handle2, "/test.yourAPI/another_service2", another_service2);
  ezgrpc_server_set_listen_port(server_handle2, 19010);
  ezgrpc_server_set_shutdownfd(server_handle2, pfd[0]);

  ezgrpc_server_add_service(server_handle3, "/test.yourAPI/whatever_service1", whatever_service1);
  ezgrpc_server_add_service(server_handle3, "/test.yourAPI/another_service2", another_service2);
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
