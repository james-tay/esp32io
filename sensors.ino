/*
   This function polls a DHT22 at "pin". If successful, the "temperature" and
   "humidity" values are written and this function returns 1, otherwise 0 if
   something went wrong, in which case an error message is written to "err",
   which is expected to be BUF_LEN_ERR bytes long.
*/

int f_sensor_dht22(int dataPin, float *temperature, float *humidity, char *err)
{
  #define DHT22_TIMEOUT_USEC 500

  int cycles[40] ;
  unsigned char data[5] ;

  /* send the trigger to begin a poll cycle */

  pinMode(dataPin, OUTPUT) ;
  digitalWrite(dataPin, LOW) ;          // sensor reset
  delayMicroseconds (1200) ;
  digitalWrite(dataPin, HIGH) ;         // sensor trigger
  delayMicroseconds(20) ;
  digitalWrite(dataPin, LOW) ;          // set low before becoming an input pin
  pinMode(dataPin, INPUT) ;

  /* expect DHT22 to respond with a low and then a high */

  if (pulseIn(dataPin, HIGH) == 0)
  {
    strncpy(err, "no ACK response after sensor trigger", BUF_LEN_ERR) ;
    if (G_runtime->config.debug)
      Serial.printf("DEBUG: f_sensor_dht22() %s.\r\n", err) ;
    return(0) ;
  }

  /* now read 40 bits of data, store pulse timings in an array */

  for (int i=0 ; i<40 ; i++)
    cycles[i] = pulseIn(dataPin, HIGH, DHT22_TIMEOUT_USEC) ;

  /* convert pulse timings timings into humidity/temperature/checksum */

  memset(data, 0, 5) ;
  for (int i=0 ; i<40 ; i++)
  {
    data[i/8] <<= 1 ;           // left shift bits, right most bit will be 0
    if (cycles[i] > 50)
      data[i/8] |= 1 ;          // set right most bit to 1
  }

  /* validate checksum */

  unsigned char c = data[0] + data[1] + data[2] + data[3] ;
  if ((c & 0xff) != data[4])
  {
    strncpy(err, "checksum failed", BUF_LEN_ERR) ;
    if (G_runtime->config.debug)
      Serial.printf("DEBUG: f_sensor_dht22() %s.\r\n", err) ;
    return(0) ;
  }

  float raw_temp = ((word)(data[2] & 0x7F)) << 8 | data[3] ;
  if (data[2] & 0x80)
    *temperature = raw_temp * -0.1 ;
  else
    *temperature = raw_temp * 0.1 ;

  *humidity = float(((int) data[0] << 8 ) | data[1]) / 10.0 ;
  return(1) ;
}

/*
   This function is called from "f_action()", our job is to obtain sensor
   readings by calling "f_sensor_dht22()" and packaging the response.
*/

void f_dht22_cmd(int idx)
{
  char *tokens[2], err[BUF_LEN_ERR] ;
  float temperature=0.0, humidity=0.0 ;

  if (f_parse(G_runtime->worker[idx].cmd, tokens, 2) != 2)
  {
    strncpy(G_runtime->worker[idx].result_msg, "Invalid usage.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }
  err[0] = 0 ;

  if (f_sensor_dht22(atoi(tokens[1]), &temperature, &humidity, err))
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "temperature %.1f, humidity %.1f\r\n", temperature, humidity) ;
    G_runtime->worker[idx].result_code = 200 ;
  }
  else
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "%s\r\n", err) ;
    G_runtime->worker[idx].result_code = 500 ;
  }
}

/*
   This function is called from "f_user_thread_lifecycle()". We are supplied
   with 2x arguments, the DHT22's data pin and power pin. Our job is to power
   on the DHT22, poll it and power it down. Once we have a reading, we expose
   them as metrics for prometheus to scrape.
*/

