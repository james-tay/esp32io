/*
   This is a convenience function to close a connected TCP client and reset
   its S_WebClient structure.
*/

void f_close_webclient(int idx)
{
  close(G_runtime->webclients[idx].sd) ;
  G_runtime->webclients[idx].sd = -1 ;
  G_runtime->webclients[idx].buf_pos = 0 ;
  G_runtime->webclients[idx].buf[0] = 0 ;
}

/*
   This function is called from "f_urldecode()". Our job is to read a single
   char and return its integer value.
*/

int f_hex_to_int (char c)
{
  if ((c >= '0') && (c <= '9')) return(c - '0') ;
  if ((c >= 'a') && (c <= 'f')) return(c - 'a' + 10) ;
  if ((c >= 'A') && (c <= 'F')) return(c - 'A' + 10) ;
  return(0) ; // this should never happen
}

/*
   This function is called from "f_handle_webrequest()". Our job is to examine
   the supplied "buf" for URL encoded tokens (eg, "%aa") and convert them into
   their original value. The supplied buffer is modified when we're done.
*/

void f_url_decode(char *src)
{
  char *dst = src ; // where we are up to in "src"
  while (*src)
  {
    if (*src == '%')            // found a "%aa" to convert
    {
      if ((isxdigit(*(src+1))) && (isxdigit(*(src+2))))
      {
        *dst = (f_hex_to_int(*(src+1)) * 16) + f_hex_to_int(*(src+2)) ;
        dst++ ; src = src + 3 ;
      }
      else // invalid sequence, just copy the '%' and continue
      {
        *dst = *src ; dst++ ; src++ ;
      }
    }
    else
    if (*src == '+')            // just a white space
    {
      *dst = ' ' ; dst++ ; src++ ;
    }
    else                        // no conversion needed
    {
      *dst = *src ; dst++ ; src++ ;
    }
  }
  *dst = 0 ;                    // null-terminate early (potentially)
}

/*
   This function is called from "f_handle_webrequest()" when it has been
   determined that the webclient wants to scrape metrics. All this function
   does is to walk through all available metrics and send them to the client.
*/

void f_handle_metrics(int idx)
{
  S_RuntimeData *r = G_runtime ; // macro since we're referencing it a lot
  int l = BUF_LEN_LINE ;         // macro to shorten the subsequent statements
  char s[l] ;                    // buffer to render a single metrics line

  strcpy(r->metrics_buf, "HTTP/1.1 200 OK\n") ;
  strcat(r->metrics_buf, "Content-Type: text/plain\n") ;
  strcat(r->metrics_buf, "Connection: close\n\n") ;
  write(r->webclients[idx].sd, r->metrics_buf, strlen(r->metrics_buf)) ;

  // base system metrics

  r->metrics_buf[0] = 0 ;
  snprintf(s, l, "ec_uptime_secs %lu\n", millis() / 1000) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS) ;
  snprintf(s, l, "ec_chip_temperature %.2f\n", temperatureRead()) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS) ;
  snprintf(s, l, "ec_free_heap_bytes %ld\n", xPortGetFreeHeapSize()) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS) ;

  // serial port metrics

  snprintf(s, l, "ec_serial_in_bytes %lu\n", r->serial_in_bytes) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS) ;
  snprintf(s, l, "ec_serial_commands %lu\n", r->serial_commands) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS) ;
  snprintf(s, l, "ec_serial_overruns %lu\n", r->serial_overruns) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS) ;

  // web server metrics

  snprintf(s, l, "ec_web_accepts %lu\n", r->web_accepts) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS) ;
  snprintf(s, l, "ec_web_busy_rejects %lu\n", r->web_busy_rejects) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS) ;
  snprintf(s, l, "ec_web_requests_overrun %lu\n", r->web_requests_overrun) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS) ;
  snprintf(s, l, "ec_web_requests_received %lu\n", r->web_requests_received) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS) ;
  snprintf(s, l, "ec_web_invalid_requests %lu\n", r->web_invalid_requests) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS) ;
  snprintf(s, l, "ec_web_idle_timeouts %lu\n", r->web_idle_timeouts) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS) ;

  // worker threads, iterate over them

  for (int idx=0 ; idx < DEF_WORKER_THREADS ; idx++)
  {
    S_WorkerData *w = &G_runtime->worker[idx] ;                 // just a macro
    snprintf(s, l, "ec_worker_cmds_executed{id=\"%s\"} %lu\n",
             w->name, w->cmds_executed) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS) ;
    snprintf(s, l, "ec_worker_total_busy_ms{id=\"%s\"} %lu\n",
             w->name, w->total_busy_ms) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS) ;
    snprintf(s, l, "ec_worker_ts_last_cmd{id=\"%s\"} %lu\n",
             w->name, w->ts_last_cmd) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS) ;
    snprintf(s, l, "ec_worker_state{id=\"%s\"} %lu\n",
             w->name, w->state) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS) ;
  }

  // we're done ! send off all our metrics

  write(r->webclients[idx].sd, r->metrics_buf, strlen(r->metrics_buf)) ;
}

