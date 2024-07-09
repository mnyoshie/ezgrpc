/* ezgrpc.c - A (crude) gRPC server in C. */

#ifndef EZGRPC_H
#define EZGRPC_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdint.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <windows.h>
#else
#  include <signal.h>
#endif

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


typedef char i8;
typedef uint8_t u8;
typedef int8_t s8;

typedef int16_t i16;
typedef uint16_t u16;

typedef int32_t i32;
typedef uint32_t u32;

typedef int64_t i64;
typedef uint64_t u64;

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
typedef struct EZGRPCServer EZGRPCServer;

struct EZGRPCServer;

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
#ifdef _WIN32
#else
  sigset_t *signal_mask;
#endif
  int shutdownfd;
};

typedef int (*ezgrpc_server_service_callback)(ezgrpc_message_t *req,
                                              ezgrpc_message_t **res,
                                              void *userdata);


#ifdef __cplusplus
extern "C" {
#endif

/* ezgrpc builtin signal handler. You can either use this by passing it
 * to ezgrpc_init, or build one of your own.
 * */

#ifdef _WIN32
int ezgrpc_init(
    PHANDLER_ROUTINE handler,
    ezhandler_arg *ezarg);
BOOL ezserver_signal_handler(DWORD type);
#else
int ezgrpc_init(
    void *(*handler)(void*),
    ezhandler_arg *ezarg);
void *ezserver_signal_handler(void *userdata);
#endif

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
