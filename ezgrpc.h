/* ezgrpc.c - A (crude) gRPC server in C. */

#ifndef EZGRPC_H
#define EZGRPC_H

#include <assert.h>
#include <byteswap.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netdb.h>

#include <arpa/inet.h>

#include <pthread.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <sys/socket.h>
#include <sys/types.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/util.h>

#include <nghttp2/nghttp2.h>

#define EZGRPC_MAX_SESSIONS 3


enum ezgrpc_status_code_t {
  /* https://github.com/grpc/grpc/tree/master/include/grpcpp/impl/codegen */
  EZGRPC_GRPC_STATUS_OK = 0,
  EZGRPC_GRPC_STATUS_CANCELLED = 1,
  EZGRPC_GRPC_STATUS_UNKNOWN = 2,
  EZGRPC_GRPC_STATUS_INVALID_ARGUMENT = 3,
  EZGRPC_GRPC_STATUS_DEADLINE_EXCEEDED = 4,
  EZGRPC_GRPC_STATUS_NOT_FOUND = 5,
  EZGRPC_GRPC_STATUS_ALREADY_EXISTS = 6,
  EZGRPC_GRPC_STATUS_PERMISSION_DENIED = 7,
  EZGRPC_GRPC_STATUS_UNAUTHENTICATED = 16,
  EZGRPC_GRPC_STATUS_RESOURCE_EXHAUSTED = 8,
  EZGRPC_GRPC_STATUS_FAILED_PRECONDITION = 9,
  EZGRPC_GRPC_STATUS_OUT_OF_RANGE = 11,
  EZGRPC_GRPC_STATUS_UNIMPLEMENTED = 12,
  EZGRPC_GRPC_STATUS_INTERNAL = 13,
  EZGRPC_GRPC_STATUS_UNAVAILABLE = 14,
  EZGRPC_GRPC_STATUS_DATA_LOSS = 15,
  EZGRPC_GRPC_STATUS_NULL = -1
};
typedef enum ezgrpc_status_code_t ezgrpc_status_code_t; 

typedef struct ezgrpc_service_t ezgrpc_service_t;
typedef struct ezgrpc_services_t ezgrpc_services_t;

typedef struct ezgrpc_session_t ezgrpc_session_t;
typedef struct ezgrpc_sessions_t ezgrpc_sessions_t;

typedef int (*ezgrpc_server_service_callback)(void *req, void *res,
                                              void *userdata);

/* stores the values of a SETTINGS frame */
typedef struct ezgrpc_settingsf_t ezgrpc_settingsf_t;
struct ezgrpc_settingsf_t {
  uint32_t header_table_size;
  uint32_t enable_push;
  uint32_t max_concurrent_streams;
  uint32_t flow_control;
};

/* a stream is created when a HEADERS frame received. A HEADERS frame
 * contains atleast the following, or a representation of it: */
typedef struct ezgrpc_stream_t ezgrpc_stream_t;
struct ezgrpc_stream_t {
  uint32_t stream_id;

  /* the time the stream is created by the server in
   * unix epoch */
  uint64_t time;

  /* a pointer to the session this stream belongs */
  ezgrpc_session_t *ezsession;

  char is_method_post;
  char is_scheme_http;
  /* just a bool. if `te` is a trailer, or `te` does exists. */
  char is_te_trailers;
  /* just a bool. if content type is application/grpc */
  char is_content_grpc;

  /* stores `:path:` */
  char *service_path;

  /* a function pointer to the service. to be initialized when
   * service_path do exist and is valid
   */
  ezgrpc_server_service_callback svcall;

  /* the thread. make sure to kill it when closing the session */
  pthread_t sthread;

  /* a pointer to ezsession.is_shutdown */
  int volatile* is_shutdown;

  /* is_cancel is set to 1 to notify the stream
   * has been cancelled by the client.
   */
  volatile int is_cancel;

  size_t recv_data_len;
  uint8_t *recv_data;

  /* to be filled when recv_data is processed */
  size_t send_data_len;
  uint8_t *send_data;

  /* to be filled and send when the recv_data is processed and
   * the service is executed in another thread. this may also
   * be filled when all `is_*` is invalid */
  ezgrpc_status_code_t grpc_status;

  ezgrpc_stream_t *next;
};

struct ezgrpc_service_t {
  char *service_path;
  ezgrpc_server_service_callback service_callback;
};

struct ezgrpc_services_t {
  size_t nb_services;
  ezgrpc_service_t *services;
};

struct ezgrpc_session_t {
  pthread_mutex_t ngmutex;
  nghttp2_session *ngsession;

  /* is_shutdown is set to 1 to notify the socket
   * has been closed and we need to shutdown the session
   */
  volatile int is_shutdown;

  /* settings requested by the client. to be set when a SETTINGS
   * frame is received.
   */
  ezgrpc_settingsf_t csettings;

  /* A pointer to the settings in EZGRPCServer.settings
   */
  ezgrpc_settingsf_t *ssettings;

  /* A pointer to the available services in
   * EZGRPCServer.sv
   */
  ezgrpc_services_t *sv;

  /* A pointer to the number of running sessions in
   * EZGRPCServer.ng.nb_sessions. this is to decrement the number of session
   * when this session is closed;
   */
  size_t volatile* nb_sessions;

  volatile size_t nb_open_streams;
  /* the streams in a linked lists. allocated when we are
   * about to receive a HEADERS frame */
  ezgrpc_stream_t *st;

  struct bufferevent *bev;

  int is_used;
};

struct ezgrpc_sessions_t {
  volatile size_t nb_sessions;
  ezgrpc_session_t sessions[EZGRPC_MAX_SESSIONS];
};

typedef struct EZGRPCServer EZGRPCServer;
struct EZGRPCServer {
  uint16_t port;

  /* default server settings to be sent */
  ezgrpc_settingsf_t settings;

  /* available services */
  ezgrpc_services_t sv;

  /* running sessions */
  ezgrpc_sessions_t ng;

//  struct {
//    /* number of running sessions */
//    size_t nb_sessions;
//    ezgrpc_session_t sessions[EZGRPC_MAX_SESSIONS];
//  } ng;
};

EZGRPCServer *ezgrpc_server_init(void);

int ezgrpc_server_set_listen_port(EZGRPCServer *server_handle, uint16_t port);
// int ezgrpc_server_set_logging_fd(EZGRPCServer *server_handle, int fd);

int ezgrpc_server_add_service(EZGRPCServer *server_handle, char *service_path,
                              ezgrpc_server_service_callback service_callback);

int ezgrpc_server_start(EZGRPCServer *server_handle);

void ezgrpc_server_free(EZGRPCServer *server_handle);
#endif /* EZGRPC_H */
