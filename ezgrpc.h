/* ezgrpc.c - A (crude) gRPC server in C. */

#ifndef EZGRPC_H
#define EZGRPC_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <assert.h>
#include <byteswap.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
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

#include <nghttp2/nghttp2.h>

#define EZGRPC_MAX_SESSIONS 32

#define EZGRPC_SERVICE_FLAG_NONE (0)

/* rpc service(stream req) returns(res); */
#define EZGRPC_SERVICE_FLAG_CLIENT_STREAMING (1 << 0)

/* rpc service(req) returns(stream res); */
#define EZGRPC_SERVICE_FLAG_SERVER_STREAMING (1 << 1)

/* in `edge triggered` (EDGET), the service starts sending replies
 * even before an END_STREAM is received. If this is not
 * set, the service start sending replies after an END_STREAM
 * is received. In this case, we call it `end triggered`.
 */
#define EZGRPC_SERVICE_FLAG_EDGET (1 << 4)

/* An END_STREAM is a HTTP2 flag in the HTTP2 frame header. */


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

typedef struct ezvec_t ezvec_t;
typedef struct ezlvec_t ezlvec_t;
typedef struct ezgrpc_message_t ezgrpc_message_t;

typedef int (*ezgrpc_server_service_callback)(ezgrpc_message_t *req,
                                              ezgrpc_message_t **res,
                                              void *userdata);

typedef char i8;
typedef uint8_t u8;
typedef int8_t s8;

typedef int16_t i16;
typedef uint16_t u16;

typedef int32_t i32;
typedef uint32_t u32;

typedef int64_t i64;
typedef uint64_t u64;

struct ezvec_t {
  size_t data_len;
  u8 *data;
};

/* grpc length-prefixed message */
struct ezgrpc_message_t {
  u8 is_compressed;
  u32 data_len;
  i8 *data;
  ezgrpc_message_t *next;
};

typedef struct ezhandler_arg ezhandler_arg;
struct ezhandler_arg {
  sigset_t *signal_mask;
  int shutdownfd;
};

/* stores the values of a SETTINGS frame */
typedef struct ezgrpc_settingsf_t ezgrpc_settingsf_t;
struct ezgrpc_settingsf_t {
  u32 header_table_size;
  u32 enable_push;
  u32 max_concurrent_streams;
  u32 flow_control;
};

/* a stream is created when a HEADERS frame received. A HEADERS frame
 * contains atleast the following, or a representation of it: */
typedef struct ezgrpc_stream_t ezgrpc_stream_t;
struct ezgrpc_stream_t {
  u32 stream_id;

  /* the time the stream is received by the server in
   * unix epoch */
  u64 time;

  /* a pointer to the session this stream belongs */
  ezgrpc_session_t *ezsession;

  i8 is_method_post:1;
  i8 is_scheme_http:1;
  /* just a bool. if `te` is a trailer, or `te` does exists. */
  i8 is_te_trailers:1;
  /* just a bool. if content type is application/grpc* */
  i8 is_content_grpc:1;

  /* stores `:path` */
  i8 *service_path;

  /* a function pointer to the service. to be initialized when
   * service_path do exist and is valid
   */
  //ezgrpc_server_service_callback svcall;

  /* a pointer to the available service in EZGRPCServer.sv */
  ezgrpc_service_t *service;

  /* the thread. make sure to kill it when closing the session */
  pthread_t sthread;

  /* a pointer to ezsession.is_shutdown */
  u8 volatile *is_shutdown;

  /* is_cancel is set to 1 to notify the stream
   * has been cancelled by the client.
   * this actually does nothing. cancelling is discouraged!!
   */
  volatile u8 is_cancel;

  ezvec_t recv_data;

  /* to be filled when recv_data is processed */
  /* number of bytes sent */
  size_t sent_data_len;
  ezvec_t send_data;

  /* to be filled and send when the recv_data is processed and
   * the service is executed in another thread. this may also
   * be filled when all `is_*` is invalid/valid */
  ezgrpc_status_code_t grpc_status;

  ezgrpc_stream_t *next;
};

struct ezgrpc_service_t {
  i8 *service_path;

  /* incoming message deserializer */
  int (*in_deserializer)(ezvec_t *in, ezvec_t *out);

  /* outgoing message serializer */
  int (*out_serializer)(ezvec_t *in, ezvec_t *out);

