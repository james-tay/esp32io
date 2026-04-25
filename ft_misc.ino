/*
   This function is called from "f_user_thread_lifecycle()". Our job is to
   poll an A2D input pin at the specified frequency "poll_ms". If a power pin
   is specified (ie, not "-1"), then it is set HIGH for the lifecycle of this
   user task thread. If the analog value crosses the "hi_thres" or "lo_thres"
   values, we'll publish an MQTT event.

   IMPORTANT - when both a "lo_thres" and "hi_thres" are specified, then an
   MQTT event is published only if there is a "state change". For example,
   if we begin in a "lo" state and the value drifts upward, then an event is
   generated when we cross "hi_thres". If the sensor then drifts down, then an
   event is generated when we cross "lo_thres".
*/

struct td_aread {
  int poll_ms ;                 // analogRead polling frequency
  int in_pin ;                  // input GPIO pin
  int pwr_pin ;                 // optional device power ("-1" to disable)
  int lo_thres ;                // publish an MQTT event if climbing past this
  int hi_thres ;                // publish an MQTT even if dropping below this
  long long next_run ;          // timestamp of our next scheduled poll
} ;
typedef struct td_aread S_td_aread ;

void ft_aread(S_UserThread *self)
{
  #define A_STATE_INIT 0        // boot up state, if we're between lo & hi
  #define A_STATE_LO 1          // value has dipped below "lo_thres"
  #define A_STATE_HI 2          // value has climbed above "hi_thres"

  S_td_aread *td=NULL ;

  // on the first loop, parse our config and set the static variables above

  if (self->loop == 0)
  {
    if (self->num_args < 3)
    {
      strncpy(self->status, "Incorrect arguments", BUF_LEN_UTHREAD_STATUS) ;
      self->state = UTHREAD_STOPPED ;
      return ;
    }

    self->malloc_buf = malloc(sizeof(S_td_aread)) ;
    memset(self->malloc_buf, 0, sizeof(S_td_aread)) ;
    td = (S_td_aread*) self->malloc_buf ;

    td->lo_thres = -1 ;
    td->hi_thres = -1 ;
    td->poll_ms = atoi(self->in_args[0]) ;
    td->in_pin = atoi(self->in_args[1]) ;
    td->pwr_pin = atoi(self->in_args[2]) ;
    if (self->num_args > 3)
      td->lo_thres = atoi(self->in_args[3]) ;
    if (self->num_args > 4)
      td->hi_thres = atoi(self->in_args[4]) ;

    td->next_run = esp_timer_get_time() ;
    self->state = UTHREAD_RUNNING ;

    if (td->poll_ms > DEF_MAX_THREAD_WRAPUP_MSEC / 2)   // limit max poll time
      td->poll_ms = DEF_MAX_THREAD_WRAPUP_MSEC / 2 ;
    if (td->poll_ms < 1)
      td->poll_ms = 1 ;                                 // limit min poll time

    if (td->pwr_pin >= 0)                       // if user specified power pin
    {
      pinMode(td->pwr_pin, OUTPUT) ;
      digitalWrite(td->pwr_pin, HIGH) ;
    }
  }
  td = (S_td_aread*) self->malloc_buf ;

  // on 2nd loop, read our first value because we want "in_pin" to settle

  if (self->loop == 1)
  {
    pinMode(td->in_pin, INPUT) ;
    self->result[0].l_name[0] = "type" ;
    self->result[0].l_data[0] = "value" ;
    self->result[0].i_value = analogRead(td->in_pin) ;
    self->result[0].result_type = UTHREAD_RESULT_INT ;  // expose first result

    self->result[1].l_name[0] = "type" ;
    self->result[1].l_data[0] = "state" ;
    self->result[1].i_value = A_STATE_INIT ;
    self->result[1].result_type = UTHREAD_RESULT_INT ;  // expose input state
  }
  else
  {
    self->result[0].i_value = analogRead(td->in_pin) ;

    // check if any thresholds were crossed

    int event = 0 ;
    if ((td->lo_thres >= 0) &&
        (self->result[0].i_value < td->lo_thres) &&
        (self->result[1].i_value != A_STATE_LO))
    {
      event = 1 ;
      self->result[1].i_value = A_STATE_LO ;
    }
    if ((td->hi_thres >= 0) &&
        (self->result[0].i_value > td->hi_thres) &&
        (self->result[1].i_value != A_STATE_HI))
    {
      event = 1 ;
      self->result[1].i_value = A_STATE_HI ;
    }
    if ((event) && (G_runtime->pubsub_state))   // publish this event on MQTT
    {
      char metric[BUF_LEN_LINE] ;
      char tmp_buf[BUF_LEN_LINE], label_cfg[BUF_LEN_LINE] ;

      snprintf(tmp_buf, BUF_LEN_LINE, "/%s.labels", self->name) ;
      if (f_read_single_line(tmp_buf, label_cfg, BUF_LEN_LINE) < 1)
        label_cfg[0] = 0 ;
      f_render_metric(label_cfg, self->name, &self->result[0], tmp_buf,
                      BUF_LEN_LINE) ;
      snprintf(metric, BUF_LEN_LINE, "%s %d", tmp_buf,
               self->result[0].i_value) ;
      f_mqtt_publish(-1, metric) ;
    }
  }

  // if we've been told to shutdown, turn off "pwr_pin" if user specified

  if ((self->state == UTHREAD_WRAPUP) && (td->pwr_pin >= 0))
  {
    digitalWrite(td->pwr_pin, LOW) ;
    return ;                            // don't bother taking a nap
  }

  // figure out how long to pause before the next poll

  td->next_run = td->next_run + (td->poll_ms * 1000) ;
  long nap_ms = (td->next_run - esp_timer_get_time()) / 1000 ;
  if (nap_ms < 1)
    nap_ms = 1 ;
  snprintf(self->status, BUF_LEN_UTHREAD_STATUS, "nap %ld ms", nap_ms) ;
  delay(nap_ms) ;
}

