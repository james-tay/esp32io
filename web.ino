/*
   This function is called from "f_webserver_thread()" when there's some
   activity on the webclient at "idx". Our job is to investigate what this
   could be - which is either more bytes coming in, or the TCP session was
   closed.
*/

void f_handle_webclient(int idx)
{
  int amt, available ;
  S_WebClient *client = &(G_runtime->webclients[idx]) ;

  available = BUF_LEN_WEBCLIENT - client->buf_pos - 1 ;
  if (available < 1)
  {
    close(client->sd) ;
    client->sd = -1 ;
    return ;
  }

  amt = read(client->sd, client->buf + client->buf_pos, available) ;
  if (amt > 0)
  {
    client->buf_pos = client->buf_pos + amt ;
    client->buf[client->buf_pos] = 0 ;
  }
  else
  {
    close(client->sd) ;
    client->sd = -1 ;
    client->buf_pos = 0 ;
    client->buf[0] = 0 ;
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
            break ;
          }

        if (idx == DEF_WEBSERVER_MAX_CLIENTS)   // ops, no available slots
          close(new_sd) ;
      }

      for (idx=0 ; idx < DEF_WEBSERVER_MAX_CLIENTS ; idx++)
        if (FD_ISSET(G_runtime->webclients[idx].sd, &fds))
          f_handle_webclient(idx) ;             // found activity on webclient
    }



  }
}