  /* available flags:
   *
   *   EZGRPC_SERVICE_FLAG_CLIENT_STREAMING
   *   EZGRPC_SERVICE_FLAG_SERVER_STREAMING
   *   EZGRPC_SERVICE_FLAG_NONE
   *
   */
  i8 service_flags;

  ezgrpc_server_service_callback service_callback;
};

struct ezgrpc_services_t {
  size_t nb_services;
  ezgrpc_service_t *services;
};

struct ezgrpc_session_t {
  int sockfd, domain;

  nghttp2_session *ngsession;

  /* an ASCII string */
  i8 client_addr[64];
  u16 client_port;

  i8 *server_addr;
  u16 *server_port;


  pthread_mutex_t ngmutex;
  pthread_t sthread;

  /* is_shutdown is set to 1 to notify the socket
   * has been closed and we need to shutdown the session
   */
  volatile u8 is_shutdown;

  /* a copy of EZGRPCServer.shutdownfd */
  int shutdownfd;

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
  size_t volatile *volatile nb_sessions;

  volatile ssize_t nb_open_streams;
  volatile _Atomic ssize_t nb_running_callbacks;
  /* the streams in a linked lists. allocated when we are
   * about to receive a HEADERS frame */
  ezgrpc_stream_t *st;

  u8 is_used;
};

struct ezgrpc_sessions_t {
  volatile size_t nb_sessions;
  ezgrpc_session_t sessions[EZGRPC_MAX_SESSIONS];
};

/* This struct is private. Please use the auxilliary functions below it */
typedef struct EZGRPCServer EZGRPCServer;
struct EZGRPCServer {
  i8 is_ipv4_enabled:1;
  i8 is_ipv6_enabled:1;

  u16 ipv4_port;
  u16 ipv6_port;

  /* ASCII strings which represent where address to bind to */
  i8 ipv4_addr[16];
  i8 ipv6_addr[64];

  /* listening socket */
  int ipv4_sockfd;
  int ipv6_sockfd;

  /* pipe(fd[2])
   * pfd[0] is for reading.
   * pfd[1] is for writing. */

  /* we pollin this descriptor.
   * if there is movement, shutdown the server.
   * shutdownfd = pfd[0]
   */
  int shutdownfd;

  /* default server settings to be sent */
  ezgrpc_settingsf_t settings;

  /* PRIVATE: */

  /* available services */
  ezgrpc_services_t sv;

  /* running sessions */
  ezgrpc_sessions_t ng;
};

#ifdef __cplusplus
extern "C" {
#endif

/* ezgrpc builtin signal handler. You can either use this by passing it
 * to ezgrpc_init, or build one of your own.
 * */
void *ezserver_signal_handler(void *userdata);

int ezgrpc_init(void *(*signal_handler)(void *), ezhandler_arg *ezarg);

EZGRPCServer *ezgrpc_server_init(void);

int ezgrpc_server_set_ipv4_bind_port(EZGRPCServer *server_handle, u16 port);
int ezgrpc_server_set_ipv6_bind_port(EZGRPCServer *server_handle, u16 port);

int ezgrpc_server_set_ipv4_bind_addr(EZGRPCServer *server_handle, const i8 *addr);
int ezgrpc_server_set_ipv6_bind_addr(EZGRPCServer *server_handle, const i8 *addr);

int ezgrpc_server_enable_ipv4(EZGRPCServer *server_handle, char n);
int ezgrpc_server_enable_ipv6(EZGRPCServer *server_handle, char n);

int ezgrpc_server_set_shutdownfd(EZGRPCServer *server_handle, int shutdownfd);

// int ezgrpc_server_set_logging_fd(EZGRPCServer *server_handle, int fd);

int ezgrpc_server_add_service(EZGRPCServer *server_handle, i8 *service_path,
                              ezgrpc_server_service_callback service_callback,
                              int (*in_deserializer)(ezvec_t *in, ezvec_t *out),
                              int (*out_serializer)(ezvec_t *in, ezvec_t *out),
                              i8 service_flags);

int ezgrpc_server_start(EZGRPCServer *server_handle);

void ezgrpc_server_free(EZGRPCServer *server_handle);

#ifdef __cplusplus
}
#endif

#endif /* EZGRPC_H */
