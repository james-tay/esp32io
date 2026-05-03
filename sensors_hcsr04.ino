
#define HCSR04_TIMEOUT_USEC 60000       // max time to wait for a response
#define HCSR04_POLL_DELAY_MS 20         // time interval between polls
#define HCSR04_MAX_SAMPLES 10           // max number of samples per poll

/*
   Returns the distance (in cm) measured by an HC-SR04 ultrasonic range sensor
   or -1.0 if it was unable to take a reading. Note that according to the
   datasheet, this sensor's operating range is between 2cm and 400cm, so we
   return -1.0 if we get a response outside of this range.
*/

float f_hcsr04 (int trig_pin, int echo_pin)
{
  pinMode(trig_pin, OUTPUT) ;
  pinMode(echo_pin, INPUT) ;

  /* set trigger pin low, then stay high for 10 usec */

  digitalWrite(trig_pin, LOW) ;
  delayMicroseconds (1000) ;
  digitalWrite (trig_pin, HIGH) ;
  delayMicroseconds (10) ;
  digitalWrite (trig_pin, LOW) ;

  unsigned long echo_usecs = pulseIn(echo_pin, HIGH, HCSR04_TIMEOUT_USEC) ;
  if (echo_usecs == 0)
    return(-1.0) ;                              // no response from sensor
  else
  {
    float cm = float(echo_usecs) / 58.0 ;       // convert time to centimeters
    if ((cm < 2.0) || (cm > 400.0))
      return(-1.0) ;                            // probably an invalid reading
    return (cm) ;
  }
}

/*
   This function is called from "f_action()" when a worker thread wakes up to
   process this command. Our job is to manage the user command, which involves
   printing the help message or parsing the command arguments.
*/

void f_hcsr04_cmd(int idx)
{
  // parse the "hcsr04..." command, or print help

  char *tokens[4] ;
  int trig_pin, echo_pin, samples, count, i ;
  if (f_parse(G_runtime->worker[idx].cmd, tokens, 4) != 4)
  {
    strncpy(G_runtime->worker[idx].result_msg,
            "hcsr04 <trigPin> <echoPin> <samples>\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }
  trig_pin = atoi(tokens[1]) ;
  echo_pin = atoi(tokens[2]) ;
  samples = atoi(tokens[3]) ;

  if (samples > HCSR04_MAX_SAMPLES)
    samples = HCSR04_MAX_SAMPLES ;
  if (samples < 1)
    samples = 1 ;

  float cur_val, min_val=0, max_val=0, ave_val=0 ;
  for (i=0 ; i < samples ; i++)
  {
    cur_val = f_hcsr04(trig_pin, echo_pin) ;
    if (i == 0)
    {
      count = 1 ;
      min_val = cur_val ;
      max_val = cur_val ;
      ave_val = cur_val ;
    }
    else
    if (cur_val != -1.0)
    {
      count++ ;
      ave_val = ave_val + cur_val ;
      if (cur_val < min_val)
        min_val = cur_val ;
      if (cur_val > max_val)
        max_val = cur_val ;
    }
    delay(HCSR04_POLL_DELAY_MS) ;
  }
  ave_val = ave_val / (float) count ;
  snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
           "%d samples - min:%.02f ave:%.02f max:%.02f cm\r\n",
           count, min_val, ave_val, max_val) ;
  G_runtime->worker[idx].result_code = 200 ;
}