/*
   This function is called from "f_handle_webclient()" when a complete HTTP
   request has been identified for the webclient "idx". This request is
   supplied to us as "method" (ie, "GET") and "uri" (ie, "/foo?key=value").
   Our job is to parse "uri" into "path" and "params". Then we figure out what
   to do with it.
*/

void f_handle_webrequest(int idx, char *method, char *uri)
{
  // parse "uri" temporarily into webserver's "url_path" and "url_params"

  int path_len ;
  char *p ;

  G_runtime->url_path[0] = 0 ;
  G_runtime->url_params[0] = 0 ;
  p = strchr(uri, '?') ;
  if (p == NULL)                        // "path" only, no "params"
    strcpy(G_runtime->url_path, uri) ;
  else
  {
    path_len = p - uri ;
    strncpy(G_runtime->url_path, uri, path_len) ;
    G_runtime->url_path[path_len] = 0 ;
    strcpy(G_runtime->url_params, p+1) ;
  }

  // if prometheus is scraping, send metrics and close connection

  if ((strcmp(method, "GET") == 0) &&
      (strcmp(G_runtime->url_path, "/metrics") == 0))
  {
    f_handle_metrics(idx) ;
    f_close_webclient(idx) ;
    return ;
  }

  // if this is a REST request, parse the "cmd=..." into our "buf" and assign
  // the task to a worker thread.

  if ((strcmp(method, "GET") == 0) &&
      (strcmp(G_runtime->url_path, "/v1") == 0) &&
      (strncmp(G_runtime->url_params, "cmd=", 4) == 0))
  {
    // use our "buf" to prepare the "cmd" we'll hand over to worker thread

    strcpy(G_runtime->webclients[idx].buf, G_runtime->url_params+4) ;
    f_url_decode(G_runtime->webclients[idx].buf) ;

    // select a worker thread, get it prepared and then wake it up

    int tid = f_get_next_worker() ;
    G_runtime->webclients[idx].worker = tid ;
    G_runtime->webclients[idx].ts_start = millis() ;
    G_runtime->worker[tid].caller = idx ;
    G_runtime->worker[tid].cmd = G_runtime->webclients[idx].buf ;
    xTaskNotifyGive(G_runtime->worker[tid].w_handle) ;
    return ;
  }

  // if we got here, that means the web client didn't call any valid endpoints

  char *r1 = "HTTP/1.1 404 OK\n" ;
  char *r2 = "Content-Type: text/plain\n" ;
  char *r3 = "Connection: close\n\n" ;
  char *r4 = "Invalid request\n" ;
  write(G_runtime->webclients[idx].sd, r1, strlen(r1)) ;
  write(G_runtime->webclients[idx].sd, r2, strlen(r2)) ;
  write(G_runtime->webclients[idx].sd, r3, strlen(r3)) ;
  write(G_runtime->webclients[idx].sd, r4, strlen(r4)) ;
  f_close_webclient(idx) ;
}

/*
   This function is called from "f_webserver_thread()" when there's some
   activity on the webclient at "idx". Our job is to investigate what this
   could be - which is either more bytes coming in, or the TCP session was
   closed.
*/

