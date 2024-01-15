/* ezgrpc.h - a (crude) grpc server without the extra fancy
 * features
 *
 * Copyright (c) 2023 M. N. Yoshie
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

//#define EZENABLE_DEBUG

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

void ezgrpc_dump(void *vdata, size_t len) {
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
  snprintf(buf, 32, "%d-%02d-%02d %02d:%02d:%02d",stm.tm_year + 1900, stm.tm_mon + 1, stm.tm_mday, stm.tm_hour, stm.tm_min, stm.tm_sec);
  return buf;
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
  free(stream->recv_data);
  free(stream->send_data);
  free(stream);
  printf("freed stream\n");
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
  /* wait for other callbacks */
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
      printf("STREAM %d CLOSED\n", s->stream_id);
      ezremove_stream(&ezsession->st, s->stream_id);
      ezsession->nb_open_streams--;
    }
  }
  assert(!ezsession->nb_open_streams);

  nghttp2_session_del(ezsession->ngsession);
  shutdown(ezsession->sockfd, SHUT_RDWR);

  printf("session got reaped\n");
  fflush(stdout);

  free(ezsession->ip_addr);

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
    assert(length >= 8);
    memset(buf, 0, 8);
    buf[4] = 3;
    buf[5] = 0x08;
    buf[6] = 0x96;
    buf[7] = 0x01;

    *data_flags = NGHTTP2_DATA_FLAG_EOF | NGHTTP2_DATA_FLAG_NO_END_STREAM;
    nghttp2_submit_trailer(ezsession->ngsession, stream_id, trailers, 1);
    return 8;
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
  //  ezcallback_t *cbdata = userdata;
  ezgrpc_stream_t *ezstream = userdata;
  ezgrpc_session_t *ezsession = ezstream->ezsession;
  ezsession->nb_running_callbacks++;

  nghttp2_nv nva[] = {
      {(void *)":status", (void *)"200", 7, 3},
      {(void *)"content-type", (void *)"application/grpc", 12, 16},
  };
  nghttp2_data_provider data_provider = {{.ptr = ezstream},
                                         data_source_read_callback};
  if ((ezstream->recv_data_len <= 4))
    ezstream->grpc_status = EZGRPC_GRPC_STATUS_INVALID_ARGUMENT;

  else if (bswap_32(*(uint32_t*)(ezstream->recv_data + 1)) != (ezstream->recv_data_len - 5))
    ezstream->grpc_status = EZGRPC_GRPC_STATUS_INVALID_ARGUMENT;

  if (ezstream->svcall == NULL)
    ezstream->grpc_status = EZGRPC_GRPC_STATUS_UNIMPLEMENTED;

  if (ezstream->grpc_status == EZGRPC_GRPC_STATUS_OK) {
    /* CALL THE ACTUAL SERVICE!! */
    int res = ezstream->svcall(NULL, NULL, NULL);
    if (res)
      ezstream->grpc_status = EZGRPC_GRPC_STATUS_INTERNAL;
  }

  pthread_mutex_lock(&ezsession->ngmutex);
  printf("submitting\n");
  //  nghttp2_submit_headers(ezsession->ngsession, NGHTTP2_FLAG_END_HEADERS,
  //  ezstream->stream_id, NULL, nva, 2, NULL);
  //  nghttp2_submit_data(ezsession->ngsession, NGHTTP2_FLAG_END_STREAM,
  //  ezstream->stream_id, &data_provider);
  //  nghttp2_submit_trailer(ezsession->ngsession, ezstream->stream_id,
  //  trailers, 1); nghttp2_submit_headers(ezsession->ngsession,
  //  NGHTTP2_FLAG_END_HEADERS | NGHTTP2_FLAG_END_STREAM, ezstream->stream_id,
  //  NULL, trailers, 1, NULL);

  nghttp2_submit_response(ezsession->ngsession, ezstream->stream_id, nva, 2,
                          &data_provider);
  while (nghttp2_session_want_write(ezsession->ngsession) &&
         !ezsession->is_shutdown) {
    int res = nghttp2_session_send(ezsession->ngsession);
    if (res)
      assert(0); // TODO
  }
  pthread_mutex_unlock(&ezsession->ngmutex);
  printf("submitted\n");
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
  ezgrpc_dump((void *)data, ret);
  printf("NGSEND SEND >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
#endif
  return ret;
}

