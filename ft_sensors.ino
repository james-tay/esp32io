/*
  BACKGROUND

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
       - labels to be associated with this sensor's metrics (note double-quotes
         are not needed here)
     - what post-poll actions to perform (eg, power down sensors)

   Consider the following polling cycle file,

     c:hi 23
     c:delay_ms 300
     f:f_sensor_dht22;d:18;l:location=kitchen,model=dht22
     c:lo 23
     c:hi 22
     c:delay_ms 300
     f:f_sensor_ds18b20;d:19;l:location=garage,model=ds18b20
     c:lo 22
     f:f_hcsr04;d:17,16;l:location=entrance,type=proximity
     f:aread;d:36;l:location="solar",type="acs712-5"

   From the above example, we see this file is organized into tasks, one task
   per line. Within each task, various parameters are seperated by semi-colons.
   Each line must start with either a "c:" or a "f:". A "f:" statement is
   usually accompanied by other parameters. The following parameters are
   supported:

     c:         A command, any supported command may be executed
     f:         Function name. Only certain sensor functions are supported
      d:        Function data. Depends on the sensor function
      l:        Labels to be included in exposed metrics

   Currently, lines must begin with with a "c:" or a "f:". Also, note the
   following limits,

     DEF_MAX_THREAD_RESULTS     total number of sensor results we can expose
     DEF_MAX_THREAD_RESULTS     total number of sensor functions we can call
     BUF_LEN_UTASK_FILESIZE     file size limit of user task file

  METRIC LABELS

   On each call to "ft_sensors()", the user specified task file is read into
   the function's "cmd_buf". This means user specified metric labels for each
   sensor lives in "cmd_buf". We need to setup the user task thread's "results"
   struct array's "l_name" and "l_data" pointers, but these must not point
   into "cmd_buf" since this buffer goes out of scope once "ft_sensors()"
   returns. Thus we copy all sensor labels into a single "label_buf" in the
   S_td_sensors structure, and use the "label_base[]" array to point to each
   sensor's labels.

   As we progress through each "f:" statement, the "f_sensors_cmd()" function
   increments "cur_function" and is responsible for setting up "label_base[]"
   and "label_buf" on first use. We know this is the first use because
   "num_functions" is initialized to -1. The user supplied metric labels are
   copied into "label_buf" at the current "label_base[]", so that we can use
   "strtok_r()" to split the individual labels. For this reason, the next
   element in "label_base[]" is always set to point to the start of the next
   offset in "label_buf". As sensor results are obtained, they are tracked in
   "cur_result". Thus if "cur_result" exceeds "total_results", that indicates
   a "result[]" set's "l_name" and "l_data" need to be initialized.

   Certain sensor functions (eg, "f_sensor_ds18b20()"), when run for the first
   time, may detect multiple devices on a bus. Thus, we need a buffer to store
   detected device addresses (these in turn are used as labels when exposing
   metrics). Thus, "label_buf" is again used for this purpose. In this case,
   the current "label_base[]" pointer is pushed forward to allow for this
   storage.

   Recall that the metric name is still determined by thread name, or it may
   be defined by "/<thread_name>.labels" (optional file).

  SENSOR FAILURES

   Some sensor functions may return multiple results (eg, multiple DS18B20
   units on a single bus). If a particular sensor drops off the bus, this can
   lead to a misalignment of "result[]" values. To mitigate this situation,
   we track "total_results" at the end of each sensor poll cycle against the
   "prev_results" value. If there is a mismatch, we must reinitialize the
   "results[]" array (and all its "l_name" and "l_data" pointers).
*/

struct td_sensors {
  char *cur_f ;                                 // user supplied function name
  char *cur_d ;                                 // current data params
  char *cur_l ;                                 // current metric labels
  int cur_function ;                            // current "f:..." statement
  int num_functions ;                           // total "f:.." statements
  int cur_result ;                              // current sensor result index
  int total_results ;                           // current total sensor results
  int prev_results ;                            // results from previous run
  int t_idx ;                                   // our user task thread index
  long long next_run ;                          // usecs timestamp
  char *label_base[DEF_MAX_THREAD_RESULTS] ;    // pointers into "label_buf"
  char label_buf[BUF_LEN_UTASK_FILESIZE] ;      // all labels here
} ;
typedef struct td_sensors S_td_sensors ;

