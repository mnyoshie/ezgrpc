/* ezgrpc.h - a (crude) grpc server without the extra fancy
 * features
 *
 * Copyright (c) 2023-2024 M. N. Yoshie
 *
 * released under MIT license.
 *
 *            _____ _____     ____  ____   ____
 *           | ____|__  /__ _|  _ \|  _ \ / ___|
 *           |  _|   / // _` | |_) | |_) | |
 *           | |___ / /| (_| |  _ <|  __/| |___
 *           |_____/____\__, |_| \_\_|    \____|
 *                      |___/
 */
#include "ezgrpc.h"

// #define EZENABLE_DEBUG
#define ezlogm(fmt, ...)                                                       \
  ezlogf(ezsession, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

/* value of pthread_self() */
static pthread_t g_thread_self;

static char *grpc_status2str(ezgrpc_status_code_t status) {
  switch (status) {
  case EZGRPC_GRPC_STATUS_OK:
    return "";
  case EZGRPC_GRPC_STATUS_CANCELLED:
    return "cancelled";
  case EZGRPC_GRPC_STATUS_UNKNOWN:
    return "unknown";
  case EZGRPC_GRPC_STATUS_INVALID_ARGUMENT:
    return "invalid argument";
  case EZGRPC_GRPC_STATUS_DEADLINE_EXCEEDED:
    return "deadline exceeded";
  case EZGRPC_GRPC_STATUS_NOT_FOUND:
    return "not found";
  case EZGRPC_GRPC_STATUS_ALREADY_EXISTS:
    return "already exists";
  case EZGRPC_GRPC_STATUS_PERMISSION_DENIED:
    return "permission denied";
  case EZGRPC_GRPC_STATUS_UNAUTHENTICATED:
    return "unauthenticated";
  case EZGRPC_GRPC_STATUS_RESOURCE_EXHAUSTED:
    return "resource exhausted";
  case EZGRPC_GRPC_STATUS_FAILED_PRECONDITION:
    return "failed precondition";
  case EZGRPC_GRPC_STATUS_OUT_OF_RANGE:
    return "out of range";
  case EZGRPC_GRPC_STATUS_UNIMPLEMENTED:
    return "unimplemented";
  case EZGRPC_GRPC_STATUS_INTERNAL:
    return "internal";
  case EZGRPC_GRPC_STATUS_UNAVAILABLE:
    return "unavailable";
  case EZGRPC_GRPC_STATUS_DATA_LOSS:
    return "data loss";
  default:
    return "(null)";
  }
}

static void dump_decode_binary(char *data, int len) {
  char look_up[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                      '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
  for (int i = 0; i < len; i++) {
    putchar(look_up[(data[i] >> 4) & 0x0f]);
    putchar(look_up[data[i] & 0x0f]);
    putchar(' ');
    if (i == 7)
      putchar(' ');
  }
}

static void dump_decode_ascii(char *data, int len) {
  for (int i = 0; i < len; i++) {
    if (data[i] < 0x20) {
      putchar('.');
    } else if (data[i] < 0x7f)
      putchar(data[i]);
    else
      putchar('.');
  }
}

void ezdump(void *vdata, size_t len) {
  char *data = vdata;
  if (len == 0)
    return;

  size_t cur = 0;

  for (; cur < 16 * (len / 16); cur += 16) {
    dump_decode_binary(data + cur, 16);
    printf("| ");
    dump_decode_ascii(data + cur, 16);
    printf(" |");
    puts("");
  }
  /* write the remaining */
  if (len % 16) {
    dump_decode_binary(data + cur, len % 16);
    /* write the empty */
    for (int i = 0; i < 16 - (len % 16); i++) {
      printf("   ");
      if (i == 7 && (7 < 16 - (len % 16)))
        putchar(' ');
    }
    printf("| ");
    dump_decode_ascii(data + cur, len % 16);
    for (int i = 0; i < 16 - (len % 16); i++) {
      putchar(' ');
    }
    printf(" |");
    puts("");
    cur += len % 16;
  }
  assert(len == cur);
}

static char *ezgetdt() {
  static char buf[32] = {0};
  time_t t = time(NULL);
  struct tm stm = *localtime(&t);
  snprintf(buf, 32, "%d-%02d-%02d %02d:%02d:%02d", stm.tm_year + 1900,
           stm.tm_mon + 1, stm.tm_mday, stm.tm_hour, stm.tm_min, stm.tm_sec);
  return buf;
}

static void ezlogf(ezgrpc_session_t *ezsession, char *file, int line, char *fmt,
                   ...) {
  fprintf(stdout, "[%s @ %s:%d] %s ", ezgetdt(), file, line,
          ezsession->client_addr);

  va_list args;
  va_start(args, fmt);
  vfprintf(stdout, fmt, args);
  va_end(args);
}

void ezlog(char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  fprintf(stdout, "[%s] ", ezgetdt());
  vfprintf(stdout, fmt, args);
  va_end(args);
}

static void ezfree_message(ezgrpc_message_t *ezmessage) {
  ezgrpc_message_t *snext;
  for (ezgrpc_message_t *msg = ezmessage; msg != NULL; msg = snext) {
    snext = msg->next;
    free(msg);
    // printf("freed msg\n");
  }
}

/* constructs a linked list from a prefixed-length message. */
static ezgrpc_message_t *ezparse_grpc_message(ezvec_t vec, int *nb_message) {

  char *wire = (char *)vec.data;
  uint32_t len = vec.data_len;

  if (len <= 4)
    return NULL;

  ezgrpc_message_t *ezmessage_tail = NULL;

  ezgrpc_message_t **ezmessage_head = &ezmessage_tail;

  size_t seek;
  for (seek = 0; seek < len;) {
    assert(seek <= len);

    ezgrpc_message_t *frame = calloc(1, sizeof(ezgrpc_message_t));
    if (frame == NULL) {
      ezfree_message(ezmessage_tail);
      printf("no mem\n");
      return NULL;
    }
    frame->next = NULL;

    frame->is_compressed = wire[seek];
    seek += 1;

    frame->data_len = ntohl(*((uint32_t *)(wire + seek)));
    seek += sizeof(uint32_t);

    frame->data = wire + seek;
    seek += frame->data_len;

    *ezmessage_head = frame;
    ezmessage_head = &(frame->next);
    (*nb_message)++;

    if (seek > len) {
      ezfree_message(ezmessage_tail);
      ezlog("prefixed-length messages overflowed\n");
      return NULL;
    }

    // printf("nb %d\n", *nb_message);
  }

  if (seek < len) {
    ezfree_message(ezmessage_tail);
    ezlog("prefixed-length messages underflowed\n");
    return NULL;
  }

  return ezmessage_tail;
}

/* Turns linked lists into a raw data which can be sent
 *
 * input: ezmessage
 * output: vec
 */
static int ezflatten_grpc_message(ezgrpc_message_t *ezmessage, ezvec_t *vec) {
  size_t seek = 0;
  size_t ssize = 0;

  /* pre compute needed size */
  for (ezgrpc_message_t *msg = ezmessage; msg != NULL; msg = msg->next)
    ssize += 5 + msg->data_len;

  vec->data_len = ssize;
  vec->data = malloc(ssize);
  if (vec->data == NULL){
    fprintf(stderr, "no mem\n");
    return 1;
  }

  for (ezgrpc_message_t *msg = ezmessage; msg != NULL; msg = msg->next) {
    vec->data[seek] = msg->is_compressed;
    seek += 1;
    *((uint32_t *)&vec->data[seek]) = htonl(msg->data_len);
    seek += 4;
    memcpy(vec->data + seek, msg->data, msg->data_len);
    seek += (size_t)msg->data_len;
  }

  return 0;
}

static ezgrpc_message_t *ezdecompress_grpc_message(ezgrpc_message_t *ezmessage,
                                                   int *nb_message) {
  return NULL;
}

void *ezserver_signal_handler(void *userdata) {
  ezhandler_arg *ezarg = userdata;
  sigset_t *signal_mask = ezarg->signal_mask;
  int shutdownfd = ezarg->shutdownfd;

  int sig, res;
  while (1) {
    res = sigwait(signal_mask, &sig);
    if (res != 0) {
      write(2, "sigwait err\n", 12);
      continue;
    }
    if (sig & (SIGINT | SIGTERM)) {
      write(1, "SIGINT/SIGTERM received!\n", 25);
      if (write(shutdownfd, "shutdown", 8) < 0)
        perror("write");
      continue;
    }
    if (sig & SIGPIPE)
      continue;

    write(2, "unknown signal\n", 15);
  }
}

/* traverses the linked list */
static ezgrpc_stream_t *ezget_stream(ezgrpc_stream_t *stream, uint32_t id) {
  for (ezgrpc_stream_t *s = stream; s != NULL; s = s->next) {
    if (s->stream_id == id)
      return s;
  }
  return NULL;
}

/* creates a ezstream and adds it to the linked list */
static ezgrpc_stream_t *ezcreate_stream(ezgrpc_stream_t *stream) {
  ezgrpc_stream_t *ezstream = calloc(1, sizeof(ezgrpc_stream_t));
  if (ezstream == NULL)
    return NULL;

  ezstream->next = stream;

  return ezstream;
}

/* only frees what is passed. it does not free all the linked lists */
static void ezfree_stream(ezgrpc_stream_t *stream) {
  free(stream->service_path);
  free(stream->recv_data.data);
  free(stream->send_data.data);
  free(stream);
#ifdef EZENABLE_DEBUG
  printf("freed stream\n");
#endif
}

static int ezremove_stream(ezgrpc_stream_t **stream, uint32_t id) {
  ezgrpc_stream_t **s = stream;
  while (*s != NULL && (*s)->stream_id != id)
    s = &(*s)->next;
  if (*s != NULL) {
    ezgrpc_stream_t *snext = (*s)->next;
    ezfree_stream(*s);
    *s = snext;
    return 0;
  }

  return 1;
}

static void *session_reaper(void *userdata) {
  ezgrpc_session_t *ezsession = userdata;
  size_t volatile *nb_sessions = ezsession->nb_sessions;

  ezsession->is_shutdown = 1;
  printf("reaping session...\n");
  /* wait for other callbacks to finish */
  while (ezsession->nb_running_callbacks) {
    // printf("open %d\n", ezsession->nb_open_streams);
  }
  if (ezsession->nb_open_streams) {
    ezgrpc_stream_t *snext;
    ;
    /* a stream is automatically freed when that stream is closed.
     * a stream is automatically closed when an appropriate response has been
     * submitted for that stream.
     *
     * a problem arises when the client suddenly closes the connection
     * before we even had to submit a response. in that case, we have
     * to manually free the memory we have allocated for that stream.
     */
    printf("streams not closed cleanly. freeing manually\n");
    for (ezgrpc_stream_t *s = ezsession->st; s != NULL; s = snext) {
      snext = s->next;
#ifdef EZENABLE_DEBUG
      printf("STREAM %d CLOSED\n", s->stream_id);
#endif
      ezremove_stream(&ezsession->st, s->stream_id);
      ezsession->nb_open_streams--;
    }
  }
  if (ezsession->nb_open_streams) {
    ezlogm("warning: closing session but nb_open_streams = %d. I'm "
           "intrigued... please contact the dev\n",
           ezsession->nb_open_streams);
  }

  nghttp2_session_del(ezsession->ngsession);
  shutdown(ezsession->sockfd, SHUT_RDWR);

  printf("session got reaped\n");
  fflush(stdout);

  free(ezsession->client_addr);

  /* is_used = 0 */
  memset(ezsession, 0, sizeof(ezgrpc_session_t));
  (*nb_sessions)--;
  return NULL;
}

static ssize_t data_source_read_callback(nghttp2_session *session,
                                         int32_t stream_id, uint8_t *buf,
                                         size_t length, uint32_t *data_flags,
                                         nghttp2_data_source *source,
                                         void *user_data) {
  ezgrpc_session_t *ezsession = user_data;
  /*
   * the data source is actually in ezstream->send_data.
   */
  ezgrpc_stream_t *ezstream = source->ptr;

  if (ezstream->grpc_status == EZGRPC_GRPC_STATUS_OK) {
    nghttp2_nv trailers[] = {
        {(void *)"grpc-status", (void *)"0", 11, 1},
    };

    /* make sure what we're writing don't exceed the available length in buf */
    *data_flags = NGHTTP2_DATA_FLAG_NO_COPY | NGHTTP2_DATA_FLAG_NO_END_STREAM;
    nghttp2_submit_trailer(ezsession->ngsession, stream_id, trailers, 1);
    return ezstream->send_data.data_len;

  } else {
    char grpc_status[32] = {0};
    int len = snprintf(grpc_status, 31, "%d", ezstream->grpc_status);
    char *grpc_message = grpc_status2str(ezstream->grpc_status);
    nghttp2_nv trailers[] = {
        {(void *)"grpc-status", (void *)grpc_status, 11, len},
        {(void *)"grpc-message", (void *)grpc_message, 12,
         strlen(grpc_message)},
    };
    *data_flags = NGHTTP2_DATA_FLAG_EOF | NGHTTP2_DATA_FLAG_NO_END_STREAM;
    nghttp2_submit_trailer(ezsession->ngsession, stream_id, trailers, 2);
    return 0;
  }
}

static void *send_response(void *userdata) {
  ezgrpc_stream_t *ezstream = userdata;
  ezgrpc_session_t *ezsession = ezstream->ezsession;
  ezsession->nb_running_callbacks++;
  int res;

  nghttp2_nv nva[] = {
      {(void *)":status", (void *)"200", 7, 3},
      {(void *)"content-type", (void *)"application/grpc", 12, 16},
  };
  nghttp2_data_provider data_provider = {{.ptr = ezstream},
                                         data_source_read_callback};
  if ((ezstream->recv_data.data_len <= 4)) {
    ezstream->grpc_status = EZGRPC_GRPC_STATUS_INVALID_ARGUMENT;
    goto submit;
  }

  if (ezstream->svcall == NULL) {
    ezstream->grpc_status = EZGRPC_GRPC_STATUS_UNIMPLEMENTED;
    goto submit;
  }

  if (ezstream->grpc_status == EZGRPC_GRPC_STATUS_OK) {
    ezvec_t send_data = {0, NULL};
    /* segment the length-prefixed message into a linked lists */
    int nb_message = 0;
    ezgrpc_message_t *ezmsg_req =
        ezparse_grpc_message(ezstream->recv_data, &nb_message);
    if (ezmsg_req == NULL) {
      ezstream->grpc_status = EZGRPC_GRPC_STATUS_INVALID_ARGUMENT;
      goto submit;
    }

    ezgrpc_message_t *ezmsg_res = NULL;
    /* XXX CALL THE ACTUAL SERVICE!! */
    res = ezstream->svcall(ezmsg_req, &ezmsg_res, NULL);
    ezfree_message(ezmsg_req);
    if (res) {
      ezstream->grpc_status = EZGRPC_GRPC_STATUS_INTERNAL;
      goto submit;
    } else {
      ezvec_t vec;
      if (ezflatten_grpc_message(ezmsg_res, &vec)) {
        ezstream->grpc_status = EZGRPC_GRPC_STATUS_INTERNAL;
        ezfree_message(ezmsg_res);
        goto submit;
      }

      ezfree_message(ezmsg_res);
      ezstream->send_data.data_len = vec.data_len;
      ezstream->send_data.data = vec.data;
    }
  }

submit:
  pthread_mutex_lock(&ezsession->ngmutex);
#ifdef EZENABLE_DEBUG
  printf("submitting\n");
#endif

  nghttp2_submit_response(ezsession->ngsession, ezstream->stream_id, nva, 2,
                          &data_provider);
  while (1) {
    if (ezsession->is_shutdown)
      break;
    if (!(res = nghttp2_session_want_write(ezsession->ngsession))) {
      // ezsession->is_shutdown = 1;
      break;
    }
    if ((res = nghttp2_session_send(ezsession->ngsession))) {
      ezsession->is_shutdown = 1;
      ezlogm("nghttp2: %s\n", nghttp2_strerror(res));
      break;
    }
  }

  pthread_mutex_unlock(&ezsession->ngmutex);
#ifdef EZENABLE_DEBUG
  printf("submitted\n");
#endif
  ezsession->nb_running_callbacks--;
  return 0;
}

/* ----------- BEGIN NGHTTP2 CALLBACKS ------------------*/

static ssize_t ngsend_callback(nghttp2_session *session, const uint8_t *data,
                               size_t length, int flags, void *user_data) {
  ezgrpc_session_t *ezsession = user_data;

  ssize_t ret = send(ezsession->sockfd, data, length, 0);
  if (ret < 0)
    return NGHTTP2_ERR_WOULDBLOCK;

#ifdef EZENABLE_DEBUG
  printf("NGSEND SEND <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< %zu bytes\n", ret);
  ezdump((void *)data, ret);
  printf("NGSEND SEND >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
#endif
  return ret;
}

static ssize_t ngrecv_callback(nghttp2_session *session, uint8_t *buf,
                               size_t length, int flags, void *user_data) {
  ezgrpc_session_t *ezsession = user_data;

  ssize_t ret = recv(ezsession->sockfd, buf, length, 0);
  if (ret < 0)
    return NGHTTP2_ERR_WOULDBLOCK;

#ifdef EZENABLE_DEBUG
  printf("NGRECV RECV <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< %zu bytes\n", ret);
  // ezdump(buf, ret);
  printf("NGRECV RECV >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
#endif

  return ret;
}

/* we're beginning to receive a header; open up a memory for the new stream */
static int on_begin_headers_callback(nghttp2_session *session,
                                     const nghttp2_frame *frame,
                                     void *user_data) {
#ifdef EZENABLE_DEBUG
  printf(">>>>>>>>>>> BEGIN HEADERS CALLBACK STREAM %d <<<<<<<<<<<<\n",
         frame->hd.stream_id);
#endif

  ezgrpc_session_t *ezsession = user_data;
  ezgrpc_stream_t *st = ezcreate_stream(ezsession->st);
  if (st == NULL)
    return NGHTTP2_ERR_CALLBACK_FAILURE;

  st->time = (uint64_t)time(NULL);
  st->stream_id = frame->hd.stream_id;
  st->ezsession = ezsession;
  st->is_shutdown = &ezsession->is_shutdown;

  /* g_thread_self is kind of the NULL for thread */
  st->sthread = g_thread_self;

  ezsession->st = st;
  ezsession->nb_open_streams++;

  return 0;
}

/* ok, here's the header name and value. this will be called from time to time
 */
static int on_header_callback(nghttp2_session *session,
                              const nghttp2_frame *frame, const uint8_t *name,
                              size_t namelen, const uint8_t *value,
                              size_t valuelen, uint8_t flags, void *user_data) {
#ifdef EZENABLE_DEBUG
  printf(">>>>>>>>>>>>>>>>> HEADER CALLBACK %d <<<<<<<<<<<<<<<<\n",
         frame->hd.stream_id);
#endif
  if (frame->hd.type != NGHTTP2_HEADERS)
    return 0;

  (void)flags;
  ezgrpc_session_t *ezsession = user_data;
  ezgrpc_stream_t *ezstream = ezget_stream(ezsession->st, frame->hd.stream_id);
  if (ezstream == NULL) {
    ezlogm("couldn't find stream %d\n", frame->hd.stream_id);
    return 1;
  }

  char *method = ":method";
  char *scheme = ":scheme";
  char *path = ":path";
  /* clang-format off */
  if (strlen(method) == namelen && !strcmp(method, (void *)name))
    ezstream->is_method_post = (strlen("POST") == valuelen &&!strncmp("POST", (void *)value, valuelen));
  else if (strlen(scheme) == namelen && !strcmp(scheme, (void *)name))
    ezstream->is_scheme_http = (strlen("http") == valuelen &&!strncmp("http", (void *)value, valuelen));
  else if (strlen(path) == namelen && !strcmp(path, (void *)name))
    ezstream->service_path = strndup((void *)value, valuelen);
  else if (strlen("content-type") == namelen && !strcmp("content-type", (void *)name))
    ezstream->is_content_grpc = (strlen("application/grpc") <= valuelen && !strncmp("application/grpc", (void *)value, 16));
  else if (strlen("te") == namelen && !strcmp("te", (void *)name))
    ezstream->is_te_trailers = (strlen("trailers") == valuelen && !strncmp("trailers", (void *)value, 8));
  else {
    ;
  }
  /* clang-format on */

#ifdef EZENABLE_DEBUG
  printf("header type: %d, %s: %.*s\n", frame->hd.type, name, (int)valuelen,
         value);
#endif

  return 0;
}

int on_frame_recv_callback(nghttp2_session *session, const nghttp2_frame *frame,
                           void *user_data) {
#ifdef EZENABLE_DEBUG
  printf(">>>>>>>>>>> FRAME RECEIVED CALLBACK STREAM %d <<<<<<<<<<<<\n",
         frame->hd.stream_id);
  printf("frame type: %d\n", frame->hd.type);
#endif
  ezgrpc_session_t *ezsession = user_data;
  switch (frame->hd.type) {
  case NGHTTP2_SETTINGS:
    if (frame->hd.stream_id != 0)
      return 0;

    printf("ack %d, length %zu\n", frame->settings.hd.flags,
           frame->settings.hd.length);
    if (!(frame->settings.hd.flags & NGHTTP2_FLAG_ACK)) {
      // apply settings
      for (size_t i = 0; i < frame->settings.niv; i++) {
        switch (frame->settings.iv[i].settings_id) {
        case NGHTTP2_SETTINGS_HEADER_TABLE_SIZE:
          ezsession->csettings.header_table_size = frame->settings.iv[i].value;
          break;
        case NGHTTP2_SETTINGS_ENABLE_PUSH:
          ezsession->csettings.enable_push = frame->settings.iv[i].value;
          break;
        case NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS:
          ezsession->csettings.max_concurrent_streams =
              frame->settings.iv[i].value;
          break;
          // case NGHTTP2_SETTINGS_FLOW_CONTROL:
          // ezstream->flow_control =  frame->settings.iv[i].value;
          break;
        default:
          break;
        };
      }
    } else if (frame->settings.hd.flags & NGHTTP2_FLAG_ACK &&
               frame->settings.hd.length == 0) {
      /* OK. The client acknowledge our settings. do nothing */
    } else
      assert(0); // TODO
    break;
  case NGHTTP2_HEADERS:
    if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
      nghttp2_submit_rst_stream(ezsession->ngsession, NGHTTP2_FLAG_NONE,
                                frame->hd.stream_id, 1);
    }
    return 0;
  case NGHTTP2_DATA: {
    if (!(frame->hd.flags & NGHTTP2_FLAG_END_STREAM)) {
      /* If service is edge triggered. start sending data */ 

      return 0;
    }
    /* We're received a data frame */
    ezgrpc_stream_t *ezstream =
        ezget_stream(ezsession->st, frame->hd.stream_id);

    if (ezstream == NULL || ezstream->service_path == NULL) {
      nghttp2_submit_rst_stream(ezsession->ngsession, NGHTTP2_FLAG_NONE,
                                frame->hd.stream_id, 1);
      return 0;
    }

    if (!(ezstream->is_method_post && ezstream->is_scheme_http &&
          ezstream->is_te_trailers && ezstream->is_content_grpc)) {
      nghttp2_submit_rst_stream(ezsession->ngsession, NGHTTP2_FLAG_NONE,
                                frame->hd.stream_id, 1);
      return 0;
    }
    // printf("recv data len %zu\n", ezstream->recv_data_len);

    ezgrpc_services_t *sv = ezsession->sv;
    /* look up service path */
    ezgrpc_server_service_callback svcall = NULL;
    if (ezstream->service_path != NULL) {
      for (size_t i = 0; i < sv->nb_services; i++) {
        if (!strcmp(sv->services[i].service_path, ezstream->service_path)) {
#ifdef EZENABLE_DEBUG
          printf("found %s\n", sv->services[i].service_path);
#endif
          svcall = sv->services[i].service_callback;
          break;
        }
      }
    }
    ezstream->grpc_status = EZGRPC_GRPC_STATUS_OK;

    /* set up the parameters */
    ezstream->svcall = svcall;

    // ezgrpc_send_response(ezstream);
    int res = pthread_create(&ezstream->sthread, NULL, send_response, ezstream);
    if (res) {
      ezlogm("fatal: pthread_create %d\n", res);
      nghttp2_submit_rst_stream(ezsession->ngsession, NGHTTP2_FLAG_NONE,
                                frame->hd.stream_id, NGHTTP2_INTERNAL_ERROR);
      return 0;
    }
  } break;
  case NGHTTP2_WINDOW_UPDATE:
    // printf("frame window update %d\n",
    // frame->window_update.window_size_increment); int res;
    //    if ((res = nghttp2_session_set_local_window_size(session,
    //    NGHTTP2_FLAG_NONE, frame->hd.stream_id,
    //    frame->window_update.window_size_increment)) != 0)
    //      assert(0);
    break;
  default:
    break;
  }
  return 0;
}

static int on_data_chunk_recv_callback(nghttp2_session *session, uint8_t flags,
                                       int32_t stream_id, const uint8_t *data,
                                       size_t len, void *user_data) {
#ifdef EZENABLE_DEBUG
  printf(">>>>>>>>>>> DATA CHUNK RECEIVED CALLBACK STREAM %d <<<<<<<<<<<<\n",
         stream_id);
#endif
  ezgrpc_session_t *ezsession = user_data;
  ezgrpc_stream_t *stream = ezget_stream(ezsession->st, stream_id);
  if (stream == NULL)
    return 0;

  void *recv_data =
      realloc(stream->recv_data.data, len + stream->recv_data.data_len);
  if (recv_data == NULL)
    return NGHTTP2_ERR_CALLBACK_FAILURE;

  memcpy(recv_data + stream->recv_data.data_len, data, len);

  stream->recv_data.data_len += len;
  stream->recv_data.data = recv_data;

  return 0;
}

int on_stream_close_callback(nghttp2_session *session, int32_t stream_id,
                             uint32_t error_code, void *user_data) {
#ifdef EZENABLE_DEBUG
  printf("STREAM %d CLOSED\n", stream_id);
#endif

  ezgrpc_session_t *ezsession = user_data;
  ezremove_stream(&ezsession->st, stream_id);

  ezsession->nb_open_streams--;

  return 0;
}

int send_data_callback(nghttp2_session *session, nghttp2_frame *frame,
                       const uint8_t *framehd, size_t length,
                       nghttp2_data_source *source, void *user_data) {
  ezgrpc_session_t *ezsession = user_data;
  ezgrpc_stream_t *ezstream = source->ptr;

  /* send frame header */
  ssize_t res = 0, sent = 0;
  for (int tries = 0; tries < 12; tries++) {
    if ((res = write(ezsession->sockfd, framehd + sent, 9 - sent)) < 0) {
      ezlogm("");
      perror("write");
      continue;
    }
    sent += res;
    if (sent == 9)
      break;
  }
  if (sent != 9) {
    assert(0);
    return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
  }

  /* send padlen */
  if (write(ezsession->sockfd, &(uint8_t){frame->data.padlen - 1},
            frame->data.padlen > 0) != (frame->data.padlen > 0)) {
    assert(0);
    return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
  }

  /* send data */
  res = 0, sent = 0;
  for (int tries = 0; tries < 12; tries++) {
    if ((res = write(ezsession->sockfd, ezstream->send_data.data + sent,
                     ezstream->send_data.data_len - sent)) < 0) {
      ezlog("");
      perror("write");
      continue;
    }
    sent += res;
    if (sent == ezstream->send_data.data_len)
      break;
  }
  if (sent != ezstream->send_data.data_len) {
    assert(0);
    return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
  }

  /* send padding */
  res = 0, sent = 0;
  if (frame->data.padlen > 1) {
    for (int i = 0; i < frame->data.padlen - 1; i++) {
      res += write(ezsession->sockfd, &(char){0}, 1);
    }
  }
  if (frame->data.padlen > 1 && (frame->data.padlen - 1 != res)) {
    assert(0);
    return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
  }

#ifdef EZENABLE_DEBUG
  ezdump((void *)framehd, length);
  ezdump((void *)ezstream->send_data, ezstream->send_data_len);
#endif
  return 0;
}
/* ----------- END NGHTTP2 CALLBACKS ------------------*/

int makenonblock(int sockfd) {
  int res = fcntl(sockfd, F_GETFL, 0);
  if (res == -1)
    return 1;
  res = (res | O_NONBLOCK);
  if (fcntl(sockfd, F_SETFL, res) != 0)
    return 1;

  return 0;
}

static ezgrpc_session_t *server_accept(int sockfd, struct sockaddr *client_addr,
                                       int socklen, void *userdata) {
  EZGRPCServer *server_handle = userdata;
  if (server_handle->ng.nb_sessions >= EZGRPC_MAX_SESSIONS) {
    return NULL;
  }

  ezgrpc_session_t *ezsession = NULL;
  for (int i = 0; i < EZGRPC_MAX_SESSIONS; i++) {
    if (!server_handle->ng.sessions[i].is_used) {
      /* Its not used? I'll take it!! */
      ezsession = server_handle->ng.sessions + i;
      break;
    }
  }

  if (ezsession == NULL)
    assert(0); // XXX: This should never happen

  /* PREPARE NGHTTP2 */
  nghttp2_session_callbacks *ngcallbacks;
  int res = nghttp2_session_callbacks_new(&ngcallbacks);
  if (res) {
    assert(0); // TODO
    shutdown(sockfd, SHUT_RDWR);
  }

  /* clang-format off */
  nghttp2_session_callbacks_set_send_callback(ngcallbacks, ngsend_callback);
  nghttp2_session_callbacks_set_recv_callback(ngcallbacks, ngrecv_callback);

  /* prepares and allocates memory for the incoming HEADERS frame */
  nghttp2_session_callbacks_set_on_begin_headers_callback(ngcallbacks, on_begin_headers_callback);
  /* we received that HEADERS frame. now here's the http2 fields */
  nghttp2_session_callbacks_set_on_header_callback(ngcallbacks, on_header_callback);
  /* we received a frame. do something about it */
  nghttp2_session_callbacks_set_on_frame_recv_callback(ngcallbacks, on_frame_recv_callback);

  //  nghttp2_session_callbacks_set_on_frame_send_callback(callbacks,
  //                                                       on_frame_send_callback);
  nghttp2_session_callbacks_set_on_data_chunk_recv_callback(ngcallbacks, on_data_chunk_recv_callback);
  nghttp2_session_callbacks_set_on_stream_close_callback(ngcallbacks, on_stream_close_callback);
  nghttp2_session_callbacks_set_send_data_callback(ngcallbacks, send_data_callback);
  //  nghttp2_session_callbacks_set_before_frame_send_callback(callbacks,
  //  before_frame_send_callback);
  /* clang-format on */

  res =
      nghttp2_session_server_new(&ezsession->ngsession, ngcallbacks, ezsession);
  nghttp2_session_callbacks_del(ngcallbacks);
  if (res < 0) {
    assert(0); // TODO
  }

  nghttp2_settings_entry siv[] = {
      {NGHTTP2_SETTINGS_MAX_FRAME_SIZE, 1024 * 1024},
  };
  res =
      nghttp2_submit_settings(ezsession->ngsession, NGHTTP2_FLAG_NONE, siv, 1);
  if (res < 0) {

    assert(0); // TODO
  }

  if (makenonblock(sockfd))
    assert(0);

  res = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&(int){1},
                   sizeof(int));
  if (res < 0) {
    assert(0); // TODO
  }

  ezsession->sockfd = sockfd;
  ezsession->shutdownfd = server_handle->shutdownfd;
  ezsession->client_addr =
      strdup(inet_ntoa(((struct sockaddr_in *)client_addr)->sin_addr));
  ezsession->server_port = server_handle->port;
  ezsession->sv = &server_handle->sv;
  ezsession->ngmutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
  ezsession->st = NULL;
  ezsession->nb_sessions = &server_handle->ng.nb_sessions;
  ezsession->nb_running_callbacks = 0;
  ezsession->nb_open_streams = 0;

  /*  all seems to be successful. set the following */
  ezsession->is_used = 1;
  server_handle->ng.nb_sessions++;
  return ezsession;
}

static void *start_session(void *userdata) {
  ezgrpc_session_t *ezsession = userdata;
  ezlogm("-> 0.0.0.0:%d connected \n", ezsession->server_port);
  struct pollfd event[2];
  event[0].fd = ezsession->shutdownfd;
  event[0].events = POLLIN;

  event[1].fd = ezsession->sockfd;
  event[1].events = POLLIN | POLLRDHUP;

  int res;
  /* send the settings */
  while (nghttp2_session_want_write(ezsession->ngsession)) {
    res = nghttp2_session_send(ezsession->ngsession);
    if (res)
      assert(0);
  }

  while (1) {
    event[0].revents = 0;
    event[1].revents = 0;
    /* poll 2 minutes */
    res = poll(event, 2, 1000 * 2);
    if (res == -1) {
      printf("poll: error\n");
      assert(0);
    } else if (res == 0) {
      printf("timeout session %d\n", ezsession->sockfd);
      // session_reaper(ezsession);
      // pthread_exit(NULL);
      /* do whatever you want here. maybe close the connection or pause()
       * and wake it up again with pthread_kill(thread, SIGUSR1). */
      continue;
    }
    if (event[0].revents & POLLIN) {
      ezlog("shutdown requested\n");
      break;
    }

    if (event[1].revents & POLLIN) {
      pthread_mutex_lock(&ezsession->ngmutex);
      if (!nghttp2_session_want_read(ezsession->ngsession))
        break;
      res = nghttp2_session_recv(ezsession->ngsession);
      if (res) {
        ezlog("nghttp2: %s. killing...\n", nghttp2_strerror(res));
        pthread_mutex_unlock(&ezsession->ngmutex);
        break;
      }
      while (nghttp2_session_want_write(ezsession->ngsession)) {
        res = nghttp2_session_send(ezsession->ngsession);
        if (res) {
          ezlog("nghttp2: %s. killing...\n", nghttp2_strerror(res));
          pthread_mutex_unlock(&ezsession->ngmutex);
          break;
        }
      }
      pthread_mutex_unlock(&ezsession->ngmutex);
    }
    if (event[1].revents & (POLLRDHUP | POLLERR) || ezsession->is_shutdown) {
      ezlogm("disconnected\n");
      break;
    }
  }
  session_reaper(ezsession);
  return NULL;
}
/* clang-format off */













/* THIS IS INTENTIONALLY LEFT BLANK */














/*-----------------------------------------------------.
| API FUNCTIONS: You will only have to care about this |
`-----------------------------------------------------*/

/* registers a handler which writes "shutdown" to shutdownfd
 * when a SIGINT or SIGTERM is received.
 *
 * Furthermore, it make sures SIGINT/SIGTERM/SIGPIPE
 * won't propagate through the session threads
 * 
 * must be called only once! */
int ezgrpc_init(void *(*handler)(void*), ezhandler_arg *ezarg) {
  int res = 0;
  
  //g_thread_self = pthread_self();

  /* set up our signal handler to shutdown the server
   */
  sigemptyset(ezarg->signal_mask);
  sigaddset(ezarg->signal_mask, SIGINT);
  sigaddset(ezarg->signal_mask, SIGTERM);
  sigaddset(ezarg->signal_mask, SIGPIPE);
  res = pthread_sigmask (SIG_BLOCK, ezarg->signal_mask, NULL);
  if (res != 0) {
    ezlog("fatal: pthread_sigmask %d\n", res);
    return 1;
  }

  static pthread_t sig_thread;
  /* run our signal handler */
  res = pthread_create(&sig_thread, NULL, handler, ezarg);
  if (res) {
    ezlog("fatal: pthread_create %d\n", res);
    return 1;
  }

  /* make sure the signals won't propagate through the
   * main thread and worker threads
   */
  res = pthread_sigmask(SIG_SETMASK, ezarg->signal_mask, NULL);
  if (res != 0) {
    ezlog("fatal: pthread_sigmask %d\n", res);
    return 1;
  }

  return 0;
}

EZGRPCServer *ezgrpc_server_init() {
  /* clang-format on */
  EZGRPCServer *server_handle = malloc(sizeof(EZGRPCServer));
  if (server_handle == NULL)
    return NULL;

  server_handle->port = 8080;
  server_handle->shutdownfd = -1;

  server_handle->sv.nb_services = 0;
  server_handle->sv.services = NULL;

  server_handle->ng.nb_sessions = 0;
  memset(server_handle->ng.sessions, 0,
         sizeof(ezgrpc_session_t) * EZGRPC_MAX_SESSIONS);
  return server_handle;
}

void ezgrpc_server_free(EZGRPCServer *server_handle) {
  for (int i = 0; i < EZGRPC_MAX_SESSIONS; i++)
    server_handle->ng.sessions[i].is_shutdown = 1;

  printf("shutting down\n");

  while (server_handle->ng.nb_sessions) {
    ;
    ;
  }

  for (int i = 0; i < server_handle->sv.nb_services; i++) {
    free(server_handle->sv.services[i].service_path);
  }

  free(server_handle->sv.services);
  shutdown(server_handle->sockfd, SHUT_RDWR);
  free(server_handle);
}

int ezgrpc_server_set_shutdownfd(EZGRPCServer *server_handle, int shutdownfd) {
  server_handle->shutdownfd = shutdownfd;
  return 0;
}
int ezgrpc_server_set_listen_port(EZGRPCServer *server_handle, uint16_t port) {
  server_handle->port = port;
  return 0;
}

int ezgrpc_server_add_service(EZGRPCServer *server_handle, char *service_path,
                              char flags,
                              ezgrpc_server_service_callback service_callback) {
  size_t nb_services = server_handle->sv.nb_services;
  ezgrpc_service_t *services = server_handle->sv.services;

  services = realloc(services, sizeof(ezgrpc_service_t) *
                                   (server_handle->sv.nb_services + 1));
  if (services == NULL)
    return 1;
  services[nb_services].service_path = strdup(service_path);
  services[nb_services].service_callback = service_callback;
  services[nb_services].service_flags = flags;

  server_handle->sv.nb_services = nb_services + 1;
  server_handle->sv.services = services;
  return 0;
}

int ezgrpc_server_start(EZGRPCServer *server_handle) {
  int ret = 0;
  int sockfd = 0;

  struct sockaddr_in saddr;
  memset(&saddr, 0, sizeof(saddr));
  saddr.sin_family = AF_INET;
  saddr.sin_addr.s_addr = inet_addr("0.0.0.0");
  saddr.sin_port = htons(server_handle->port);

  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("socket");
    return 1;
  }

  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));

  if (bind(sockfd, (struct sockaddr *)&saddr, sizeof(saddr)) == -1) {
    perror("bind");
    return 1;
  }

  if (listen(sockfd, 16) == -1) {
    perror("listen");
    return 1;
  }

  if (makenonblock(sockfd))
    assert(0); // TODO

  server_handle->sockfd = sockfd;

  ezlog("listening on port %d ...\n", server_handle->port);

  struct pollfd event[2];
  event[0].fd = server_handle->shutdownfd;
  event[0].events = POLLIN;

  event[1].fd = server_handle->sockfd;
  event[1].events = POLLIN | POLLRDHUP;

  /* start accepting connections */
  while (1) {
    event[0].revents = 0;
    event[1].revents = 0;
    int res = poll(event, 2, 1000 * 60 * 30);
    if (res == -1) {
      printf("poll: error\n");
      assert(0);
    } else if (res == 0) {
      ezlog("timeout %d\n", server_handle->sockfd);
      continue;
    }
    /* we've received a shutdown */
    if (event[0].revents & POLLIN)
      break;

    if (event[1].revents & POLLIN) {
      ezlog("incoming connection\n");
      int confd = accept(sockfd, (struct sockaddr *)&saddr,
                         (socklen_t *)&(int){sizeof(saddr)});
      if (confd == -1) {
        perror("accept");
        continue;
      }

      ezgrpc_session_t *ezsession = server_accept(
          confd, (struct sockaddr *)&saddr, sizeof(saddr), server_handle);
      if (ezsession == NULL) {
        ezlog("max session reached\n");
        shutdown(confd, SHUT_RDWR);
        continue;
      }
      int res =
          pthread_create(&ezsession->sthread, NULL, start_session, ezsession);
      if (res) {
        ezlog("fatal: pthread_create %d\n", res);
        session_reaper(ezsession);
      }
    }
  }

exit:

  ezlog("exiting %s\n", __func__);
  return ret;
}