static ssize_t ngrecv_callback(nghttp2_session *session, uint8_t *buf,
                               size_t length, int flags, void *user_data) {
  ezgrpc_session_t *ezsession = user_data;

  //  struct evbuffer *evbuff = bufferevent_get_input(bev);
  //  if (evbuffer_get_length(evbuff) > 1024)
  //    return NGHTTP2_ERR_WOULDBLOCK;

  ssize_t ret = recv(ezsession->sockfd, buf, length, 0);
  if (ret < 0)
    return 0;
  // return NGHTTP2_ERR_WOULDBLOCK;
#ifdef EZENABLE_DEBUG
  printf("NGRECV RECV <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< %zu bytes\n", ret);
  ezgrpc_dump(buf, ret);
  printf("NGRECV RECV >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
#endif

  return ret;
}

/* we're beginning to receive a header; open up a memory for the new stream */
static int on_begin_headers_callback(nghttp2_session *session,
                                     const nghttp2_frame *frame,
                                     void *user_data) {
  printf(">>>>>>>>>>> BEGIN HEADERS CALLBACK STREAM %d <<<<<<<<<<<<\n",
         frame->hd.stream_id);

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
  printf(">>>>>>>>>>>>>>>>> HEADER CALLBACK %d <<<<<<<<<<<<<<<<\n",
         frame->hd.stream_id);
  if (frame->hd.type != NGHTTP2_HEADERS)
    return 0;

  (void)flags;
  ezgrpc_session_t *ezsession = user_data;
  ezgrpc_stream_t *ezstream = ezget_stream(ezsession->st, frame->hd.stream_id);
  if (ezstream == NULL)
    assert(0); // TODO

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

  printf("header type: %d, %s: %.*s\n", frame->hd.type, name, (int)valuelen,
         value);

  return 0;
}

int on_frame_recv_callback(nghttp2_session *session, const nghttp2_frame *frame,
                           void *user_data) {
  printf(">>>>>>>>>>> FRAME RECEIVED CALLBACK STREAM %d <<<<<<<<<<<<\n",
         frame->hd.stream_id);
  ezgrpc_session_t *ezsession = user_data;
  printf("frame type: %d\n", frame->hd.type);
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
    if (!(frame->hd.flags & NGHTTP2_FLAG_END_STREAM))
      return 0;
    /* We're received a data frame */
    ezgrpc_stream_t *ezstream =
        ezget_stream(ezsession->st, frame->hd.stream_id);
    if (ezstream == NULL)
      assert(0); // TODO
    if (ezstream->service_path == NULL)
      assert(0); // TODO

    if (!(ezstream->is_method_post && ezstream->is_scheme_http &&
          ezstream->is_te_trailers && ezstream->is_content_grpc)) {
      nghttp2_submit_rst_stream(ezsession->ngsession, NGHTTP2_FLAG_NONE,
                                frame->hd.stream_id, 1);
      return 0;
    }
    printf("recv data len %zu\n", ezstream->recv_data_len);

    ezgrpc_services_t *sv = ezsession->sv;
    /* look up service path */
    ezgrpc_server_service_callback svcall = NULL;
    if (ezstream->service_path != NULL) {
      for (size_t i = 0; i < sv->nb_services; i++) {
        if (!strcmp(sv->services[i].service_path, ezstream->service_path)) {
          printf("found %s\n", sv->services[i].service_path);
          svcall = sv->services[i].service_callback;
          break;
        }
      }
    }
    ezstream->grpc_status = EZGRPC_GRPC_STATUS_OK;

    /* set up the parameters */
    ezstream->svcall = svcall;

    // ezgrpc_send_response(ezstream);
    int res = pthread_create(&ezstream->sthread, NULL, send_response,
                             ezstream);
    if (res)
      assert(0); // TODO
  }
  default:
    break;
  }
  return 0;
}

static int on_data_chunk_recv_callback(nghttp2_session *session, uint8_t flags,
                                       int32_t stream_id, const uint8_t *data,
                                       size_t len, void *user_data) {
  printf(">>>>>>>>>>> DATA CHUNK RECEIVED CALLBACK STREAM %d <<<<<<<<<<<<\n",
         stream_id);
  ezgrpc_session_t *ezsession = user_data;
  ezgrpc_stream_t *stream = ezget_stream(ezsession->st, stream_id);
  if (stream == NULL)
    assert(0); // TODO

  //  stream->recv_data_len += len;
  void *recv_data = realloc(stream->recv_data, len + stream->recv_data_len);
  if (recv_data == NULL)
    return NGHTTP2_ERR_CALLBACK_FAILURE;

  memcpy(recv_data + stream->recv_data_len, data, len);

  stream->recv_data_len += len;
  stream->recv_data = recv_data;

  return 0;
}

int on_stream_close_callback(nghttp2_session *session, int32_t stream_id,
                             uint32_t error_code, void *user_data) {
  printf("STREAM %d CLOSED\n", stream_id);
  ezgrpc_session_t *ezsession = user_data;
  ezremove_stream(&ezsession->st, stream_id);

  ezsession->nb_open_streams--;

  return 0;
}

/* ----------- END NGHTTP2 CALLBACKS ------------------*/

