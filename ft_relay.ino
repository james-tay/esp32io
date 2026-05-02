/*
   This function is called from "f_action()". If no arguments are provided,
   we print the help message, otherwise we identify the "ft_relay()" thread
   specified and directly modify its "result[0].i_value" to manage relay state.
   In addition, we update its "result[2].ll_value" to indicate the time of the
   "on" command.
*/

void f_relay_cmd(int idx)
{
  // parse our "relay..." command, or print help

  char *tokens[3], *name=NULL, *action=NULL ;
  if (f_parse(G_runtime->worker[idx].cmd, tokens, 3) != 3)
  {
    strncpy(G_runtime->worker[idx].result_msg,
            "<relay_thread> on          turn on a relay\r\n"
            "<relay_thread> off         turn off a relay\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }
  name = tokens[1] ;
  action = tokens[2] ;

  // attempt to locate an "ft_relay()" instance with "name" that is running

  int slot ;
  for (slot=0 ; slot < DEF_MAX_USER_THREADS ; slot++)
    if ((G_runtime->utask[slot].state == UTHREAD_RUNNING) &&
        (G_runtime->utask[slot].ft_addr == ft_relay) &&
        (strcmp(G_runtime->utask[slot].name, name) == 0))
      break ;                                                   // found it !

  if (slot == DEF_MAX_USER_THREADS)
  {
    strncpy(G_runtime->worker[idx].result_msg, "No such relay instance\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }

  // if relay is in a fault state, don't proceed

  if (G_runtime->utask[slot].result[0].i_value < 0)
  {
    strncpy(G_runtime->worker[idx].result_msg, "Relay in a fault state.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }

  // handle the supplied "on" or "off" action

  if (strcmp(action, "on") == 0)
  {
    if (G_runtime->utask[slot].result[0].i_value == 1)
    {
      strncpy(G_runtime->worker[idx].result_msg, "Already on",
              BUF_LEN_WORKER_RESULT) ;
      G_runtime->worker[idx].result_code = 200 ;
    }
    else
    {
      strncpy(G_runtime->worker[idx].result_msg, "Turning on",
              BUF_LEN_WORKER_RESULT) ;
      G_runtime->worker[idx].result_code = 200 ;
      G_runtime->utask[slot].result[0].i_value = 1 ;
    }
    G_runtime->utask[slot].result[2].ll_value = esp_timer_get_time() ;
  }
  else
  if (strcmp(action, "off") == 0)
  {
    if (G_runtime->utask[slot].result[0].i_value == 0)
    {
      strncpy(G_runtime->worker[idx].result_msg, "Already off",
              BUF_LEN_WORKER_RESULT) ;
      G_runtime->worker[idx].result_code = 400 ;
    }
    else
    {
      strncpy(G_runtime->worker[idx].result_msg, "Turning off",
              BUF_LEN_WORKER_RESULT) ;
      G_runtime->worker[idx].result_code = 200 ;
      G_runtime->utask[slot].result[0].i_value = 0 ;
    }
  }
  else
  {
    strncpy(G_runtime->worker[idx].result_msg, "Invalid action\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }
}

/*
   The following functions implement the electrical actions to turn a relay
   on or off at the specified GPIO pin. They're called from "ft_relay()".
*/

void f_relay_on(int pin)
{
  pinMode(pin, OUTPUT) ;
  digitalWrite(pin, LOW) ;
}

void f_relay_off(int pin)
{
  pinMode(pin, INPUT) ;
}

/*
   This thread implements safe relay control. This thread controls the GPIO
   pin which controls the relay and must be running before calling the "relay"
   command to turn it on/off. Specifically, the desired relay state is
   indicated by this thread's "self->result[0].i_value". Thus the "relay
   command directly manipulates this "i_value", and this thread performs the
   necessary actions.

   This code has been tested on active low relay modules (without optocoupler).
   During typical operation, the relay module's "IN" pin is naturally high and
   needs to be pulled low to activate the relay. To deactivate the relay, the
   "IN" pin must go high. However, the 3.3v output from an ESP32 might not be
   high enough to match the relay module's 5v supply. Even if the "IN" pin is
   configured as an input pin, the voltage rises up to 3.8v, which is not high
   enough to deactivate the relay. To workaround this, place an LED with its
   anode connected to the GPIO pin.

   When the GPIO pin is high (ie, 3.2v), the voltage across the LED is 1.2v,
   and is insufficient for a forward bias, thus the LED is off and effectively
   has a massive resistance and its cathode rises to 4.4v, this deactivates the
   relay.

   When the GPIO pin is low, then current flows through the LED, the voltage
   at the cathode is now low, at about 1.7v, which is low enough to activate
   the relay.

   When the GPIO pin is configured as an input, its impedence goes high, and
   its voltage is 3v. But on the LED's cathode, it is 4.4v, which deactivates
   the relay.
*/

struct td_relay {
  int pin ;             // GPIO pin which controls the relay
  int cur_state ;       // whether the relay is currently on
  long long timeout_secs ; // turn off if not (re)commanded on within this time
} ;
typedef struct td_relay S_td_relay ;

void ft_relay(S_UserThread *self)
{
  #define RELAY_DELAY_MSEC 100                  // how often we check status

  S_td_relay *td=NULL ;

  if (self->loop == 0)
  {
    if (self->num_args != 2)
    {
      strncpy(self->status, "Incorrect arguments", BUF_LEN_UTHREAD_STATUS) ;
      self->state = UTHREAD_STOPPED ;
      return ;
    }

    self->malloc_buf = malloc(sizeof(S_td_relay)) ;
    if (self->malloc_buf == NULL)
    {
      strncpy(self->status, "malloc failed", BUF_LEN_UTHREAD_STATUS) ;
      self->state = UTHREAD_STOPPED ;
      return ;
    }
    memset(self->malloc_buf, 0, sizeof(S_td_relay)) ;
    td = (S_td_relay*) self->malloc_buf ;

    td->pin = atoi(self->in_args[0]) ;          // GPIO pin to control relay
    td->timeout_secs = atoi(self->in_args[1]) ; // time out to deactivate relay
    td->cur_state = 0 ;                         // initial state must be off
    f_relay_off(td->pin) ;                      // pin state at power on

    // note that "result[0].i_value" is updated by "f_relay_cmd()".

    self->result[0].l_name[0] = "relay" ;
    self->result[0].l_data[0] = "state" ;
    self->result[0].result_type = UTHREAD_RESULT_INT ;
    self->result[0].i_value = 0 ;                       // 1=on 0=off -1=fault
    self->result[1].l_name[0] = "timeout_secs" ;
    self->result[1].l_data[0] = "remaining" ;
    self->result[1].result_type = UTHREAD_RESULT_INT ;
    self->result[1].i_value = 0 ;                       // auto off time left
    self->result[2].l_name[0] = "ts_last_command" ;
    self->result[2].l_data[0] = "on" ;
    self->result[2].result_type = UTHREAD_RESULT_LONGLONG ;
    self->result[2].ll_value = 0 ;                      // last "on" command

    self->state = UTHREAD_RUNNING ;
    snprintf(self->status, BUF_LEN_UTHREAD_STATUS, "pin:%d timeout_secs:%lld",
             td->pin, td->timeout_secs) ;

    if (G_runtime->config.debug)
      Serial.printf("DEBUG: ft_relay() pin:%d started.\r\n", td->pin) ;
  }
  td = (S_td_relay*) self->malloc_buf ;

  // if we've been told to shutdown, turn off the relay first

  if (self->state == UTHREAD_WRAPUP)
  {
    if (G_runtime->config.debug)
      Serial.printf("DEBUG: ft_relay() wrapup, relay off.\r\n") ;
    f_relay_off(td->pin) ;
    self->state = UTHREAD_STOPPED ;
    return ;
  }

  // if we're off but have been commanded on

  if ((td->cur_state == 0) && (self->result[0].i_value == 1))
  {
    f_relay_on(td->pin) ;
    td->cur_state = 1 ;
  }

  // if we're on but have been commanded off

  if ((td->cur_state == 1) && (self->result[0].i_value == 0))
  {
    f_relay_off(td->pin) ;
    td->cur_state = 0 ;
    self->result[1].i_value = 0 ;               // disable auto off time left
  }

  // if we're on, check if it's time to automatically turn off (ie, fault)

  if (td->cur_state == 1)
  {
    long long now = esp_timer_get_time() ;
    long long off_time = self->result[2].ll_value +
                         (td->timeout_secs * 1000000) ;
    long long remaining = (off_time - now) / 1000000 ;
    if (remaining < 0)                          // enter fault state
    {
      f_relay_off(td->pin) ;
      td->cur_state = -1 ;
      self->result[0].i_value = -1 ;            // relay state (ie, fault)
      self->result[1].i_value = -1 ;            // auto off time left
      strncpy(self->status, "Fault state", BUF_LEN_UTHREAD_STATUS) ;
      if (G_runtime->config.debug)
        Serial.printf("DEBUG: ft_relay() '%s' in a fault state.\r\n",
                      self->name) ;
    }
    else
      self->result[1].i_value = remaining ;
  }

  delay(RELAY_DELAY_MSEC) ;
}
