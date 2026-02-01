/*
   This function is called from "f_worker_thread()" and supplied "idx" of the
   worker thread which called us. Our job is to parse/ execute our "cmd" and
   write to our "result_msg" and "result_code". If we're calling another
   function to do the work, then it falls to that function to write to our
   "result_msg" and "result_code".
*/

void f_action(int idx)
{

  // commands are like a menu system. Identify the first "keyword" and then
  // potentially farm it out to other functions which specialize in their
  // implementation.

  char keyword[BUF_LEN_LINE] ;
  memset (keyword, 0, BUF_LEN_LINE) ;
  for (int i=0 ; i < BUF_LEN_LINE-1 ; i++)
    if ((G_runtime->worker[idx].cmd[i] == 0) ||
        (G_runtime->worker[idx].cmd[i] == ' '))
      break ;
    else
      keyword[i] = G_runtime->worker[idx].cmd[i] ;

  if (strcmp(keyword, "help") == 0)
  {
    strncpy(G_runtime->worker[idx].result_msg,
      "set <key> <value>        set device configuration\r\n"
      "version                  show software version and build time\r\n",
      BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 200 ;
  }
  else
  if (strcmp(keyword, "set") == 0)
  {
    f_set_config(idx) ;
  }
  else
  if (strcmp(keyword, "version") == 0)
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "esp32io git commit %s, built %s.", BUILD_COMMIT, BUILD_TIME) ;
    G_runtime->worker[idx].result_code = 200 ;
  }
  else
  {
    strcpy(G_runtime->worker[idx].result_msg, "Invalid command.") ;
    G_runtime->worker[idx].result_code = 404 ;
  }
}

/*
   This function forms the thread lifecycle of a single worker thread. We are
   created from setup(). Our main loop starts with "ulTaskNotifyTake()" which
   makes us block until we're told to wake up. At which time, we process the
   supplied "cmd". When our work is complete, we write "result_msg" and
   "result_code". Finally we notify our caller (be it the serial console
   thread, or an HTTP webclient).
*/

void f_worker_thread(void *param)
{
  int myidx = *((int*)param) ;
  Serial.printf("BOOT: %s thread started.\r\n",
                G_runtime->worker[myidx].name) ;

  while (1)
  {
    // sit here and wait until somebody tells us to do work

    ulTaskNotifyTake(pdTRUE, portMAX_DELAY) ;

    // at this point, we've been woken up, time to do work

    G_runtime->worker[myidx].ts_start = millis() ;
    G_runtime->worker[myidx].state = W_BUSY ;
    f_action(myidx) ;
    G_runtime->worker[myidx].state = W_DONE ;

    // update our internal metrics to reflect work we just did

    G_runtime->worker[myidx].cmds_executed++ ;
    G_runtime->worker[myidx].ts_last_cmd = millis() ;
    G_runtime->worker[myidx].total_busy_ms +=
      G_runtime->worker[myidx].ts_last_cmd -
      G_runtime->worker[myidx].ts_start ;

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