/*
   This function is called from "f_sensors_cmd()" when the current function is
   identified to be an "aread".
*/

void f_sfunction_aread(struct td_sensors *td)
{
  int in_pin = atoi(td->cur_d) ;
  S_ThreadResult *res = G_runtime->utask[td->t_idx].result ;

  // if this is the first time writing into "result[]", then we'll need to
  // parse our "label_base[]" and setup "l_name" and "l_data" fields.

  if (td->cur_result == td->total_results)
  {
    int label_idx=0 ;
    char *p, *token ;
    token = strtok_r(td->label_base[td->cur_function], ",", &p) ;
    while (token)
    {
      // "token" is typically in the format "label=value"

      char *pos = strchr(token, '=') ;
      if (pos)
      {
        *pos = 0 ;
        res[td->cur_result].l_name[label_idx] = token ;
        res[td->cur_result].l_data[label_idx] = pos + 1 ;
      }
      token = strtok_r(NULL, ",", &p) ;
      label_idx++ ;
    }
    td->total_results++ ;     // indicate this result is now initialized
  }

  res[td->cur_result].i_value = analogRead(in_pin) ;
  res[td->cur_result].result_type = UTHREAD_RESULT_INT ;

  td->cur_result++ ;          // move this on to the next insertion point
}

/*
   This function is called from "f_sensors_cmd()" when the current function is
   identified to be an "f_sensor_dht22".
*/

void f_sfunction_dht22(struct td_sensors *td)
{




}

/*
   This function is called from "f_sensors_cmd()" when the current function is
   identified to be an "f_sensor_ds18b20".
*/

void f_sfunction_ds18b20(struct td_sensors *td)
{
  int label_idx=0 ;
  int data_pin = atoi(td->cur_d) ;
  float temperatures[DEF_DS18B20_MAX_PER_BUS] ;
  unsigned char addrs[DEF_DS18B20_MAX_PER_BUS * 8] ;  // 8 bytes per sensor
  S_ThreadResult *res = G_runtime->utask[td->t_idx].result ;

  memset(addrs, 0, DEF_DS18B20_MAX_PER_BUS * 8) ;
  int total_devs = f_sensor_ds18b20(data_pin, temperatures, addrs) ;

  // if this our first time writing into "result[]", we'll need to store
  // the hex string addresses of DS18B20 units in heap memory. Since the
  // current "label_base[]" entry points at "free" space, we'll use this
  // area for storing the hex strings and move the current "label_base[]"
  // forward. Note each hex address string needs 18 bytes (including NULL).

  if (td->cur_result + total_devs >= td->total_results)
  {
    int total_buf_size = 18 * total_devs ;
    char *addr_hex = td->label_base[td->cur_function + 1] ; // next free area
    td->label_base[td->cur_function + 1] = addr_hex + total_buf_size ;

    for (int dev_idx=0 ; dev_idx < total_devs ; dev_idx++)
    {
      char *p, *token ;

      // parse our "label_base[]" entry for the first result only. For all
      // subsequent results, copy the "l_name" and "l_data" entries from the
      // first result until we hit "address" for "l_name".

      label_idx = 0 ;
      if (dev_idx == 0)
      {
        token = strtok_r(td->label_base[td->cur_function], ",", &p) ;
        while (token)
        {
          char *pos = strchr(token, '=') ;
          if (pos)
          {
            *pos = 0 ;
            res[td->cur_result].l_name[label_idx] = token ;
            res[td->cur_result].l_data[label_idx] = pos + 1 ;
          }
          token = strtok_r(NULL, ",", &p) ;
          label_idx++ ;
        }
      }
      else
      {
        int first_result = td->cur_result - dev_idx ;
        while (strcmp(res[first_result].l_name[label_idx], "address") != 0)
        {
          res[td->cur_result].l_name[label_idx] =
            res[first_result].l_name[label_idx] ;
          res[td->cur_result].l_data[label_idx] =
            res[first_result].l_data[label_idx] ;
          label_idx++ ;
        }
      }

      // the last label is the "address", record down its "l_data" buffer.

      res[td->cur_result].l_name[label_idx] = "address" ;
      res[td->cur_result].l_data[label_idx] = addr_hex ;

      addr_hex = addr_hex + 18 ;
      td->total_results++ ;
      td->cur_result++ ;
    }

    // we're done initializing result labels. Move "cur_result" backwards
    // so that we can start filling sensor data.

    td->cur_result = td->cur_result - total_devs ;
  }

  // now copy the sensor's addr and readings into the "result[]". Before we
  // can do that, find the "label_idx" which points to "address".

  for (label_idx=0 ; label_idx < DEF_MAX_THREAD_LABELS ; label_idx++)
    if (strcmp(res[td->cur_result].l_name[label_idx], "address") == 0)
      break ;
  for (int dev_idx=0 ; dev_idx < total_devs ; dev_idx++)
  {
    char *dev = (char*) addrs + (dev_idx * 8) ;
    sprintf(res[td->cur_result].l_data[label_idx],
            "%02x%02x%02x%02x%02x%02x%02x%02x",
            dev[0], dev[1], dev[2], dev[3], dev[4], dev[5], dev[6], dev[7]) ;
    res[td->cur_result].f_value = temperatures[dev_idx] ;
    res[td->cur_result].result_type = UTHREAD_RESULT_FLOAT ;
    td->cur_result++ ;
  }
}

