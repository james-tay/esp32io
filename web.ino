/*
   This is a convenience function to close a connected TCP client and reset
   its S_WebClient structure.
*/

void f_close_webclient(int idx)
{
  if (G_runtime->config.debug)
    Serial.printf("DEBUG: f_close_webclient() idx:%d sd:%d\r\n",
                  idx, G_runtime->webclients[idx].sd) ;

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
   determined that the webclient wants to scrape metrics. This function
   iterates through active user task threads and renders metrics for all
   result values exposed.
*/

void f_handle_utask_metrics(int idx)
{
  int remainder ;
  char s[BUF_LEN_LINE], *p ;            // general purpose
  char label_cfg[BUF_LEN_LINE] ;        // for "/<name>.labels" file contents
  char label_set[BUF_LEN_LINE] ;        // collection of all metric labels

  G_runtime->metrics_buf[0] = 0 ;       // accumulate small strings here

  // iterate through all user task threads which are running

  for (int slot=0 ; slot < DEF_MAX_USER_THREADS ; slot++)
    if (G_runtime->utask[slot].state == UTHREAD_RUNNING)
    {
      // decide "metric" and "static_labels" we'll use for this user thread

      char *metric=G_runtime->utask[slot].name, *static_labels=NULL ;
      snprintf(s, BUF_LEN_LINE, "/%s.labels", G_runtime->utask[slot].name) ;
      if (f_read_single_line(s, label_cfg, BUF_LEN_LINE) > 0)
      {
        metric = label_cfg ;
        p = strstr(label_cfg, ",") ;
        if (p)                          // static labels are present
        {
          *p = 0 ;
          static_labels = p + 1 ;
        }
      }

      // iterate through all exposed results

      for (int r_idx=0 ; r_idx < DEF_MAX_THREAD_RESULTS ; r_idx++)
      {
        S_ThreadResult *r_ptr = &G_runtime->utask[slot].result[r_idx] ;
        if (r_ptr->result_type != UTHREAD_RESULT_NONE)
        {
          // consolidate all user task thread defined labels in this result,
          // accumulate them in "label_set", track it with "remainder".

          memset(label_set, 0, BUF_LEN_LINE) ;
          remainder = BUF_LEN_LINE - 1 ;
          if (static_labels)
          {
            strncpy(label_set, static_labels, BUF_LEN_LINE-1) ;
            remainder = remainder - strlen(static_labels) ;
          }

          for (int l_idx=0 ; l_idx < DEF_MAX_THREAD_LABELS ; l_idx++)
            if ((r_ptr->l_name[l_idx]) && (r_ptr->l_data[l_idx]))
            {
              snprintf(s, BUF_LEN_LINE, "%s=\"%s\"",
                       r_ptr->l_name[l_idx], r_ptr->l_data[l_idx]) ;
              if (strlen(label_set) > 0)
              {
                strncat(label_set, ",", remainder) ;
                remainder-- ;
              }
              strncat(label_set, s, remainder) ;
              remainder = BUF_LEN_LINE - strlen(label_set) - 1 ;
            }

          // at this point, we can assemble the whole metric line into "s",
          // depending on the "result_type".

          s[0] = 0 ;
          switch (r_ptr->result_type)
          {
            case UTHREAD_RESULT_INT:
              snprintf(s, BUF_LEN_LINE, "%s{%s} %d\n",
                       metric, label_set, r_ptr->i_value) ;
              break ;
            case UTHREAD_RESULT_FLOAT:
              snprintf(s, BUF_LEN_LINE, "%s{%s} %f\n",
                       metric, label_set, r_ptr->f_value) ;
              break ;
            case UTHREAD_RESULT_LONGLONG:
              snprintf(s, BUF_LEN_LINE, "%s{%s} %lld\n",
                       metric, label_set, r_ptr->ll_value) ;
              break ;
          }

          // accumulate metrics in "metrics_buf" instead of write()'ing small
          // buffers to the web client.

          remainder = BUF_LEN_METRICS - strlen(G_runtime->metrics_buf) - 1 ;
          strncat(G_runtime->metrics_buf, s, remainder) ;

        } // ... loop thru all results which are not UTHREAD_RESULT_NONE
      } // ... loop thru all exposed results in this user task thread
    } // ... loop thru all user task threads which are running

  if (strlen(G_runtime->metrics_buf) > 0)
    write(G_runtime->webclients[idx].sd, G_runtime->metrics_buf,
          strlen(G_runtime->metrics_buf)) ;
}

/*
   This function is called from "f_handle_webrequest()" when it has been
   determined that the webclient wants to scrape metrics. All this function
   does is to walk through all available metrics and send them to the client.
*/

void f_handle_core_metrics(int idx)
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
  size_t heap_free_block = heap_caps_get_largest_free_block(
                             MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL) ;

  snprintf(s, l, "ec_uptime_secs %lld\n", esp_timer_get_time() / 1000000) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
  snprintf(s, l, "ec_chip_temperature %.2f\n", temperatureRead()) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
  snprintf(s, l, "ec_free_heap_bytes %ld\n", xPortGetFreeHeapSize()) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
  snprintf(s, l, "ec_free_block_bytes %ld\n", heap_free_block) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
  snprintf(s, l, "ec_ntp_updates %lu\n", r->ntp_updates) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
  snprintf(s, l, "ec_ts_last_ntp_sync %llu\n", r->ts_last_ntp_sync) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
  snprintf(s, l, "ec_wifi_connects %ld\n", r->wifi_connects) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
  snprintf(s, l, "ec_wifi_channel %d\n", WiFi.channel()) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
  snprintf(s, l, "ec_wifi_rssi %d\n", WiFi.RSSI()) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
  snprintf(s, l, "ec_user_tasks_running %d\n", f_task_running()) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;

  // serial port metrics

  snprintf(s, l, "ec_serial_in_bytes %lu\n", r->serial_in_bytes) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
  snprintf(s, l, "ec_serial_ts_last_loop %lld\n", r->serial_ts_last_loop) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
  snprintf(s, l, "ec_serial_ts_last_read %lld\n", r->serial_ts_last_read) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
  snprintf(s, l, "ec_serial_commands %lu\n", r->serial_commands) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
  snprintf(s, l, "ec_serial_overruns %lu\n", r->serial_overruns) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;

  // web server metrics

  snprintf(s, l, "ec_web_accepts %lu\n", r->web_accepts) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
  snprintf(s, l, "ec_web_ts_last_accept %lld\n", r->web_ts_last_accept) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
  snprintf(s, l, "ec_web_busy_rejects %lu\n", r->web_busy_rejects) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
  snprintf(s, l, "ec_web_requests_overrun %lu\n", r->web_requests_overrun) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
  snprintf(s, l, "ec_web_requests_received %lu\n", r->web_requests_received) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
  snprintf(s, l, "ec_web_invalid_requests %lu\n", r->web_invalid_requests) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
  snprintf(s, l, "ec_web_idle_timeouts %lu\n", r->web_idle_timeouts) ;
  strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;

  // if the MQTT subsystem is setup

  if (r->pubsub_state)
  {
    snprintf(s, l, "ec_mqtt_ts_last_connect %lld\n", r->mqtt_connect_ts) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
    snprintf(s, l, "ec_mqtt_connects %ld\n", r->mqtt_connects) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
    snprintf(s, l, "ec_mqtt_connect_fails %ld\n", r->mqtt_connect_fails) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
    snprintf(s, l, "ec_mqtt_connected %d\n", G_psClient.connected()) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
    snprintf(s, l, "ec_mqtt_state %d\n", G_psClient.state()) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
  }

  // if the camera subsystem is configured, expose its metrics

  if (r->cam_data)
  {
    S_CamData *c = r->cam_data ; // a macro

    snprintf(s, l, "ec_cam_xclk_mhz %lu\n", c->xclk_mhz) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
    snprintf(s, l, "ec_cam_frames_ok %lu\n", c->frames_ok) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
    snprintf(s, l, "ec_cam_frames_bad %lu\n", c->frames_bad) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
    snprintf(s, l, "ec_cam_last_frame_size %lu\n", c->last_frame_size) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
    snprintf(s, l, "ec_cam_last_capture_usec %lu\n", c->last_capture_usec) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
    snprintf(s, l, "ec_cam_last_xmit_msec %lu\n", c->last_xmit_msec) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
  }

  // worker threads, iterate over them

  for (int idx=0 ; idx < DEF_WORKER_THREADS ; idx++)
  {
    S_WorkerData *w = &G_runtime->worker[idx] ;                 // just a macro
    snprintf(s, l, "ec_worker_cmds_executed{id=\"%s\"} %lu\n",
             w->name, w->cmds_executed) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
    snprintf(s, l, "ec_worker_total_busy_ms{id=\"%s\"} %lu\n",
             w->name, w->total_busy_ms) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
    snprintf(s, l, "ec_worker_ts_last_cmd{id=\"%s\"} %lld\n",
             w->name, w->ts_last_cmd) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
    snprintf(s, l, "ec_worker_state{id=\"%s\"} %lu\n",
             w->name, w->state) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS - strlen(r->metrics_buf)) ;
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
    f_handle_core_metrics(idx) ;
    f_handle_utask_metrics(idx) ;
    f_close_webclient(idx) ;
    return ;
  }

  // the camera endpoint, recall that we don't want this thread to block

  if ((strcmp(method, "GET") == 0) &&
      (strcmp(G_runtime->url_path, "/cam") == 0))
  {
    f_handle_camera(idx, uri) ;
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

    // select a worker thread, get it prepared and then wake it up. Note that
    // if all worker threads are busy, this webserver thread blocks here.

    int tid = f_get_next_worker() ;
    G_runtime->webclients[idx].worker = tid ;
    G_runtime->webclients[idx].ts_start = esp_timer_get_time() ;
    G_runtime->worker[tid].caller = idx ;
    G_runtime->worker[tid].cmd = G_runtime->webclients[idx].buf ;
    xTaskNotifyGive(G_runtime->worker[tid].w_handle) ;
    return ;
  }

  // if we got here, that means the web client didn't call any valid endpoints

  char *r1 = "HTTP/1.1 404 Not Found\n" ;
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
    client->ts_last_activity = esp_timer_get_time() ;

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
  w->ts_end = esp_timer_get_time() ;

  // IMPORTANT !!! The "result_code" is typically set to reflect the outcome
  // of the user requested command. This function will print the HTTP headers,
  // the contents of "result_msg", and finally a summary of how the command
  // execution turned out. However, if the value of "result_code" is "0", this
  // is a special case and we don't print anything. This is probably because
  // any and all output meant for the client has already been delivered.

  if (G_runtime->worker[tid].result_code != 0)
  {
    char *resp_l1 = "HTTP/1.1 200 OK\n" ;
    char *resp_l2 = "Content-Type: text/plain\n" ;
    char *resp_l3 = "Connection: close\n\n" ;
    write(w->sd, resp_l1, strlen(resp_l1)) ;
    write(w->sd, resp_l2, strlen(resp_l2)) ;
    write(w->sd, resp_l3, strlen(resp_l3)) ;

    // now send the worker thread's result to the HTTP client and clean up

    write(w->sd, G_runtime->worker[tid].result_msg,
          strlen(G_runtime->worker[tid].result_msg)) ;
    snprintf(line, BUF_LEN_LINE, "[code:%d time:%dms]\n",
             G_runtime->worker[tid].result_code,
             (w->ts_end - w->ts_start) / 1000) ;
    write(w->sd, line, strlen(line)) ;
  }
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
      long long now = esp_timer_get_time() ;

      if (FD_ISSET(listen_sd, &fds))            // new client connection
      {
        G_runtime->web_ts_last_accept = now ;
        int new_sd = accept(listen_sd, NULL, NULL) ;
        if (G_runtime->config.debug)
          Serial.printf("DEBUG: f_webserver_thread() new_sd:%d\r\n", new_sd) ;

        // see if we have an available "webclients" slot for "new_sd"

        for (idx=0 ; idx < DEF_WEBSERVER_MAX_CLIENTS ; idx++)
          if (G_runtime->webclients[idx].sd < 0)
          {
            G_runtime->webclients[idx].sd = new_sd ;
            G_runtime->webclients[idx].buf_pos = 0 ;
            G_runtime->webclients[idx].buf[0] = 0 ;
            G_runtime->webclients[idx].ts_last_activity = now ;
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
          Serial.printf("FATAL! read() failed on event_sd.\r\n") ; // very bad
      }
    }

    // check if connected clients have been idle for too long. webclients with
    // no selected worker thread have "worker" set to -1.

    long long now = esp_timer_get_time() ;
    for (idx=0 ; idx < DEF_WEBSERVER_MAX_CLIENTS ; idx++)
      if ((G_runtime->webclients[idx].worker < 0) &&
          (G_runtime->webclients[idx].sd > 0))
      {
        unsigned long age = now - G_runtime->webclients[idx].ts_last_activity ;
        if (age > (DEF_WEBSERVER_MAX_IDLE_MS * 1000))
        {
          f_close_webclient(idx) ;
          G_runtime->web_idle_timeouts++ ;
        }
      }
  }
}
