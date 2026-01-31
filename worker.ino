void f_worker_thread(void *param)
{
  int myidx = *((int*)param) ;

  Serial.printf("BOOT: %s thread started.\r\n",
                G_runtime->worker[myidx].name) ;

  while (1)
  {
    // sit here and wait until somebody tells us to do work

    ulTaskNotifyTake(pdTRUE, portMAX_DELAY) ;
    G_runtime->worker[myidx].state = W_BUSY ;

    // put in dummy response to indicate we did work.

    snprintf(G_runtime->worker[myidx].result_msg, BUF_LEN_WORKER_RESULT,
             "hello world from %s.", G_runtime->worker[myidx].name) ;
    G_runtime->worker[myidx].result_code = 200 ;
    G_runtime->worker[myidx].state = W_DONE ;

    // notify the caller that we're done

    if (G_runtime->worker[myidx].caller < 0)    // serial console thread
    {
      xTaskNotifyGive(G_runtime->sconsole_handle) ;
    }
    else                                        // notify a webclient via UDP
    {
      char payload = (char) G_runtime->worker[myidx].caller ;
      struct sockaddr_in dest_addr ;

      dest_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK) ;
      dest_addr.sin_family = AF_INET;
      dest_addr.sin_port = htons(DEF_WEBSERVER_EVENT_PORT) ;

      sendto(G_runtime->notify_sd, &payload, 1, 0,
             (struct sockaddr*)&dest_addr, sizeof(dest_addr)) ;
    }
  }
}