/*
   This function is called from "ft_sensors()". Our job is to process the
   supplied "cur_cmd". This involves parsing it into semi-colon separated
   tokens and then deciding what to do with them. Thus, our job is to populate
   the "c", "f", "d" and "l" pointers.
*/

void f_sensors_cmd(struct td_sensors *td, char *cur_cmd)
{
  char *token, *p ;
  char *c=NULL, *f=NULL, *d=NULL, *l=NULL ;     // user supplied params

  if (G_runtime->config.debug)
    Serial.printf("DEBUG: f_sensors_cmd() cur_cmd:%s\r\n", cur_cmd) ;

  token = f_get_statement(cur_cmd, &p) ;
  while ((token) && (strlen(token) > 2))
  {
    if (G_runtime->config.debug)
      Serial.printf("DEBUG: f_sensors_cmd() token:%s\r\n", token) ;
    if ((token[0] == 'c') && (token[1] == ':'))
      c = token + 2 ;
    if ((token[0] == 'f') && (token[1] == ':'))
      f = token + 2 ;
    if ((token[0] == 'd') && (token[1] == ':'))
      d = token + 2 ;
    if ((token[0] == 'l') && (token[1] == ':'))
      l = token + 2 ;
    token = f_get_statement(NULL, &p) ;         // move on to next token
  }

  // broadly, statements are either a "c" (command) or a "f" (function). If
  // "cur_cmd" is a "c", evaluate built-in commands, otherwise farm it out to
  // a worker thread.

  if (c)
  {
    char *tokens[2] ;
    if ((strncmp(c, "delay_ms", 8) == 0) && (f_parse(c, tokens, 2) == 2))
    {
      delay(atoi(tokens[1])) ;
    }
    else
    {
      int tid = f_get_next_worker() ;
      G_runtime->worker[tid].caller = DEF_UTHREAD_CALLER_OFFSET + td->t_idx ;
      G_runtime->worker[tid].cmd = c ;
      xTaskNotifyGive(G_runtime->worker[tid].w_handle) ;      // wake worker
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY) ;               // wait here

      // if we're here, that means worker "tid" has completed "cur_cmd"

      if (G_runtime->config.debug)
        Serial.printf("DEBUG: f_sensors_cmd() c:%s -> %d:%s", c,
                      G_runtime->worker[tid].result_code,
                      G_runtime->worker[tid].result_msg) ;

      G_runtime->worker[tid].cmd = NULL ;         // unset worker's "cmd"
      G_runtime->worker[tid].state = W_IDLE ;     // release worker
    }
  }

  if ((f) && (d) && (l))
  {
    td->cur_f = f ;             // function name
    td->cur_d = d ;             // data params
    td->cur_l = l ;             // metric labels
    td->cur_function++ ;

    // if this is the first time we're encounting this function, then copy
    // its metric labels into "label_base[]" (which references "label_buf"),
    // and update the next "label_base[]" so that it is ready for the next
    // "l:..." entry.

    if (td->cur_function > td->num_functions)
    {
      td->num_functions++ ;
      int fn_idx = td->cur_function ;
      memcpy(td->label_base[fn_idx], l, strlen(l)) ;
      td->label_base[fn_idx + 1] = td->label_base[fn_idx] + strlen(l) + 1 ;
    }

    // now decide which sensor function we'll execute

    if (strcmp(f, "aread") == 0)
      f_sfunction_aread(td) ;
    if (strcmp(f, "f_sensor_dht22") == 0)
      f_sfunction_dht22(td) ;
    if (strcmp(f, "f_sensor_ds18b20") == 0)
      f_sfunction_ds18b20(td) ;
  }
}

