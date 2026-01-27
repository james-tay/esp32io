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
   This function is called from "f_handle_webclient()" when a complete HTTP
   request has been identified for the webclient "idx". This request is
   supplied to us as "method" (ie, "GET") and "uri" (ie, "/foo?key=value").
   Our job is to parse "uri" and figure out what to do with it.
*/

void f_handle_webrequest(int idx, char *method, char *uri)
{
  #define LINE_LEN 128
  char s[LINE_LEN] ;

  if ((strcmp(method, "GET") == 0) && (strcmp(uri, "/metrics") == 0))
  {
    S_RuntimeData *r = G_runtime ; // a macro since we're referencing it a lot

    strcpy(r->metrics_buf, "HTTP/1.1 200 OK\n") ;
    strcat(r->metrics_buf, "Content-Type: text/plain\n") ;
    strcat(r->metrics_buf, "Connection: close\n\n") ;
    write(r->webclients[idx].sd, r->metrics_buf, strlen(r->metrics_buf)) ;

    // base system metrics

    r->metrics_buf[0] = 0 ;
    snprintf(s, LINE_LEN, "ec_uptime_secs %lu\n", millis() / 1000) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS) ;
    snprintf(s, LINE_LEN, "ec_chip_temperature %.2f\n", temperatureRead()) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS) ;
    snprintf(s, LINE_LEN, "ec_free_heap_bytes %ld\n", xPortGetFreeHeapSize()) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS) ;

    // serial port metrics

    snprintf(s, LINE_LEN, "ec_serial_in_bytes %lu\n", r->serial_in_bytes) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS) ;
    snprintf(s, LINE_LEN, "ec_serial_commands %lu\n", r->serial_commands) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS) ;
    snprintf(s, LINE_LEN, "ec_serial_overruns %lu\n", r->serial_overruns) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS) ;

    // web server metrics

    snprintf(s, LINE_LEN, "ec_web_accepts %lu\n", r->web_accepts) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS) ;
    snprintf(s, LINE_LEN, "ec_web_busy_rejects %lu\n", r->web_busy_rejects) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS) ;
    snprintf(s, LINE_LEN, "ec_web_requests_overrun %lu\n",
             r->web_requests_overrun) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS) ;
    snprintf(s, LINE_LEN, "ec_web_requests_received %lu\n",
             r->web_requests_received) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS) ;
    snprintf(s, LINE_LEN, "ec_web_invalid_requests %lu\n",
             r->web_invalid_requests) ;
    strncat(r->metrics_buf, s, BUF_LEN_METRICS) ;

    // we're done ! send off all our metrics

    write(r->webclients[idx].sd, r->metrics_buf, strlen(r->metrics_buf)) ;
    f_close_webclient(idx) ;
  }
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
      f_handle_webrequest(idx, method, uri) ;
    }
    else
      G_runtime->web_invalid_requests++ ;
  }
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
    G_runtime->webclients[idx].sd = -1 ;

  // setup the UDP socket which listens for work thread events.

  int event_sd = socket(AF_INET, SOCK_DGRAM, 0) ;
  event_saddr.sin_family = AF_INET ;
  event_saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK) ;
  event_saddr.sin_port = htons(DEF_WEBSERVER_EVENT_PORT) ;
  bind(event_sd, (struct sockaddr*) &event_saddr, sizeof(event_saddr)) ;

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
    }
  }
}
