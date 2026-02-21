/*
   This is a convenience function to parse a "src" string into tokens. The
   "src" string will be modified (ie, space delimiters replaced with NULLs)
   as tokens are discovered. The "tokens" array will contain pointers to all
   identified tokens (up to "max_tokens"). On completion, the total number
   of entries in "tokens" is returned.
*/

int f_parse(char *src, char **tokens, int max_tokens)
{
  int tokens_found=1 ;
  if ((src == NULL) || (strlen(src) < 1) || (max_tokens < 1))
    return(0) ;

  char *p=NULL ;
  tokens[0] = strtok_r(src, " ", &p) ;          // the very first token
  while (tokens_found < max_tokens)
  {
    if (tokens_found == max_tokens - 1)         // this will be the last token
    {
      if ((p != NULL) && (strlen(p) > 0))
      {
        tokens[tokens_found] = p ; // last token is the remainder of "src"
        tokens_found++ ;
      }
      break ;
    }

    char *next = strtok_r(NULL, " ", &p) ;      // try find next token
    if (next == NULL)                           // opsie, no more tokens
      break ;
    tokens[tokens_found] = next ;
    tokens_found++ ;
  }
  return(tokens_found) ;
}

/*
   This function is called from "f_action()" when we're called with either the
   "hi" or "lo" commands. Note that "hi" or "lo" commands may include an
   optional number of microseconds to pulse the pin at.
*/

void f_hi_lo_cmd(int idx)
{
  char *tokens[3], *cmd=NULL ;
  int pin=-1, pulse=-1, state=-1 ;
  int count = f_parse(G_runtime->worker[idx].cmd, tokens, 3) ;
  cmd = tokens[0] ;                     // "hi" or "lo"
  if (count == 1)
  {
    strncpy(G_runtime->worker[idx].result_msg, "No pin specified.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }
  pin = atoi(tokens[1]) ;               // parse the GPIO pin
  if (count == 3)
    pulse = atoi(tokens[2]) ;           // parse the pulse duration (usecs)

  pinMode(pin, OUTPUT) ;
  if (strcmp(cmd, "hi") == 0)
  {
    digitalWrite(pin, HIGH) ;
    state = 1 ;
  }
  else
  {
    digitalWrite(pin, LOW) ;
    state = 0 ;
  }

  // if the user is pulsing this pin, use 2x different calls to implement
  // this. Ie,
  //   delay(msec)              # allows FreeRTOS to run other tasks
  //   delayMicroseconds(usec)  # uses a busy loop, only supports up to 16383 !

  if (pulse > 0)
  {
    int msec = pulse / 1000 ;
    int usec = pulse - (msec * 1000) ;
    delay(msec) ;
    delayMicroseconds(usec) ;

    if (strcmp(cmd, "hi") == 0)
    {
      digitalWrite(pin, LOW) ;
      snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
               "GPIO%d pulsed HIGH for %d usec\r\n", pin, pulse) ;
    }
    else
    {
      digitalWrite(pin, HIGH) ;
      snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
               "GPIO%d pulsed LOW for %d usec\r\n", pin, pulse) ;
    }
  }
  else
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "GPIO%d -> %d\r\n", pin, state) ;

  G_runtime->worker[idx].result_code = 200 ;
}

/*
   This function is called from "f_action()", our job is to print our current
   uptime.
*/

void f_uptime_cmd(int idx)
{
  char line[BUF_LEN_LINE] ;
  struct tm timeinfo ;          // captures current timestamp
  struct timeval tv ;           // captures microsec resolution

  strncpy(line, "clock not ntp synced", BUF_LEN_LINE) ;
  if ((G_runtime->ntp_updates > 0) && (getLocalTime(&timeinfo)))
  {
    gettimeofday(&tv, NULL) ;
    snprintf(line, BUF_LEN_LINE-1, "%04d%02d%02d-%02d%02d.%03d UTC",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
             tv.tv_usec / 1000) ;
  }

  snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
           "uptime %lld secs, %s\r\n", esp_timer_get_time() / 1000000, line) ;
  G_runtime->worker[idx].result_code = 200 ;
}

/*
   This function is called from "f_action()", our job is to print some info
   on our hardware, software and runtime.
*/