static ezgrpc_session_t *server_accept(int sockfd,
                                              struct sockaddr *client_addr,
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
    close(sockfd);
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
  //  nghttp2_session_callbacks_set_before_frame_send_callback(callbacks,
  //  before_frame_send_callback);
  /* clang-format on */

  res =
      nghttp2_session_server_new(&ezsession->ngsession, ngcallbacks, ezsession);
  nghttp2_session_callbacks_del(ngcallbacks);
  if (res < 0) {
    assert(0); // TODO
  }

  res =
      nghttp2_submit_settings(ezsession->ngsession, NGHTTP2_FLAG_NONE, NULL, 0);
  if (res < 0) {
    assert(0); // TODO
  }

  res = fcntl(sockfd, F_GETFL, 0);
  if (res == -1)
    return NULL;
  res = (res | O_NONBLOCK);
  if (fcntl(sockfd, F_SETFL, res) != 0)
    assert(0);

  res = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&(int){1},
                   sizeof(int));
  if (res < 0) {
    assert(0); // TODO
  }

  ezsession->sockfd = sockfd;
  ezsession->ip_addr = strdup(inet_ntoa(((struct sockaddr_in*)client_addr)->sin_addr));
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
  time_t t = time(NULL);
  struct tm stm = *localtime(&t);
  printf("[%s] %s connected\n", ezgetdt(), ezsession->ip_addr);
  struct pollfd event;
  event.fd = ezsession->sockfd;
  event.events = POLLIN | POLLRDHUP;

  while (1) {
    event.revents = 0;
    /* poll 2 minutes */
    int res = poll(&event, 1, 1000 * 60 * 2);
    if (res == -1) {
      printf("poll: error\n");
      assert(0);
    } else if (res == 0) {
      printf("timeout %d\n", ezsession->sockfd);
      // session_reaper(ezsession);
      // pthread_exit(NULL);
      /* do whatever you want here. maybe close the connection or pause()
       * and wake it up again with pthread_kill(thread, SIGUSR1). */
      continue;
    }

    if (event.revents | POLLIN) {
      pthread_mutex_lock(&ezsession->ngmutex);
      res = nghttp2_session_recv(ezsession->ngsession);
      if (nghttp2_session_want_read(ezsession->ngsession)) {
        if (res)
          assert(0);
      }
      while (nghttp2_session_want_write(ezsession->ngsession)) {
        printf("want write\n");
        res = nghttp2_session_send(ezsession->ngsession);
        if (res)
          assert(0);
      }
      pthread_mutex_unlock(&ezsession->ngmutex);
    }
    if (event.revents & (POLLRDHUP | POLLERR)) {
      printf("[%s] %s disconnected\n", ezgetdt(), ezsession->ip_addr);
      session_reaper(ezsession);
      break;
    }
  }
  return NULL;
}
/* clang-format off */













/* THIS IS INTENTIONALLY LEFT BLANK */














/*-----------------------------------------------------.
| API FUNCTIONS: You will only have to care about this |
`-----------------------------------------------------*/
EZGRPCServer *ezgrpc_server_init() {
  /* clang-format on */
  EZGRPCServer *server_handle = malloc(sizeof(EZGRPCServer));
  if (server_handle == NULL)
    return NULL;

  g_thread_self = pthread_self();

  server_handle->port = 8080;

  server_handle->sv.nb_services = 0;
  server_handle->sv.services = NULL;

  server_handle->ng.nb_sessions = 0;
  memset(server_handle->ng.sessions, 0,
         sizeof(ezgrpc_session_t) * EZGRPC_MAX_SESSIONS);
  return server_handle;
}

void ezgrpc_server_free(EZGRPCServer *server_handle) {
  free(server_handle->sv.services);
  close(server_handle->sockfd);
  free(server_handle);

}

int ezgrpc_server_set_listen_port(EZGRPCServer *server_handle, uint16_t port) {
  server_handle->port = port;
  return 0;
}

int ezgrpc_server_add_service(EZGRPCServer *server_handle, char *service_path,
                              ezgrpc_server_service_callback service_callback) {
  signal(SIGPIPE, SIG_IGN);
  size_t nb_services = server_handle->sv.nb_services;
  ezgrpc_service_t *services = server_handle->sv.services;

  services = realloc(services, sizeof(ezgrpc_service_t) *
                                   (server_handle->sv.nb_services + 1));
  if (services == NULL)
    return 1;
  services[nb_services].service_path = strdup(service_path);
  services[nb_services].service_callback = service_callback;

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
  } else
    printf("socket listener created\n");

  if (bind(sockfd, (struct sockaddr *)&saddr, sizeof(saddr)) == -1) {
    perror("bind");
    return 1;
  }

  if (listen(sockfd, 16) == -1) {
    perror("listen");
    return 1;
  }

  server_handle->sockfd = sockfd;

  printf("listening...\n");

  /* start accepting connections */
  while (1) {
    int confd = accept(sockfd, (struct sockaddr *)&saddr,
                       (socklen_t *)&(int){sizeof(saddr)});
    if (confd == -1) {
      perror("accept");
      continue;
    }

    ezgrpc_session_t *ezsession = server_accept(
        confd, (struct sockaddr *)&saddr, sizeof(saddr), server_handle);
    if (ezsession == NULL) {
      printf("max session reached\n");
      shutdown(confd, SHUT_RDWR);
      continue;
    }
    int res =
        pthread_create(&ezsession->sthread, NULL, start_session, ezsession);
    if (res) {
      printf("fatal\n");
      shutdown(confd, SHUT_RDWR);
      session_reaper(ezsession);
    }

  }

exit:

  return ret;
}
