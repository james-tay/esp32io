/*
   This is the user tasks thread. Our job is to read commands from a file and
   execute them by dispatching each command to a worker thread. This function
   is expected to only run once. Once all commands in the specified file are
   complete, our job is done.
*/

void ft_utasks(S_UserThread *self)
{
  char cmd_buf[BUF_LEN_UTASK_FILESIZE] ;        // all commands we'll run

  self->state = UTHREAD_RUNNING ;
  if (G_runtime->config.debug)
    Serial.printf("DEBUG: ft_utasks()\r\n") ;

  if (self->num_args != 1)
  {
    strncpy(self->status, "Incorrect arguments", BUF_LEN_UTHREAD_STATUS) ;
    self->state = UTHREAD_STOPPED ;
    return ;
  }
  char *cmd_file = self->in_args[0] ;

  // try read commands from the user specified file.

  int amt = f_read_whole(cmd_file, cmd_buf, BUF_LEN_UTASK_FILESIZE) ;





  self->state = UTHREAD_STOPPED ;
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
