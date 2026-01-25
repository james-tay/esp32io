void f_webserver_thread (void *param)
{
  int max_fd ;
  struct fd_set fds ;
  struct timeval tv ;
  struct sockaddr_in listen_saddr, event_saddr ;

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

    int result = select(max_fd + 1, &fds, NULL, NULL, &tv) ;


  }
}