void f_handle_webclient(int idx)
{
  int amt, available ;
  char *request=NULL ;
  S_WebClient *client = &(G_runtime->webclients[idx]) ;

  available = BUF_LEN_WEBCLIENT - client->buf_pos - 1 ;
  if (available < 1)                    // no more buffer for HTTP header
  {
    G_runtime->web_requests_overrun++ ;
    f_close_webclient(idx) ;
    return ;
  }

  amt = read(client->sd, client->buf + client->buf_pos, available) ;
  if (amt > 0)
  {
    client->buf_pos = client->buf_pos + amt ;
    client->buf[client->buf_pos] = 0 ;
    client->ts_last_activity = millis() ;

    // have we received the full HTTP header ? Just check for 2x new lines

    if ((strstr(client->buf, "\n\n")) || (strstr(client->buf, "\r\n\r\n")))
    {
      // Now isolate the first line and discard the rest

      for (int i=0 ; i < client->buf_pos ; i++)
        if ((client->buf[i] == '\r') || (client->buf[i] == '\n'))
        {
          client->buf[i] = 0 ;
          client->buf_pos = i ;
          request = client->buf ;       // indicate we go to the next step
          break ;
        }
    }
  }
  else                                  // TCP session closed on us
    f_close_webclient(idx) ;

  /*
     if "request" is set, then it (hopefully) points to a request line, eg
       GET /path/to/endpoint?key=value HTTP/1.1

     pull this apart into "method", "uri" and "proto".
  */

  if (request)
  {
    char *method=NULL, *uri=NULL, *proto=NULL, *c_idx=NULL ;

    method = strtok_r(request, " ", &c_idx) ;
    if (method)
      uri = strtok_r(NULL, " ", &c_idx) ;
      if (uri)
        proto = strtok_r(NULL, " ", &c_idx) ;

    if ((method) && (uri) && (proto) &&
        ((strcmp(proto, "HTTP/1.0")==0) || strcmp(proto, "HTTP/1.1")==0))
    {
      G_runtime->web_requests_received++ ;
      if (strlen(uri) < BUF_LEN_WEB_URL)
        f_handle_webrequest(idx, method, uri) ;
      else
        G_runtime->web_requests_overrun++ ;     // "uri" too long
    }
    else
      G_runtime->web_invalid_requests++ ;       // unsupported HTTP protocol
  }
}

/*
   This function is called from "f_webserver_thread()" when webclient "idx"
   has a result from a worker thread. Our job is to return this result to
   the HTTP client and then clean up the web client TCP session and return
   the worker thread to a W_IDLE state.
*/

void f_handle_result(int idx)
{
  char line[BUF_LEN_LINE] ;
  S_WebClient *w = &G_runtime->webclients[idx] ;

  int tid = w->worker ;
  w->ts_end = millis() ;

  // first send our HTTP response header

  char *resp_l1 = "HTTP/1.1 200 OK\n" ;
  char *resp_l2 = "Content-Type: text/plain\n" ;
  char *resp_l3 = "Connection: close\n\n" ;
  write(w->sd, resp_l1, strlen(resp_l1)) ;
  write(w->sd, resp_l2, strlen(resp_l2)) ;
  write(w->sd, resp_l3, strlen(resp_l3)) ;

  // now send the worker thread's result to the HTTP client and clean up

  write(w->sd, G_runtime->worker[tid].result_msg,
        strlen(G_runtime->worker[tid].result_msg)) ;
  snprintf(line, BUF_LEN_LINE, "\ncode:%d time:%dms\n",
           G_runtime->worker[tid].result_code, w->ts_end - w->ts_start) ;
  write(w->sd, line, strlen(line)) ;
  f_close_webclient(idx) ;

  w->worker = -1 ;                              // mark as idle http client
  G_runtime->worker[tid].cmd = NULL ;           // no more active command
  G_runtime->worker[tid].state = W_IDLE ;       // release worker thread
}

/*
   This function forms the life cycle of the webserver thread, thus it never
   exits. We are created from "setup()".
*/