/*
   This function is called from "f_user_thread_lifecycle()". Our job is to
   poll a GPIO pin and perform a digitalRead. We not only expose metrics, but
   also emit an event if our MQTT subsystem is online. Consider the following
   usage (ie, "/button1.thread")

     ft_dread:0,50,1,-1,1,200

   In the above example, we have a button connecting GPIO1 to ground. We'll
   poll every 50 ms, applying the internal pull-up resistor to maintain a logic
   high for an open circuit. Since this is a passive circuit, we have no power
   pin (ie, "-1"). To suppress false triggering, the button must be held down
   for at least 200ms (ie, 4x polling cycles).

   Note that "ori_state" is initialized once. If initialized low, then
   "triggers" are counted when "in_pin" goes high. If initialized high, then
   "triggers" are counted when "in_pin" goes low.
*/

void ft_dread(S_UserThread *self)
{
  static thread_local int poll_ms=0, in_pin=-1, prev_state=0, ref_state=0 ;
  static thread_local int pwr_pin=-1, pullup=0, thres_ms=0, ori_state=0 ;
  static long long next_run, state_change_time ;

  // on the first loop, parse our config and set the static variables above

  if (self->loop == 0)
  {
    if ((self->num_args < 4) || (self->num_args > 5))
    {
      strncpy(self->status, "Incorrect arguments", BUF_LEN_UTHREAD_STATUS) ;
      self->state = UTHREAD_STOPPED ;
      return ;
    }

    poll_ms = atoi(self->in_args[0]) ;  // how often we'll poll "in_pin"
    in_pin = atoi(self->in_args[1]) ;   // the GPIO pin we digitalRead() on
    pwr_pin = atoi(self->in_args[2]) ;  // the pin powering the peripheral
    pullup = atoi(self->in_args[3]) ;   // whether to apply a pullup voltage
    if (self->in_args[4] != NULL)
      thres_ms = atoi(self->in_args[4]) ; // used to suppress false triggering

    next_run = esp_timer_get_time() ;
    self->state = UTHREAD_RUNNING ;

    if (poll_ms > DEF_MAX_THREAD_WRAPUP_MSEC / 2)       // limit max poll time
      poll_ms = DEF_MAX_THREAD_WRAPUP_MSEC / 2 ;
    if (poll_ms < 1)
      poll_ms = 1 ;                                     // limit min poll time

    if (pullup)
      pinMode(in_pin, INPUT_PULLUP) ;   // use built-in pull up resistor
    else
      pinMode(in_pin, INPUT) ;          // floating input

    if (pwr_pin >= 0)                   // if user specified a power pin
    {
      pinMode(pwr_pin, OUTPUT) ;
      digitalWrite(pwr_pin, HIGH) ;
    }
  }

  // on 2nd loop, initialize "prev_state" because we want "in_pin" to settle

  if (self->loop == 1)
  {
    prev_state = digitalRead(in_pin) ;
    ref_state = prev_state ;
    ori_state = prev_state ;                    // "ori_state" is set ONCE
    state_change_time = esp_timer_get_time() ;

    // configure the metrics we'll expose

    self->result[0].result_type = UTHREAD_RESULT_INT ;
    self->result[0].l_name[0] = "type" ;
    self->result[0].l_data[0] = "state" ;
    self->result[1].result_type = UTHREAD_RESULT_LONGLONG ;
    self->result[1].l_name[0] = "type" ;
    self->result[1].l_data[0] = "transients" ;
    self->result[2].result_type = UTHREAD_RESULT_LONGLONG ;
    self->result[2].l_name[0] = "type" ;
    self->result[2].l_data[0] = "triggers" ;

    self->result[0].i_value = ref_state ;
    if (G_runtime->config.debug)
      Serial.printf("DEBUG: ft_dread() ori_state:%d\r\n.", ori_state) ;
  }
  else
  {
    // check if our state changed. The following conditions may occur,
    // 1. no state change, ie, "cur_state" matches "prev_state". Do nothing.
    // 2. "cur_state" changed, but this may be a transient
    //   a) if we changed back to "ref_state" then this was a transient.
    //   b) if we're not "ref_state", note down "state_change_time".
    // 3. we've been in a new state for some time,
    //   a) emit an event
    //   b) update "ref_state"

    int cur_state = digitalRead(in_pin) ;

    if (cur_state != prev_state)                        // condition 2.
    {
      if (cur_state == ref_state)                       // found a transient
      {
        state_change_time = 0 ;
        self->result[1].ll_value++ ;                    // "transients" metric
      }
      else
        state_change_time = esp_timer_get_time() ;      // note change time
      prev_state = cur_state ;
    }

    if (cur_state != ref_state)
    {
      long long now = esp_timer_get_time() ;
      long long dur = now - state_change_time ;
      if (dur > (thres_ms * 1000))                      // condition 3.
      {
        if (G_runtime->config.debug)
          Serial.printf("DEBUG: ft_dread() state %d->%d\r\n",
                        ref_state, cur_state) ;

        ref_state = cur_state ;
        self->result[0].i_value = cur_state ;           // "state" metric
        if (cur_state != ori_state)
          self->result[2].ll_value++ ;                  // "triggers" metric

        if (G_runtime->pubsub_state)
        {
          char metric[BUF_LEN_LINE] ;
          char tmp_buf[BUF_LEN_LINE], label_cfg[BUF_LEN_LINE] ;

          snprintf(tmp_buf, BUF_LEN_LINE, "/%s.labels", self->name) ;
          if (f_read_single_line(tmp_buf, label_cfg, BUF_LEN_LINE) < 1)
            label_cfg[0] = 0 ;
          f_render_metric(label_cfg, self->name, &self->result[0],
                          tmp_buf, BUF_LEN_LINE) ;
          snprintf(metric, BUF_LEN_LINE, "%s %d", tmp_buf, cur_state) ;
          f_mqtt_publish(-1, metric) ;
        }
      }
    }
  }

  // if we've been told to shutdown, turn off "pwr_pin" if user specified

  if ((self->state == UTHREAD_WRAPUP) && (pwr_pin >= 0))
  {
    digitalWrite(pwr_pin, LOW) ;
    return ;                            // don't bother taking a nap
  }

  // figure out how long to pause before the next poll

  next_run = next_run + (poll_ms * 1000) ;
  long nap_ms = (next_run - esp_timer_get_time()) / 1000 ;
  if (nap_ms < 1)
    nap_ms = 1 ;
  snprintf(self->status, BUF_LEN_UTHREAD_STATUS, "nap %ld ms", nap_ms) ;
  delay(nap_ms) ;
}

