/*
  References
  - https://docs.espressif.com/projects/arduino-esp32/en/latest/api/ledc.html
*/

/*
   This function is called from "f_pwm_cmd()". Our job is to set the PWM duty
   cycle to "duty" on "pin". Note that if the resolution for the PWM signal
   is set to 8-bit, then "duty" is 0-255. If resolution is 12-bit then "duty"
   is 0-4095 and so on. If "duty" is -1, then we try reading the currently
   configured duty cycle value.
*/

void f_pwm_duty(int idx, int pin, int duty)
{
  if (duty < 0)                 // just read pin's duty cycle and we're done
  {
    unsigned int value = ledcRead(pin) ;
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "GPIO%d duty cycle %d.\r\n", pin, value) ;
    G_runtime->worker[idx].result_code = 200 ;
    return ;
  }

  // if we're here, that means out job is to set they "duty" cycle value

  if (ledcWrite(pin, duty))
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "GPIO%d duty set to %d.\r\n", pin, duty) ;
    G_runtime->worker[idx].result_code = 200 ;
  }
  else
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "Cannot set GPIO%d duty cycle.\r\n", pin) ;
    G_runtime->worker[idx].result_code = 500 ;
  }
}

/*
   This function is called from "f_pwm_cmd()". Our job is to query the PWM
   subsystem and report its status to the calling worker thread "idx". Note
   that during compile time, the following is set depending on the build
   platform:
     CONFIG_IDF_TARGET_ESP32    - esp32/esp32-cam have hi&lo speed timers
     CONFIG_IDF_TARGET_ESP32S3  - esp32-s3 typically only has lo speed timers
*/

void f_pwm_info(int idx)
{
  char line[BUF_LEN_LINE] ;
  if (G_runtime->pwm_init == 0)
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "PWM not initialized. Max timer resolution %d-bits.\r\n",
              SOC_LEDC_TIMER_BIT_WIDTH) ;
    G_runtime->worker[idx].result_code = 200 ;
    return ;
  }

  #ifdef CONFIG_IDF_TARGET_ESP32
    ledc_mode_t mode = LEDC_HIGH_SPEED_MODE ;
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "%d channels, high(80Mhz) and low(1MHz), max res %d-bits\r\n",
             SOC_LEDC_CHANNEL_NUM, SOC_LEDC_TIMER_BIT_WIDTH) ;
  #else
    ledc_mode_t mode = LEDC_LOW_SPEED_MODE ;
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "%d channels, low(1MHz), max res %d-bits\r\n",
             SOC_LEDC_CHANNEL_NUM, SOC_LEDC_TIMER_BIT_WIDTH) ;
  #endif

  for (int chan=0 ; chan < SOC_LEDC_CHANNEL_NUM ; chan++)
  {
    // note that multiple channels share a single clock generator

    int freq = ledc_get_freq(mode, (ledc_timer_t)((chan / 2) % 4));
    snprintf(line, BUF_LEN_LINE, " channel:%d freq:%d hz\r\n", chan, freq) ;
    strncat(G_runtime->worker[idx].result_msg, line,
            BUF_LEN_WORKER_RESULT - strlen(G_runtime->worker[idx].result_msg)) ;
  }
  G_runtime->worker[idx].result_code = 200 ;
}

/*
   This function is called from "f_pwm_cmd()". Our job is to shutdown and
   release the PWM resources assciated with "pin".
*/

void f_pwm_off(int idx, int pin)
{
  if (ledcDetach(pin))
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "PWM on GPIO%d shutdown\r\n", pin) ;
    G_runtime->worker[idx].result_code = 200 ;
  }
  else
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "Could not shutdown PWM on GPIO%d\r\n", pin) ;
    G_runtime->worker[idx].result_code = 500 ;
  }
}

/*
   This function is called from "f_pwm_cmd()". Our job is to initialize the
   PWM subsystem for "pin" at "freq_hz" at resolution "res" (1-20 bits).
*/

void f_pwm_on(int idx, int pin, int freq_hz, int res)
{
  if (ledcAttach(pin, freq_hz, res))
  {
    G_runtime->pwm_init = 1 ;
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "GPIO%d configured\r\n", pin) ;
    G_runtime->worker[idx].result_code = 200 ;
  }
  else
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "Could not setup GPIO%d\r\n", pin) ;
    G_runtime->worker[idx].result_code = 500 ;
  }
}

/*
   We're called from "f_action()" by the worker thread "idx" when the user
   wants to manage (hardware) PWM. Parse the command and proceed accordingly.
*/

void f_pwm_cmd(int idx)
{
  char *tokens[5], *cmd=NULL ;
  int count = f_parse(G_runtime->worker[idx].cmd, tokens, 6) ;

  if (count==1)
  {
    strncpy(G_runtime->worker[idx].result_msg,
            "pwm duty <pin> [duty]              get or set pin's duty cycle\r\n"
            "pwm info                           print current PWM status\r\n"
            "pwm off <pin>                      turn off PWM on a pin\r\n"
            "pwm on <pin> <freq_hz> <res>       turn on PWM on a pin\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }
  cmd = tokens[1] ;

  if ((strcmp(cmd, "duty") == 0) && (tokens[2]))
  {
    int duty=-1 ;
    if (tokens[3])
      duty = atoi(tokens[3]) ;
    f_pwm_duty(idx, atoi(tokens[2]), duty) ;
  }
  else  
  if (strcmp(cmd, "info") == 0)
    f_pwm_info(idx) ;
  else
  if ((strcmp(cmd, "off") == 0) && (tokens[2]))
    f_pwm_off(idx, atoi(tokens[2])) ;
  else
  if ((strcmp(cmd, "on") == 0) && (tokens[2]) && (tokens[3]) && (tokens[4]))
    f_pwm_on(idx, atoi(tokens[2]), atoi(tokens[3]), atoi(tokens[4])) ;
  else
  {
    strncpy(G_runtime->worker[idx].result_msg, "Invalid command.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
  }
}
