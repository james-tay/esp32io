#define DHT22_TIMEOUT_USEC 500          // expected pulseIn response time
#define DHT22_MAX_T_DELTA 5.0           // max temperature swing between polls
#define DHT22_MAX_H_DELTA 20            // max humidity swing between polls
#define DHT22_MAX_RETRIES 10            // max total polls
#define DHT22_POWER_ON_DELAY_MS 1200    // based on datasheet section 6
#define DHT22_POLL_DELAY_MS 2200        // based on datasheet section 7

#define DS18B20_MAX_PER_BUS 8           // max devices per GPIO pin
#define DS18B20_POLL_DELAY 800          // based on datasheet page 1
#define DS18B20_POWER_ON_DELAY_MS 20    // wait for voltage to stabilize
#define DS18B20_TEMP_MAX 125.0          // maximum valid temperature reading
#define DS18B20_TEMP_MIN -55.0          // minimum valid temperature reading

/*
   This function polls a DHT22 at "pin". If successful, the "temperature" and
   "humidity" values are written and this function returns 1, otherwise 0 if
   something went wrong, in which case an error message is written to "err",
   which is expected to be BUF_LEN_ERR bytes long.
*/

int f_sensor_dht22(int dataPin, float *temperature, float *humidity, char *err)
{
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
   them as metrics for prometheus to scrape. This thread uses/exposes the
   following results,
     result[0].f_value  - temperature
     result[1].f_value  - humidity
     result[2].i_value  - abnormal readings, eg, checksum failed
     result[3].ll_value - (internal use only) timestamp of next run
*/

void ft_dht22(S_UserThread *self)
{
  if (self->num_args != 3)      // don't run if we're called with bad arguments
  {
    strncpy(self->status, "Incorrect arguments", BUF_LEN_UTHREAD_STATUS) ;
    self->state = UTHREAD_STOPPED ;
    return ;
  }
  int dataPin = atoi(self->in_args[0]) ;
  int pwrPin = atoi(self->in_args[1]) ;
  int intervalSecs = atoi(self->in_args[2]) ;

  if (self->loop == 0) // setup metrics we'll expose AFTER successful polling
  {
    self->state = UTHREAD_RUNNING ;
    self->result[0].l_name[0] = "measurement" ;
    self->result[0].l_data[0] = "temperature" ;
    self->result[1].l_name[0] = "measurement" ;
    self->result[1].l_data[0] = "humidity" ;
    self->result[2].l_name[0] = "readings" ;
    self->result[2].l_data[0] = "abnormal" ;
    self->result[3].ll_value = esp_timer_get_time() ; // internal use only !!
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

  // update thread's status, then sit here until it's time to run again.
  // recall that "result[3].ll_value" is the timestamp of our next run.

  long long ts_end = esp_timer_get_time() ;
  self->result[3].ll_value = self->result[3].ll_value +
                             (intervalSecs * 1000000) ;
  long long nap_ms = (self->result[3].ll_value - ts_end) / 1000 ;

  if (strlen(err) > 0)          // something went wrong, don't expose results
  {
    snprintf(self->status, BUF_LEN_UTHREAD_STATUS, "%s, retry in %lld ms",
             err, nap_ms) ;
    self->result[0].result_type = UTHREAD_RESULT_NONE ;
    self->result[1].result_type = UTHREAD_RESULT_NONE ;
    self->result[2].result_type = UTHREAD_RESULT_NONE ;
  }
  else                          // data is good, expose our results
  {
    snprintf(self->status, BUF_LEN_UTHREAD_STATUS,
             "polled in %lldms, nap %lld ms",
             (ts_end - ts_start) / 1000, nap_ms) ;
    self->result[0].result_type = UTHREAD_RESULT_FLOAT ;
    self->result[1].result_type = UTHREAD_RESULT_FLOAT ;
    self->result[2].result_type = UTHREAD_RESULT_INT ;
  }

  if (nap_ms > 0)
    delay(nap_ms) ;             // pause until it's time to be called again
}

/*
   This function searches "pin" for DS18B20 devices, up to DS18B20_MAX_PER_BUS
   will be searched. Their readings will be written into the "t_values" array
   and their OneWire addresses written into the "addresses" array. The actual
   number of devices discovered is returned.
*/

int f_sensor_ds18b20(int pin, float *t_values, unsigned char *addrs)
{
  int count=0 ;
  unsigned char dev[8], data[9], *addr_ptr = addrs ;

  OneWire bus(pin) ;
  bus.reset() ;
  bus.reset_search() ;

  while ((bus.search(dev)) && (count < DS18B20_MAX_PER_BUS))
    if (dev[0] == 0x28) // DS18B20 units have 0x28 as their first addr byte
    {
      memcpy(addr_ptr, dev, 8) ;
      addr_ptr = addr_ptr + 8 ;
      count++ ;
    }

  addr_ptr = addrs ;
  for (int idx=0 ; idx < count ; idx++)
  {
    memcpy(dev, addr_ptr, 8) ;
    bus.reset() ;               // a reset "wakes" each device on the bus
    bus.select(dev) ;
    bus.write(0x44) ;           // start temperature conversion to scratch pad
    delay(DS18B20_POLL_DELAY) ;
    bus.reset() ;               // must perform a reset before next command
    bus.select(dev) ;
    bus.write(0xBE) ;           // read scratch pad
    for (int i=0 ; i < 9 ; i++)
      data[i] = bus.read() ;

    // temperature MSB and LSB are in the first 2 bytes

    short raw = (data[1] << 8) | data[0] ;
    float cur = (float) raw / 16.0 ;

    // make sure reading is within the valid range, otherwise mark this sensor
    // as invalid by setting the first byte of its address to '0'.

    if ((cur >= DS18B20_TEMP_MIN) && (cur <= DS18B20_TEMP_MAX))
      t_values[idx] = cur ;
    else
    {
      t_values[idx] = 0.0 ;
      addr_ptr[0] = 0 ;                 // make device's address invalid
      if (G_runtime->config.debug)
        Serial.printf("DEBUG: Invalid temperature %f from sensor%d.\r\n",
                      cur, idx) ;
    }
    addr_ptr = addr_ptr + 8 ;           // move on to next sensor's address
  }
  return(count) ;
}

/*
   This function is called from "f_action()", our job is to obtain readings
   from one or more DS18B20 sensors on a certain GPIO pin by calling the
   "f_sensor_ds18b20()" function, and then packaging the response.
*/

void f_ds18b20_cmd(int idx)
{
  char *tokens[2], buf[BUF_LEN_LINE], dev[8] ;
  unsigned char addrs[DS18B20_MAX_PER_BUS * 8] ; // 8x hex bytes per device
  float temperatures[DS18B20_MAX_PER_BUS] ;

  if (f_parse(G_runtime->worker[idx].cmd, tokens, 2) != 2)
  {
    strncpy(G_runtime->worker[idx].result_msg, "Invalid usage.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }
  memset(addrs, 0, DS18B20_MAX_PER_BUS * 17) ;
  int total = f_sensor_ds18b20(atoi(tokens[1]), temperatures, addrs) ;
  for (int i=0 ; i < total ; i++)
  {
    memcpy(dev, addrs + (i*8), 8) ;
    snprintf(buf, BUF_LEN_LINE, "%02x%02x%02x%02x%02x%02x%02x%02x -> %fC\r\n",
             dev[0], dev[1], dev[2], dev[3], dev[4], dev[5], dev[6], dev[7],
             temperatures[i]) ;
    strncat(G_runtime->worker[idx].result_msg, buf,
            BUF_LEN_WORKER_RESULT -
            strlen(G_runtime->worker[idx].result_msg) - 1) ;
  }
  if (total > 0)
    G_runtime->worker[idx].result_code = 200 ;
  else
    G_runtime->worker[idx].result_code = 500 ;
}

/*
   This function is called from "f_user_thread_lifecycle()". We are supplied
   with the data pin and power pin arguments. Our job is to manage power and
   call "f_sensor_ds18b20()" to perform the actual work. The first "result"
   in this thread is the number of sensor read faults. Each subsequent "result"
   is then a sensor reading.
*/

struct td_ds18b20 {
  int dataPin ;                                 // one wire bus
  int pwrPin ;                                  // power to sensors
  int intervalSecs ;                            // how often we poll
  int total_sensors ;                           // total sensors found on bus
  long long ts_next_run ;                       // time of next intended run
  char addr_buf[DS18B20_MAX_PER_BUS * (16 + 1)] ; // buffer for dev addresses
} ;
typedef struct td_ds18b20 S_td_ds18b20 ;

void ft_ds18b20(S_UserThread *self)
{
  S_td_ds18b20 *td=NULL ;       // thread local data

  if (self->num_args != 3)      // don't run if we're called with bad arguments
  {
    strncpy(self->status, "Incorrect arguments", BUF_LEN_UTHREAD_STATUS) ;
    self->state = UTHREAD_STOPPED ;
    return ;
  }

  if (self->loop == 0)
  {
    // prepare thread local data

    self->malloc_buf = malloc(sizeof(S_td_ds18b20)) ;
    if (self->malloc_buf == NULL)
    {
      strncpy(self->status, "malloc failed", BUF_LEN_UTHREAD_STATUS) ;
      self->state = UTHREAD_STOPPED ;
      return ;
    }
    memset(self->malloc_buf, 0, sizeof(S_td_ds18b20)) ;

    td = (S_td_ds18b20*) self->malloc_buf ;
    td->dataPin = atoi(self->in_args[0]) ;
    td->pwrPin = atoi(self->in_args[1]) ;
    td->intervalSecs = atoi(self->in_args[2]) ;
    td->ts_next_run = esp_timer_get_time() ;

    // setup thread's first result only ... which is a faults counter

    self->result[0].l_name[0] = "read" ;
    self->result[0].l_data[0] = "faults" ;
    self->result[0].result_type = UTHREAD_RESULT_INT ;

    self->state = UTHREAD_RUNNING ;
  }

  td = (S_td_ds18b20*) self->malloc_buf ;
  long long ts_start = esp_timer_get_time() ;
  float temperatures[DS18B20_MAX_PER_BUS] ;
  unsigned char addrs[DS18B20_MAX_PER_BUS * 8] ; // 8x hex bytes per device

  // "addrs" is a temporary buffer we pass to "f_sensor_ds18b20()", while
  // "td->addr_buf" holds the (hex) string representation for addresses of all
  // possible sensors. This space is used to populate "l_data" in each of the
  // results we'll expose. Thus, the "addr_buf[]" has the format,
  //   [<1st_dev_16chars>0x00][<2nd_dev_16chars>0x00]...

  int addr_size = (16 + 1) * DS18B20_MAX_PER_BUS ;
  char *addr_ptr=td->addr_buf ;
  unsigned char dev[8] ;        // buffer to help us sprintf() dev addr

  if (td->pwrPin >= 0)
  {
    pinMode(td->pwrPin, OUTPUT) ;
    digitalWrite(td->pwrPin, HIGH) ;
    delay(DS18B20_POWER_ON_DELAY_MS) ;
  }

  // poll the one-wire bus, then expose whatever devices we found

  int total = f_sensor_ds18b20(td->dataPin, temperatures, addrs) ;
  if (total > td->total_sensors)
    td->total_sensors = total ;
  if ((self->loop > 1) && (total < td->total_sensors))
  {
    self->result[0].i_value++ ;                 // read fault occured
    if (G_runtime->config.debug)
      Serial.printf("DEBUG: ft_ds18b20() found %d devices, expecting %d.\r\n",
                    total, td->total_sensors) ;
  }

  for (int i=0 ; i < total ; i++)
  {
    addr_ptr = td->addr_buf + (i*16) + i ; // a location within "addr_buf"
    memcpy(dev, addrs + (i*8), 8) ;
    sprintf(addr_ptr, "%02x%02x%02x%02x%02x%02x%02x%02x",
            dev[0], dev[1], dev[2], dev[3], dev[4], dev[5], dev[6], dev[7]) ;

    // make sure sensor's address begins with 0x28, otherwise it is invalid

    if (dev[0] != 0x28)
    {
      self->result[0].i_value++ ;               // read an invalid sensor
      if (G_runtime->config.debug)
        Serial.printf("DEBUG: ft_ds18b20() invalid sensor address %s.\r\n",
                      addr_ptr) ;
    }
    else
    {
      self->result[i+1].l_name[0] = "address" ;
      self->result[i+1].l_data[0] = addr_ptr ;
      self->result[i+1].f_value = temperatures[i] ;
      self->result[i+1].result_type = UTHREAD_RESULT_FLOAT ;
    }
  }

  // if somehow we have fewer devices this time, don't expose stale metrics

  for (int i=total+1 ; i < DEF_MAX_THREAD_RESULTS ; i++)
    self->result[i].result_type = UTHREAD_RESULT_NONE ;

  // power down the device(s) and then figure out how long to nap for

  if (td->pwrPin >= 0)
    digitalWrite(td->pwrPin, LOW) ;

  long long ts_end = esp_timer_get_time() ;
  td->ts_next_run = td->ts_next_run + (td->intervalSecs * 1000000) ;
  long long nap_ms = (td->ts_next_run - ts_end) / 1000 ;
  snprintf(self->status, BUF_LEN_UTHREAD_STATUS,
           "polled %d devices in %lldms, nap %lld ms",
           total, (ts_end - ts_start) / 1000, nap_ms) ;
  if (nap_ms > 0)
    delay (nap_ms) ;
}