void f_version_cmd(int idx)
{
  esp_chip_info_t chip_info ;
  esp_chip_info(&chip_info) ;
  snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
           "Model %s (revision %d), %d cores.\r\n"
           "Git commit %s, built %s.\r\n"
           "G_runtime is %d bytes.\r\n",
           ESP.getChipModel(), chip_info.revision, chip_info.cores,
           BUILD_COMMIT, BUILD_TIME, sizeof(S_RuntimeData)) ;
  G_runtime->worker[idx].result_code = 200 ;
}

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
  // implementation. Use our own "token_buf" because we don't want "f_parse()"
  // to modify "cmd" at this point.

  char token_buf[BUF_LEN_WEBCLIENT], *tokens[1], *keyword=NULL ;
  strncpy(token_buf, G_runtime->worker[idx].cmd, BUF_LEN_WEBCLIENT) ;
  if (f_parse(token_buf, tokens, 1) == 0)
  {
    strncpy(G_runtime->worker[idx].result_msg, "Missing command\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 500 ;
    return ;
  }
  keyword = tokens[0] ;

  if (strcmp(keyword, "help") == 0)
  {
    strncpy(G_runtime->worker[idx].result_msg,
      "cam ...          camera management\r\n"
      "fs ...           filesystem management\r\n"
      "hi <pin> [usec]  set a pin high or pulse it high\r\n"
      "ps               threads cpu time consumed\r\n"
      "lo <pin> [usec]  set a pin low or pulse it low\r\n"
      "ntp <server>     update local clock\r\n"
      "ota <url>        perform a software update\r\n"
      "reload           reload/reboot the device\r\n"
      "set ...          set device configuration\r\n"
      "task ...         manage task threads\r\n"
      "uptime           show device uptime\r\n"
      "version          show software version and build time\r\n"
      "wifi ...         wifi management\r\n",
      BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 200 ;
  }
  else
  if (strncmp(keyword, "/cam", 4) == 0)                         // "/cam"
    f_process_camera(idx) ;
  else
  if (strcmp(keyword, "cam") == 0)                              // cam
    f_cam_cmd(idx) ;
  else
  if (strcmp(keyword, "hi") == 0)                               // hi
    f_hi_lo_cmd(idx) ;
  else
  if (strcmp(keyword, "fs") == 0)                               // fs
    f_fs_cmd(idx) ;
  else
  if (strcmp(keyword, "ps") == 0)                               // ps
  {
    vTaskGetRunTimeStats(G_runtime->worker[idx].result_msg) ;
    G_runtime->worker[idx].result_code = 200 ;
  }
  else
  if (strcmp(keyword, "lo") == 0)                               // lo
    f_hi_lo_cmd(idx) ;
  else
  if (strcmp(keyword, "ntp") == 0)                              // ntp
    f_ntp_cmd(idx) ;
  else
  if (strcmp(keyword, "ota") == 0)                              // ota
    f_ota_cmd(idx) ;
  else
  if (strcmp(keyword, "reload") == 0)                           // reload
    G_runtime->request_reload = 1 ;
  else
  if (strcmp(keyword, "set") == 0)                              // set
    f_set_cmd(idx) ;
  else
  if (strcmp(keyword, "task") == 0)                             // task
    f_task_cmd(idx) ;
  else
  if (strcmp(keyword, "uptime") == 0)                           // uptime
    f_uptime_cmd(idx) ;
  else
  if (strcmp(keyword, "version") == 0)                          // version
    f_version_cmd(idx) ;
  else
  if (strcmp(keyword, "wifi") == 0)                             // wifi
    f_wifi_cmd(idx) ;
  else  // if we got here, that means the user gave us an invalid command.
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "Invalid command '%s'.\r\n", keyword) ;
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

    G_runtime->worker[myidx].ts_start = esp_timer_get_time() ;
    G_runtime->worker[myidx].state = W_BUSY ;
    G_runtime->worker[myidx].result_msg[0] = 0 ;        // initialize result
    G_runtime->worker[myidx].result_code = 0 ;          // initialize result
    f_action(myidx) ;
    G_runtime->worker[myidx].state = W_DONE ;

    // update our internal metrics to reflect work we just did

    G_runtime->worker[myidx].cmds_executed++ ;
    G_runtime->worker[myidx].ts_last_cmd = esp_timer_get_time() ;
    G_runtime->worker[myidx].total_busy_ms +=
      (G_runtime->worker[myidx].ts_last_cmd -
       G_runtime->worker[myidx].ts_start) / 1000 ;

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