/*
   This function is called from "f_user_thread_lifecycle()". Its job is to
   parse its supplied file, acting on each task, line by line.
*/

void ft_sensors(S_UserThread *self)
{
  int t_idx ;
  char cmd_buf[BUF_LEN_UTASK_FILESIZE], *cur_cmd, *p ;
  S_td_sensors *td=NULL ;

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
    td->t_idx = -1 ;
    td->num_functions = -1 ;
    td->label_base[0] = td->label_buf ;
    td->next_run = esp_timer_get_time() ;

    // identify our user task thread index (needed for job dispatch later)

    for (t_idx=0 ; t_idx < DEF_MAX_USER_THREADS ; t_idx++)
      if (&G_runtime->utask[t_idx] == self)
      {
        td->t_idx = t_idx ;
        break ;
      }
    if (t_idx == DEF_MAX_USER_THREADS)
    {
      strncpy(self->status, "Cannot find t_idx", BUF_LEN_UTHREAD_STATUS) ;
      self->state = UTHREAD_STOPPED ;
      return ;
    }
    self->state = UTHREAD_RUNNING ;     // results are exposed once this is set
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

  // this is where we'll iterate through all statements, but first initialize
  // variables which track progress.

  td->cur_function = -1 ;
  td->cur_result = 0 ;
  cur_cmd = f_get_statement(cmd_buf, &p) ;
  while (cur_cmd)
  {
    // do a "trim()" on "cur_cmd", ie, remove white space, before sending it
    // to "f_sensors_cmd()".

    while ((strlen(cur_cmd) > 0) && (isspace((char)*cur_cmd)))
      cur_cmd++ ;
    char *end = cur_cmd + strlen(cur_cmd) - 1 ;
    while ((end > cur_cmd) && (isspace((char)*end)))
    {
      *end = 0 ;
      end-- ;
    }
    f_sensors_cmd(td, cur_cmd) ;                        // run user statement
    if (td->cur_function == DEF_MAX_THREAD_RESULTS)     // too many functions
    {
      strncpy(self->status, "Too many functions", BUF_LEN_UTHREAD_STATUS) ;
      self->state = UTHREAD_STOPPED ;
      return ;
    }
    cur_cmd = f_get_statement(NULL, &p) ;       // move on to next task
  }

  // end of sensor poll cycle. Update our status and take a nap

  td->next_run = td->next_run + (interval_secs * 1000000) ;
  long nap_ms = (td->next_run - esp_timer_get_time()) / 1000 ;
  if (nap_ms < 1)
    nap_ms = 1 ;
  snprintf(self->status, BUF_LEN_UTHREAD_STATUS,
           "funtions:%d results:%d nap:%ldms",
           td->num_functions, td->total_results, nap_ms) ;
  delay(nap_ms) ;
}