void ft_dht22(S_UserThread *self)
{
  #define DHT22_MAX_T_DELTA 5.0         // max temperature swing between polls
  #define DHT22_MAX_H_DELTA 20          // max humidity swing between polls
  #define DHT22_MAX_RETRIES 10          // max total polls
  #define DHT22_POWER_ON_DELAY_MS 1200  // based on datasheet section 6
  #define DHT22_POLL_DELAY_MS 2200      // based on datasheet section 7

  static thread_local long long ts_next_run=0 ;

  if (self->num_args != 3)
  {
    strncpy(self->status, "Incorrect arguments", BUF_LEN_UTHREAD_STATUS) ;
    self->state = UTHREAD_STOPPED ;
    return ;
  }
  int dataPin = atoi(self->in_args[0]) ;
  int pwrPin = atoi(self->in_args[1]) ;
  int intervalSecs = atoi(self->in_args[2]) ;

  if (self->loop == 0)                  // setup metrics we'll expose
  {
    self->state = UTHREAD_RUNNING ;
    self->result[0].result_type = UTHREAD_RESULT_FLOAT ;        // temperature
    self->result[0].l_name[0] = "measurement" ;
    self->result[0].l_data[0] = "temperature" ;
    self->result[1].result_type = UTHREAD_RESULT_FLOAT ;        // humidity
    self->result[1].l_name[0] = "measurement" ;
    self->result[1].l_data[0] = "humidity" ;
    self->result[2].result_type = UTHREAD_RESULT_INT ;          // abnormal
    self->result[2].l_name[0] = "readings" ;
    self->result[2].l_data[0] = "abnormal" ;
    ts_next_run = esp_timer_get_time() + (intervalSecs * 1000000) ;
  }

  // if user defined the power pin, boot up the DHT22 now

  long long ts_start = esp_timer_get_time() ;
  if (pwrPin >= 0)
  {
    pinMode(pwrPin, OUTPUT) ;
    digitalWrite(pwrPin, HIGH) ;
    delay(DHT22_POWER_ON_DELAY_MS) ;
  }

  // poll the sensor, if its readings exceed the expected delta, poll it again,
  // if the 2nd set of readings are within the readings from the 1st poll, then
  // we accept them.

  int retries = DHT22_MAX_RETRIES ;
  char err[BUF_LEN_ERR] ;
  float cur_temp, cur_humidity, prev_temp, prev_humidity ;

  while (retries > 0)
  {
    err[0] = 0 ;

    // perform the first poll

    delay(DHT22_POLL_DELAY_MS) ;
    if (f_sensor_dht22(dataPin, &cur_temp, &cur_humidity, err))
    {
      // check against previous results (if we've done this before)

      if ((self->loop > 0) &&
          (fabsf(self->result[0].f_value - cur_temp) < DHT22_MAX_T_DELTA) &&
          (fabsf(self->result[1].f_value - cur_humidity) < DHT22_MAX_H_DELTA))
      {
        self->result[0].f_value = cur_temp ;
        self->result[1].f_value = cur_humidity ;
        break ;                                         // success !!
      }
      else // first loop or readings are above delta, perform a 2nd poll
      {
        prev_temp = cur_temp ;
        prev_humidity = cur_humidity ;
        err[0] = 0 ;
        delay(DHT22_POLL_DELAY_MS) ;
        if ((f_sensor_dht22(dataPin, &cur_temp, &cur_humidity, err)) &&
            (prev_temp - cur_temp < DHT22_MAX_T_DELTA) &&
            (prev_humidity - cur_humidity < DHT22_MAX_H_DELTA))
        {
          self->result[0].f_value = cur_temp ;
          self->result[1].f_value = cur_humidity ;
          break ;                                       // success !!
        }
        else
        {
          self->result[2].i_value++ ;
          if ((G_runtime->config.debug) && (strlen(err) > 0))
            Serial.printf("DEBUG: ft_dht22() err:%s\r\n", err) ;
        }
      }
    }
    else
    {
      self->result[2].i_value++ ;                       // anomaly
      if ((G_runtime->config.debug) && (strlen(err) > 0))
        Serial.printf("DEBUG: ft_dht22() err:%s\r\n", err) ;
    }

    retries-- ;
  }

  if (pwrPin >= 0)
    digitalWrite(pwrPin, LOW) ;

  // if "err" is present then don't expose metrics because they're now stale

  long long ts_end = esp_timer_get_time() ;
  if (strlen(err) > 0)
    snprintf(self->status, BUF_LEN_UTHREAD_STATUS, "%s", err) ;
  else
    snprintf(self->status, BUF_LEN_UTHREAD_STATUS, "polled in %lldms",
             (ts_end - ts_start) / 1000) ;

  // sit here until it's time to run again


}

