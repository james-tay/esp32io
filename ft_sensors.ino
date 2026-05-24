/*
   One of the side effects of having a large number of sensors (eg, DHT22,
   DS18B20, HC-SR04, ACS712, etc) connected to an ESP32, is that running
   individual threads to poll and expose their metrics results in a high
   amount of memory consumption (due to having to allocate independent thread
   stacks). In many scenarios, we do not need high speed polling of these
   sensors, so it might be more efficient to have a single thread manage the
   polling of multiple sensors and exposing their results.

   This is the motivation behind the "ft_sensors()" user task thread. The
   standard configuration supplied to this function includes the overall
   polling frequency (eg, every 60 secs), and the file which configures each
   polling cycle. Thus each line in this file would tell us,

     - what preparation to perform (eg, power up a group of sensors)
     - the sensor function to call
       - the pin(s) where this sensor(s) is attached to
       - labels to be associated with this sensor's metrics
     - what post-poll actions to perform (eg, power down sensors)

   Consider the following polling cycle file,

     c:hi 23
     f:f_sensor_dht22;d:18;l:location="kitchen",model="dht22"
     c:lo 23
     c:hi 22
     f:f_sensor_ds18b20;d:19;l:location="garage",model="ds18b20"
     c:lo 22
     f:f_hcsr04;d:17,16;l:location="entrance",type="proximity"
     f:aread;d:36;l:location="solar",type="acs712-5"

   From the above example, we see this file is organized into tasks, one task
   per line. Within each task, various parameters are seperated by semi-colons.
   The following parameters are supported:

     c:         A command, any supported command may be executed
     f:         Function name. Only certain sensor functions are supported
     d:         Function data. Depends on the sensor function
     l:         Labels to be included in exposed metrics

   Currently, lines must begin with with a "c:" or a "f:". Also, note the
   following limits,

     DEF_MAX_THREAD_RESULTS     total number of sensor results we can expose
     BUF_LEN_UTASK_FILESIZE     file size limit of user task file
*/

struct td_sensors {
  long long next_run ;                          // usecs timestamp
} ;
typedef struct td_sensors S_td_sensors ;

/*
   This function is called from "f_user_thread_lifecycle()". Its job is to
   parse its supplied file, acting on each task, line by line.
*/

void ft_sensors(S_UserThread *self)
{
  char cmd_buf[BUF_LEN_UTASK_FILESIZE], *cur_cmd, *p ;
  S_td_sensors *td = NULL ;

  // parse our thread arguments.

  if (self->num_args != 2)
  {
    strncpy(self->status, "Incorrect arguments", BUF_LEN_UTHREAD_STATUS) ;
    self->state = UTHREAD_STOPPED ;
    return ;
  }
  int interval_secs = atoi(self->in_args[0]) ;
  char *tasks_file = self->in_args[1] ;

  // initialization on our first loop

  if (self->loop == 0)
  {
    self->malloc_buf = malloc(sizeof(S_td_sensors)) ;
    if (self->malloc_buf == NULL)
    {
      strncpy(self->status, "malloc failed", BUF_LEN_UTHREAD_STATUS) ;
      self->state = UTHREAD_STOPPED ;
      return ;
    }
    memset(self->malloc_buf, 0, sizeof(S_td_sensors)) ;
    td = (S_td_sensors*) self->malloc_buf ;
    td->next_run = esp_timer_get_time() ;
  }
  td = (S_td_sensors*) self->malloc_buf ;

  // read our tasks file into "cmd_buf" and iterate through each task

  if (f_read_whole(tasks_file, cmd_buf, BUF_LEN_UTASK_FILESIZE) < 1)
  {
    snprintf(self->status, BUF_LEN_UTHREAD_STATUS,
             "Cannot read %s", tasks_file) ;
    self->state = UTHREAD_STOPPED ;
    return ;
  }

  cur_cmd = f_get_statement(cmd_buf, &p) ;
  while (cur_cmd)
  {
    if (G_runtime->config.debug)
      Serial.printf("DEBUG: ft_sensors() cur_cmd:%s\r\n", cur_cmd) ;





    cur_cmd = f_get_statement(NULL, &p) ;       // move on to next task
  }

  // end of sensor poll cycle. Take a nap

  td->next_run = td->next_run + (interval_secs * 1000000) ;
  long nap_ms = (td->next_run - esp_timer_get_time()) / 1000 ;
  if (nap_ms < 1)
    nap_ms = 1 ;
  snprintf(self->status, BUF_LEN_UTHREAD_STATUS, "nap %ld ms", nap_ms) ;
  delay(nap_ms) ;
}