void f_webserver_thread (void *param)
{
  int max_fd, idx ;
  struct fd_set fds ;
  struct timeval tv ;
  struct sockaddr_in listen_saddr, event_saddr ;

  // initialize webclients

  for (idx=0 ; idx < DEF_WEBSERVER_MAX_CLIENTS ; idx++)
  {
    G_runtime->webclients[idx].sd = -1 ;
    G_runtime->webclients[idx].worker = -1 ;
  }

  // "notify_sd" is a shared socket used by worker threads to send a packet.
  // "event_sd" is used only by this thread to listen for incoming packets.

  G_runtime->notify_sd = socket(AF_INET, SOCK_DGRAM, 0) ;
  int event_sd = socket(AF_INET, SOCK_DGRAM, 0) ;
  event_saddr.sin_family = AF_INET ;
  event_saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK) ;
  event_saddr.sin_port = htons(DEF_WEBSERVER_EVENT_PORT) ;
  if (bind(event_sd, (struct sockaddr*) &event_saddr, sizeof(event_saddr)) < 0)
    Serial.printf("FATAL! bind() failed on loopback for event_sd.\r\n") ;

  // setup the TCP socket which listens for http clients.

  int listen_sd = socket(AF_INET, SOCK_STREAM, 0) ;
  listen_saddr.sin_family = AF_INET ;
  listen_saddr.sin_addr.s_addr = htonl(INADDR_ANY) ;
  listen_saddr.sin_port = htons(80) ;
  bind(listen_sd, (struct sockaddr*) &listen_saddr, sizeof(listen_saddr)) ;
  listen(listen_sd, 1) ;

  Serial.printf("BOOT: Webserver running.\r\n") ;
  while (1)
  {
    // get ready to call select()

    tv.tv_sec = 1 ;
    tv.tv_usec = 0 ;
    max_fd = 0 ;

    FD_ZERO(&fds) ;
    if (event_sd > max_fd) max_fd = event_sd ;
    max_fd = event_sd ;
    FD_SET(event_sd, &fds) ;
    if (listen_sd > max_fd) max_fd = listen_sd ;
    FD_SET(listen_sd, &fds) ;
    for (idx=0 ; idx < DEF_WEBSERVER_MAX_CLIENTS ; idx++)
      if (G_runtime->webclients[idx].sd > 0)
      {
        if (G_runtime->webclients[idx].sd > max_fd)
          max_fd = G_runtime->webclients[idx].sd ;
        FD_SET(G_runtime->webclients[idx].sd, &fds) ;
      }

    // sit here and wait for something to happen (or timeout)

    if (select(max_fd + 1, &fds, NULL, NULL, &tv) > 0)
    {
      if (FD_ISSET(listen_sd, &fds))            // new client connection
      {
        int new_sd = accept(listen_sd, NULL, NULL) ;

        // see if we have an available "webclients" slot for "new_sd"

        for (idx=0 ; idx < DEF_WEBSERVER_MAX_CLIENTS ; idx++)
          if (G_runtime->webclients[idx].sd < 0)
          {
            G_runtime->webclients[idx].sd = new_sd ;
            G_runtime->webclients[idx].buf_pos = 0 ;
            G_runtime->webclients[idx].buf[0] = 0 ;
            G_runtime->webclients[idx].ts_last_activity = millis() ;
            G_runtime->web_accepts++ ;
            break ;
          }
        if (idx == DEF_WEBSERVER_MAX_CLIENTS)   // ops, no available slots
        {
          close(new_sd) ;
          G_runtime->web_busy_rejects++ ;
        }
      }

      for (idx=0 ; idx < DEF_WEBSERVER_MAX_CLIENTS ; idx++)
        if (FD_ISSET(G_runtime->webclients[idx].sd, &fds))
          f_handle_webclient(idx) ;             // found activity on webclient

      if (FD_ISSET(event_sd, &fds))             // a worker thread is done
      {
        char payload ;
        if ((read(event_sd, &payload, 1) == 1) &&
            (payload >= 0) && (payload < DEF_WEBSERVER_MAX_CLIENTS))
          f_handle_result((int) payload) ;
        else
          Serial.printf("FATAL! read() failed on event_sd.\r\n") ;
      }
    }

    // check if connected clients have been idle for too long. webclients with
    // no selected worker thread have "worker" set to -1.

    unsigned long now = millis() ;
    for (idx=0 ; idx < DEF_WEBSERVER_MAX_CLIENTS ; idx++)
      if ((G_runtime->webclients[idx].worker < 0) &&
          (G_runtime->webclients[idx].sd > 0))
      {
        unsigned long age = now - G_runtime->webclients[idx].ts_last_activity ;
        if (age > DEF_WEBSERVER_MAX_IDLE_MS)
        {
          f_close_webclient(idx) ;
          G_runtime->web_idle_timeouts++ ;
        }
      }

  }
}
