/*
   This is the user tasks thread. Our job is to read commands from a file and
   execute them by dispatching each command to a worker thread. This function
   is expected to only run once. Once all commands in the specified file are
   complete, our job is done. Apart from the standard commands, this function
   internally implements,
     delay_ms <msecs>           pause execution for specified time
*/

void ft_utasks(S_UserThread *self)
{
  int my_idx, amt, tid, cmds=0 ;
  char *cmd_file, *tokens[2], *cur_cmd, *end, *p ;
  char cmd_buf[BUF_LEN_UTASK_FILESIZE] ;

  self->state = UTHREAD_RUNNING ;
  if (self->num_args != 1)
  {
    strncpy(self->status, "Incorrect arguments", BUF_LEN_UTHREAD_STATUS) ;
    self->state = UTHREAD_STOPPED ;
    return ;
  }
  cmd_file = self->in_args[0] ;

  // find our index number in "G_runtime->utask[]"

  for (my_idx=0 ; my_idx < DEF_MAX_USER_THREADS ; my_idx++)
    if (&G_runtime->utask[my_idx] == self)
      break ;
  if (my_idx == DEF_MAX_USER_THREADS)
  {
    strncpy(self->status, "Cannot find my_idx", BUF_LEN_UTHREAD_STATUS) ;
    self->state = UTHREAD_STOPPED ;
    return ;
  }

  // try read commands from the user specified file, then split up commands

  if (f_read_whole(cmd_file, cmd_buf, BUF_LEN_UTASK_FILESIZE) < 1)
  {
    snprintf(self->status, BUF_LEN_UTHREAD_STATUS,
             "Cannot read %s", cmd_file) ;
    self->state = UTHREAD_STOPPED ;
    return ;
  }

  long long ts_start = esp_timer_get_time() ;
  cur_cmd = strtok_r(cmd_buf, ";", &p) ;
  while (cur_cmd != NULL)
  {
    // do a "trim()" on "cur_cmd", ie, remove white space

    while ((strlen(cur_cmd) > 0) && (isspace((char)*cur_cmd)))
      cur_cmd++ ;
    end = cur_cmd + strlen(cur_cmd) - 1 ;
    while ((end > cur_cmd) && (isspace((char)*end)))
    {
      *end = 0 ;
      end-- ;
    }

    if (strlen(cur_cmd) > 0)
    {
      cmds++ ;
      if ((strncmp(cur_cmd, "delay_ms", 8) == 0) &&
          (f_parse(cur_cmd, tokens, 2) == 2))           // "delay_ms" command
      {
        if (G_runtime->config.debug)
          Serial.printf("DEBUG: ft_utasks() cur_cmd(%s)(%s)\r\n",
                        tokens[0], tokens[1]) ;
        delay(atoi(tokens[1])) ;
      }
      else                                              // handle command
      {
        tid = f_get_next_worker() ;
        G_runtime->worker[tid].caller = DEF_UTHREAD_CALLER_OFFSET + my_idx ;
        G_runtime->worker[tid].cmd = cur_cmd ;
        xTaskNotifyGive(G_runtime->worker[tid].w_handle) ;      // wake worker
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY) ;               // wait here

        // if we're here, that means worker "tid" has completed "cur_cmd"

        if (G_runtime->config.debug)
          Serial.printf("DEBUG: ft_utasks() cur_cmd(%s) -> %d:%s", cur_cmd,
                        G_runtime->worker[tid].result_code,
                        G_runtime->worker[tid].result_msg) ;

        G_runtime->worker[tid].cmd = NULL ;             // unset "cmd"
        G_runtime->worker[tid].state = W_IDLE ;         // release worker
      }
    }
    cur_cmd = strtok_r(NULL, ";", &p) ;                 // move to next command
  }

  long long ts_end = esp_timer_get_time() ;
  snprintf(self->status, BUF_LEN_UTHREAD_STATUS, "ran %d cmds in %d ms.",
           cmds, (ts_end - ts_start) / 1000) ;
  self->state = UTHREAD_STOPPED ;                       // we're all done
}

/*
   This is a watchdog function. Our job is to detect that something might have
   gone wrong and trigger a reload. The primary means of determining this is
   lack of response on both serial console and webserver traffic.
*/

void ft_wd(S_UserThread *self)
{
  static thread_local long long my_start_time=0 ;

  if (self->num_args != 3)              // don't run if our params are wrong
  {
    strncpy(self->status, "Incorrect arguments", BUF_LEN_UTHREAD_STATUS) ;
    self->state = UTHREAD_STOPPED ;
    return ;
  }

  if (self->loop == 0)
  {
    self->state = UTHREAD_RUNNING ;             // indicate that we're alive
    my_start_time = esp_timer_get_time() ;      // mark our startup time

    // configure the result values we expose

    self->result[0].result_type = UTHREAD_RESULT_LONGLONG ;
    self->result[0].l_name[0] = "last_activity_sec" ;
    self->result[0].l_data[0] = "console" ;
    self->result[1].result_type = UTHREAD_RESULT_LONGLONG ;
    self->result[1].l_name[0] = "last_activity_sec" ;
    self->result[1].l_data[0] = "webserver" ;
  }

  int startup_ignore_secs = atoi(self->in_args[0]) ;
  int check_interval_secs = atoi(self->in_args[1]) ;
  int no_activity_reboot_secs = atoi(self->in_args[2]) ;

  long long now = esp_timer_get_time() ;
  self->result[0].ll_value = (now - G_runtime->serial_ts_last_read) / 1000000 ;
  self->result[1].ll_value = (now - G_runtime->web_ts_last_accept) / 1000000 ;

  if ((now - my_start_time) / 1000000 > startup_ignore_secs)
    strncpy(self->status, "watchdog running", BUF_LEN_UTHREAD_STATUS) ;
  else
    snprintf(self->status, BUF_LEN_UTHREAD_STATUS, "watchdog idle %d/%d/%d",
             startup_ignore_secs,
             check_interval_secs,
             no_activity_reboot_secs) ;

  // if both console and webserver idle is too large, reboot immediately

  if ((self->result[0].ll_value > no_activity_reboot_secs) &&
      (self->result[1].ll_value > no_activity_reboot_secs))
  {
    Serial.printf("FATAL! Watchdog triggered, rebooting.\r\n") ;
    delay(1000) ;
    ESP.restart() ;
  }

  delay (check_interval_secs * 1000) ;
}