/*
   This function is called from "f_user_thread_lifecycle()". This is the user
   tasks thread. Our job is to read commands from a file and execute them by
   dispatching each command to a worker thread. This function is expected to
   only run once. Once all commands in the specified file are complete, our
   job is done. Apart from the standard commands, this function internally
   implements,
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
  if (self->num_args != 3)              // don't run if our params are wrong
  {
    strncpy(self->status, "Incorrect arguments", BUF_LEN_UTHREAD_STATUS) ;
    self->state = UTHREAD_STOPPED ;
    return ;
  }

  if (self->loop == 0)
  {
    self->state = UTHREAD_RUNNING ;             // indicate that we're alive

    // configure the result values we expose

    self->result[0].result_type = UTHREAD_RESULT_LONGLONG ;
    self->result[0].l_name[0] = "last_activity_sec" ;
    self->result[0].l_data[0] = "console" ;
    self->result[1].result_type = UTHREAD_RESULT_LONGLONG ;
    self->result[1].l_name[0] = "last_activity_sec" ;
    self->result[1].l_data[0] = "webserver" ;

    // track my start time in result[2], but don't expose this as a metric

    self->result[2].ll_value = esp_timer_get_time() ; // mark our startup time
  }

  int startup_ignore_secs = atoi(self->in_args[0]) ;
  int check_interval_secs = atoi(self->in_args[1]) ;
  int no_activity_reboot_secs = atoi(self->in_args[2]) ;

  long long now = esp_timer_get_time() ;
  long long my_start_time = self->result[2].ll_value ;
  self->result[0].ll_value = (now - G_runtime->serial_ts_last_read) / 1000000 ;
  self->result[1].ll_value = (now - G_runtime->web_ts_last_accept) / 1000000 ;

  // update our status message to indicate if we're "armed" yet.

  if ((now - my_start_time) / 1000000 > startup_ignore_secs)
  {
    strncpy(self->status, "watchdog armed", BUF_LEN_UTHREAD_STATUS) ;

    // if both console and webserver idle is too large, reboot immediately

    if ((self->result[0].ll_value > no_activity_reboot_secs) &&
        (self->result[1].ll_value > no_activity_reboot_secs))
    {
      Serial.printf("FATAL! Watchdog triggered, rebooting.\r\n") ;
      delay(1000) ;
      ESP.restart() ;
    }
  }
  else
    snprintf(self->status, BUF_LEN_UTHREAD_STATUS, "watchdog idle %d/%d/%d",
             startup_ignore_secs,
             check_interval_secs,
             no_activity_reboot_secs) ;

  delay (check_interval_secs * 1000) ;
}
