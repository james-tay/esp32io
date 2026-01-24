void f_webserver_thread (void *param)
{
  struct sockaddr_in saddr ;

  int sd = socket(AF_INET, SOCK_STREAM, 0) ;
  saddr.sin_family = AF_INET ;
  saddr.sin_addr.s_addr = htonl(INADDR_ANY) ;
  saddr.sin_port = htons(80) ;
  bind(sd, (struct sockaddr*) &saddr, sizeof(saddr)) ;
  listen(sd, 1) ;

  Serial.printf("BOOT: Webserver running.\r\n") ;
  while (1)
  {
    vTaskDelay(1000) ;
  }
}
