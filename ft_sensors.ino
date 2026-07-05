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
   polling frequency (eg, every 60 secs), and the file which configures tasks
   to be performed on each polling cycle. Thus each line in this file would
   tell us,

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
      d:        Comma separated function data. Depends on the sensor function
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

  INTERNALS

   As a standard user task thread, the call chain is as follows,

    - ft_sensors()              # entrypoint, reads commands file
     - f_sensors_cmd()          # supplied a single line, parses it
      - <internal commands>     # eg, "delay_ms"
      - f_sfunction_XXX()       # eg, "f_sfunction_dht22()"
       - f_sensor_XXX()         # eg, "f_sensor_dht22()"

  SENSOR FAILURES

   Some sensor functions may return multiple results (eg, multiple DS18B20
   units on a single bus). If a particular sensor drops off the bus, this can
   lead to a misalignment of "result[]" values. To mitigate this situation,
   we check "cur_result" against "total_results" at the end of each sensor poll
   cycle. If there is a mismatch, we must reinitialize "S_td_sensors" structure
   and the thread's "results[]" array (and all its "l_name" and "l_data"
   pointers). If the sensor subsequently recovers, we will get a higher number
   of results, which is detected by comparing against "prev_results". This too
   triggers a reinitialization of "S_td_sensors" and the thread's "result[]"
   array.

   Each time such a reinitialization is performed by "f_init_thread_data()",
   we increment this as a counter in the last DEF_MAX_THREAD_RESULTS entry in
   our "result[]" array.

   To better understand sensor failures, the "retries" flag (default 0) can be
   set to "1". Functions may check the "retries" flag and perform more detailed
   polling and checking. When "retries" is turned on, functions may send fault
   messages via MQTT so that we can better understand what's going wrong.
   These messages will be published to the default configured topic in the
   format,

     <thread_name>{event="fault",<other_labels,...>} 1
*/

/*
   The following structure is malloc'ed by "ft_sensors()" for its "malloc_buf".
   It is essentially the global memory shared among the "ft_sensors()" thread
   and all the "f_sfunction_xx()" calls. As we progress through the user
   supplied statements, "cur_result" starts from 0 and is incremented as the
   various "f_sfunction_xx()" calls obtain results. The "f_sfunction_xx()"
   calls also track "total_results", and use this to identify uninitialized
   results, which would typically require them calling "f_sensor_init_labels()"
   which sets up their result's "l_name" and "l_data" fields.
*/

struct td_sensors {
  char *cur_f ;                                 // user supplied function name
  char *cur_d ;                                 // current data params
  int cur_function ;                            // current "f:..." statement
  int num_functions ;                           // total "f:.." statements
  int cur_result ;                              // current sensor result index
  int total_results ;                           // current total sensor results
  int prev_results ;                            // results from previous run
  int t_idx ;                                   // our user task thread index
  int retries ;                                 // a flag to retry sensor reads
  long long next_run ;                          // usecs timestamp of next run
  long long ts_init ;                           // this struct's init time
  char *thread_name ;                           // this thread's name
  char *label_base[DEF_MAX_THREAD_RESULTS] ;    // pointers into "label_buf"
  char label_buf[BUF_LEN_UTASK_FILESIZE] ;      // all labels here
} ;
typedef struct td_sensors S_td_sensors ;

/*
   This function is called from "f_sfunction_xx()" functions. When these
   functions run for the first time, they need to initialize the labels in
   their results. That's where this function comes in. It parses the user
   supplied labels and updates the result's "l_name" and "l_data" pointers.
   On completion, the number of labels parsed is returned. Note that this
   function modifies the current "label_base[]" buffer, thus it can only be
   called ONCE per "label_base[]" entry.
*/

int f_sensor_init_labels(struct td_sensors *td)
{
  int label_idx=0 ;
  char *p, *token ;
  S_ThreadResult *res = G_runtime->utask[td->t_idx].result ;

  token = strtok_r(td->label_base[td->cur_function], ",", &p) ;
  while (token)
  {
    // note that "token" is typically in the format "label=value", parse it
    // into the current result's "l_name" and "l_data".

    char *pos = strchr(token, '=') ;
    if (pos)
    {
      *pos = 0 ;
      res[td->cur_result].l_name[label_idx] = token ;
      res[td->cur_result].l_data[label_idx] = pos + 1 ;
      label_idx++ ;
    }
    token = strtok_r(NULL, ",", &p) ;
  }
  return(label_idx) ;
}

/*
   This function is called from "s_sfunction_xx()" functions when multiple
   results are involved. Recall that "f_sensor_init_labels()" can only be
   called once by each "s_sfunction_xx()". Thus, if multiple results are
   involved, this function helps copy "l_name" and "l_data" entries from
   the specified "first" result, up to but not including the "l_name" with
   "end" into the "cur_result". The number of labels copied is returned.
*/

int f_sensor_copy_labels_until(struct td_sensors *td, int first, char *end)
{
  int label_idx = 0 ;
  S_ThreadResult *res = G_runtime->utask[td->t_idx].result ;

  while ((label_idx < DEF_MAX_THREAD_LABELS) &&
         (res[first].l_name[label_idx] != NULL) &&
         (strcmp(res[first].l_name[label_idx], end) != 0))
  {
    res[td->cur_result].l_name[label_idx] = res[first].l_name[label_idx] ;
    res[td->cur_result].l_data[label_idx] = res[first].l_data[label_idx] ;
    label_idx++ ;
  }
  return(label_idx) ;
}

/*
   This function is called from "f_sensors_cmd()" when the current function is
   identified to be an "aread".
*/

void f_sfunction_aread(struct td_sensors *td)
{
  if (td->cur_d == NULL)                        // fatal ! pin not specified
    return ;
  int in_pin = atoi(td->cur_d) ;
  S_ThreadResult *res = G_runtime->utask[td->t_idx].result ;

  // if this is the first time writing into "result[]", then we'll need to
  // parse our "label_base[]" and setup "l_name" and "l_data" fields.

  if (td->cur_result == td->total_results)
  {
    f_sensor_init_labels(td) ;
    td->total_results++ ; // this indicates the result has been initialized
  }

  res[td->cur_result].i_value = analogRead(in_pin) ;
  res[td->cur_result].result_type = UTHREAD_RESULT_INT ;
  if (G_runtime->config.debug)
    Serial.printf("DEBUG: f_sfunction_aread() in_pin:%d value:%d\r\n",
                  in_pin, res[td->cur_result].i_value) ;

  td->cur_result++ ;          // move this on to the next insertion point
}

/*
   This function is called from "f_sensors_cmd()" when the current function
   is identified to be an "f_sensor_bme280".
*/

void f_sfunction_bme280(struct td_sensors *td)
{
  S_ThreadResult *res = G_runtime->utask[td->t_idx].result ;

  // if this is the first time writing into "result[]", then we'll need to
  // parse our "label_base[]" and setup "l_name" and "l_data" fields.

  if (td->cur_result == td->total_results)
  {
    int label_idx = f_sensor_init_labels(td) ;
    res[td->cur_result].l_name[label_idx] = "measurement" ;
    res[td->cur_result].l_data[label_idx] = "temperature" ;
    td->total_results++ ;
    td->cur_result++ ;          // move forward to configure next result

    // copy the "l_name" and "l_data" from the first result into the next

    label_idx = f_sensor_copy_labels_until(td, td->cur_result - 1,
                                           "measurement") ;
    res[td->cur_result].l_name[label_idx] = "measurement" ;
    res[td->cur_result].l_data[label_idx] = "humidity" ;
    td->total_results++ ;
    td->cur_result++ ;

    label_idx = f_sensor_copy_labels_until(td, td->cur_result - 1,
                                           "measurement") ;
    res[td->cur_result].l_name[label_idx] = "measurement" ;
    res[td->cur_result].l_data[label_idx] = "pressure" ;
    td->total_results++ ;
    td->cur_result = td->cur_result - 2 ; // move result insertion point back
  }

  float temperature=0.0, humidity=0.0, pressure=0.0 ;
  if (f_bme280(&temperature, &humidity, &pressure))
  {
    res[td->cur_result].f_value = temperature ;
    res[td->cur_result].result_type = UTHREAD_RESULT_FLOAT ;
    td->cur_result++ ;
    res[td->cur_result].f_value = humidity ;
    res[td->cur_result].result_type = UTHREAD_RESULT_FLOAT ;
    td->cur_result++ ;
    res[td->cur_result].f_value = pressure ;
    res[td->cur_result].result_type = UTHREAD_RESULT_FLOAT ;
    td->cur_result++ ;
  }

  if (G_runtime->config.debug)
    Serial.printf("DEBUG: f_sfunction_bme280() t:%.3fC h:%.3f%% p:%.3fhpa\r\n",
                  temperature, humidity, pressure) ;
}

/*
   This function is called from "f_sensors_cmd()" when the current function
   is identified to be an "f_sensor_bmp180".
*/

void f_sfunction_bmp180(struct td_sensors *td)
{
  S_ThreadResult *res = G_runtime->utask[td->t_idx].result ;

  // if this is the first time writing into "result[]", then we'll need to
  // parse our "label_base[]" and setup "l_name" and "l_data" fields.

  if (td->cur_result == td->total_results)
  {
    int label_idx = f_sensor_init_labels(td) ;
    res[td->cur_result].l_name[label_idx] = "measurement" ;
    res[td->cur_result].l_data[label_idx] = "temperature" ;
    td->total_results++ ;
    td->cur_result++ ;          // move forward to configure next result

    // copy the "l_name" and "l_data" from the first result into the next

    label_idx = f_sensor_copy_labels_until(td, td->cur_result - 1,
                                           "measurement") ;
    res[td->cur_result].l_name[label_idx] = "measurement" ;
    res[td->cur_result].l_data[label_idx] = "pressure" ;

    td->total_results++ ;
    td->cur_result-- ;          // move next result insertion point back
  }

  float temperature=0.0, pressure=0.0 ;
  if (f_bmp180(&temperature, &pressure))
  {
    res[td->cur_result].f_value = temperature ;
    res[td->cur_result].result_type = UTHREAD_RESULT_FLOAT ;
    td->cur_result++ ;
    res[td->cur_result].f_value = pressure ;
    res[td->cur_result].result_type = UTHREAD_RESULT_FLOAT ;
    td->cur_result++ ;
  }

  if (G_runtime->config.debug)
    Serial.printf("DEBUG: f_sfunction_bmp180() t:%.3fC p:%.3fhpa\r\n",
                  temperature, pressure) ;
}

/*
   This function is called from "f_sensors_cmd()" when the current function is
   identified to be an "f_hcsr04".
*/

void f_sfunction_hcsr04(struct td_sensors *td)
{
  S_ThreadResult *res = G_runtime->utask[td->t_idx].result ;

  // expect 2x data parameters, trigger pin and echo pin, eg, "17,18"

  char line[BUF_LEN_LINE], *p ;
  if ((td->cur_d == NULL) || (strchr(td->cur_d, ',') == NULL))
    return ;                                    // fatal ! pins not specified
  strncpy(line, td->cur_d, BUF_LEN_LINE) ;
  p = strchr(td->cur_d, ',') ;
  *p = 0 ;
  int trig_pin = atoi(line) ;
  int echo_pin = atoi(p+1) ;
  float distance_cm = f_hcsr04(trig_pin, echo_pin) ;

  // if this is the first time writing into "result[]", then we'll need to
  // parse our "label_base[]" and setup "l_name" and "l_data" fields.

  if (td->cur_result == td->total_results)
  {
    f_sensor_init_labels(td) ;
    td->total_results++ ;
  }

  if (distance_cm > 0.0)
  {
    res[td->cur_result].f_value = distance_cm ;
    res[td->cur_result].result_type = UTHREAD_RESULT_FLOAT ;
    td->cur_result++ ;
  }

  if (G_runtime->config.debug)
    Serial.printf("DEBUG: f_sfunction_hcsr04(): trig:%d echo:%d d:%.3fcm\r\n",
                  trig_pin, echo_pin, distance_cm) ;
}

/*
   This function is called from "f_sensors_cmd()" when the current function is
   identified to be an "f_sensor_dht22".
*/

void f_sfunction_dht22(struct td_sensors *td)
{
  if (td->cur_d == NULL)                        // fatal ! pin not specified
    return ;
  int data_pin = atoi(td->cur_d) ;
  S_ThreadResult *res = G_runtime->utask[td->t_idx].result ;

  // if this is the first time writing into "result[]", then we'll need to
  // parse our "label_base[]" and setup "l_name" and "l_data" fields.

  if (td->cur_result == td->total_results)
  {
    int label_idx = f_sensor_init_labels(td) ;
    res[td->cur_result].l_name[label_idx] = "measurement" ;
    res[td->cur_result].l_data[label_idx] = "temperature" ;
    td->total_results++ ;
    td->cur_result++ ;          // move forward to configure next result

    // copy the "l_name" and "l_data" from the first result into the next

    label_idx = f_sensor_copy_labels_until(td, td->cur_result - 1,
                                           "measurement") ;
    res[td->cur_result].l_name[label_idx] = "measurement" ;
    res[td->cur_result].l_data[label_idx] = "humidity" ;

    td->total_results++ ;
    td->cur_result-- ;          // move next result insertion point back
  }

  int all_good=0 ;
  char err[BUF_LEN_ERR] ;
  float temperature=0.0, humidity=0.0 ;

  // if "td->retries" is set, then privately poll the sensor twice and compare
  // results before proceeding.

  if (td->retries)
  {
    float t1, t2, h1, h2 ;

    for (int attempt=0 ; attempt < DHT22_MAX_RETRIES ; attempt++)
    {
      all_good = f_sensor_dht22(data_pin, &t1, &h1, err) ;
      if (all_good == 0)
      {
        snprintf(err, BUF_LEN_ERR,
                 "%s{event=\"fault\",f=\"f_sfunction_dht22\","
                 "reason=\"empty_poll1\",pin=\"%d\",attempt=\"%d\"} 1",
                 td->thread_name, data_pin, attempt) ;
        f_mqtt_publish(-1, err) ;
      }

      delay(DHT22_POLL_DELAY_MS) ;
      if (all_good)                             // proceed with 2nd reading
      {
        all_good = f_sensor_dht22(data_pin, &t2, &h2, err) ;
        if (all_good)
        {
          if ((fabsf(t1 - t2) < DHT22_MAX_T_DELTA) &&
              (fabsf(h1 - h2) < DHT22_MAX_H_DELTA))     // ok, really good !
          {
            if (G_runtime->config.debug)
              Serial.printf("DEBUG: f_sfunction_dht22() attempt:%d\r\n",
                            attempt) ;
            temperature = t1 ;
            humidity = h1 ;
            break ;
          }
          else
          {
            if (G_runtime->config.debug)
              Serial.printf("DEBUG: f_sfunction_dht22() large delta\r\n") ;
            snprintf(err, BUF_LEN_ERR,
                     "%s{event=\"fault\",f=\"f_sfunction_dht22\","
                     "reason=\"large_delta\",pin=\"%d\",attempt=\"%d\"} 1",
                     td->thread_name, data_pin, attempt) ;
            f_mqtt_publish(-1, err) ;
          }
        }
        else
        {
          snprintf(err, BUF_LEN_ERR,
                   "%s{event=\"fault\",f=\"f_sfunction_dht22\","
                   "reason=\"empty_poll2\",pin=\"%d\",attempt=\"%d\"} 1",
                   td->thread_name, data_pin, attempt) ;
          f_mqtt_publish(-1, err) ;
        }
      }
      else
      {
        snprintf(err, BUF_LEN_ERR,
                 "%s{event=\"fault\",f=\"f_sfunction_dht22\","
                 "reason=\"no_response\",pin=\"%d\",attempt=\"%d\"} 1",
                 td->thread_name, data_pin, attempt) ;
        f_mqtt_publish(-1, err) ;
      }

      delay(DHT22_POLL_DELAY_MS) ;
    }
  }
  else
    all_good = f_sensor_dht22(data_pin, &temperature, &humidity, err) ;

  // if we were successful, update/expose new temperature / humidity readings

  if (all_good)
  {
    res[td->cur_result].f_value = temperature ;
    res[td->cur_result].result_type = UTHREAD_RESULT_FLOAT ;
    td->cur_result++ ;
    res[td->cur_result].f_value = humidity ;
    res[td->cur_result].result_type = UTHREAD_RESULT_FLOAT ;
    td->cur_result++ ;
  }

  if (G_runtime->config.debug)
    Serial.printf("DEBUG: f_sfunction_dht22() t:%.3fC h:%.3f%%\r\n",
                  temperature, humidity) ;
}

/*
   This function is called from "f_sensors_cmd()" when the current function is
   identified to be an "f_sensor_ds18b20".
*/

void f_sfunction_ds18b20(struct td_sensors *td)
{
  if (td->cur_d == NULL)                        // fatal ! pin not specified
    return ;

  int label_idx=0, total_devs=0 ;
  int data_pin = atoi(td->cur_d) ;
  char err[BUF_LEN_ERR] ;
  float temperatures[DS18B20_MAX_PER_BUS] ;
  unsigned char addrs[DS18B20_MAX_PER_BUS * 8] ;  // 8 bytes per sensor

  // if "td->retries" is set, then privately repeat the above poll on
  // "data_pin" and compare results before blindly accepting them. We want
  // to have 2x sets of readings which have matching number of devices and
  // are within DS18B20_MAX_DELTA of each other.

  if (td->retries)
  {
    int all_good=0 ;
    int devs_one=0, devs_two=0 ;
    float t_one[DS18B20_MAX_PER_BUS], t_two[DS18B20_MAX_PER_BUS] ;

    for (int attempt=0 ; attempt < DS18B20_MAX_RETRIES ; attempt++)
    {
      memset(addrs, 0, DS18B20_MAX_PER_BUS * 8) ;
      devs_one = f_sensor_ds18b20(data_pin, t_one, addrs) ;
      delay(DS18B20_POLL_DELAY) ;
      devs_two = f_sensor_ds18b20(data_pin, t_two, addrs) ;

      if ((devs_one == 0) || (devs_two == 0))   // no DS18B20 devices detected
      {
        snprintf(err, BUF_LEN_ERR,
                 "%s{event=\"fault\",f=\"f_sfunction_ds18b20\","
                 "reason=\"empty,d1:%d,d2:%d\",pin=\"%d\",attempt=\"%d\"} 1",
                 td->thread_name, devs_one, devs_two, data_pin, attempt) ;
        f_mqtt_publish(-1, err) ;
      }

      if (devs_one != devs_two)
      {
        all_good = 0 ;
        if (G_runtime->config.debug)
          Serial.printf("DEBUG: f_sfunction_ds18b20() devs mismatch\r\n") ;
        snprintf(err, BUF_LEN_ERR,
                 "%s{event=\"fault\",f=\"f_sfunction_ds18b20\","
                 "reason=\"count_mismatch,d1:%d,d2:%d\","
                 "pin=\"%d\",attempt=\"%d\"} 1",
                 td->thread_name, devs_one, devs_two, data_pin, attempt) ;
        f_mqtt_publish(-1, err) ;
      }
      else
      {
        all_good = 1 ;
        for (int i=0 ; i < devs_one ; i++)
          if (fabsf(t_one[i] - t_two[i]) > DS18B20_MAX_DELTA)
          {
            all_good = 0 ;
            if (G_runtime->config.debug)
              Serial.printf("DEBUG: f_sfunction_ds18b20() large delta\r\n") ;
            snprintf(err, BUF_LEN_ERR,
                     "%s{event=\"fault\",f=\"f_sfunction_ds18b20\","
                     "reason\"large_delta,i:%d,%.3f->%.3f\","
                     "pin=\"%d\",attempt=\"%d\"} 1",
                     td->thread_name, i, t_one[i], t_two[i],
                     data_pin, attempt) ;
            f_mqtt_publish(-1, err) ;
          }
      }

      // if data is good, copy to "total_devs", "temperatures" and "addrs"

      if (all_good)
      {
        if (G_runtime->config.debug)
          Serial.printf("DEBUG: f_sfunction_ds18b20() devs:%d attempt:%d\r\n",
                        devs_one, attempt) ;

        total_devs = devs_one ;
        for (int i=0 ; i < devs_one ; i++)
          temperatures[i] = t_one[i] ;
        break ;
      }
      else
        delay(DS18B20_POLL_DELAY) ;
    }
  }
  else                          // don't do retries, just do a single poll
  {
    memset(addrs, 0, DS18B20_MAX_PER_BUS * 8) ;
    total_devs = f_sensor_ds18b20(data_pin, temperatures, addrs) ;
  }

  // if this our first time writing into "result[]", we'll need to store
  // the hex string addresses of DS18B20 units in heap memory. Since the
  // current "label_base[]" entry points at "free" space, we'll use this
  // area for storing the hex strings and move the current "label_base[]"
  // forward. Note each hex address string needs 18 bytes (including NULL).

  S_ThreadResult *res = G_runtime->utask[td->t_idx].result ;
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
        label_idx = f_sensor_init_labels(td) ;
      else
      {
        int first_idx = td->cur_result - dev_idx ;
        label_idx = f_sensor_copy_labels_until(td, first_idx, "address") ;
      }

      // the last label is the "address", record down its "l_data" buffer.

      res[td->cur_result].l_name[label_idx] = "address" ;
      res[td->cur_result].l_data[label_idx] = addr_hex ;

      addr_hex = addr_hex + 18 ;
      td->cur_result++ ;
      td->total_results++ ; // this indicates the result has been initialized
    }

    // we're done initializing result labels. Move "cur_result" backwards
    // so that we can start filling sensor data.

    td->cur_result = td->cur_result - total_devs ;
  }

  // now copy the sensor's addr and readings into the "result[]". Before we
  // can do that, find the "label_idx" which points to "address".

  for (label_idx=0 ; label_idx < DEF_MAX_THREAD_LABELS ; label_idx++)
    if ((res[td->cur_result].l_name[label_idx] != NULL) &&
        (strcmp(res[td->cur_result].l_name[label_idx], "address") == 0))
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
  if (G_runtime->config.debug)
    Serial.printf("DEBUG: f_sfunction_ds18b20() data_pin:%d total_devs:%d\r\n",
                  data_pin, total_devs) ;
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
        Serial.printf("DEBUG: f_sensors_cmd() c:%s [code:%d] %s", c,
                      G_runtime->worker[tid].result_code,
                      G_runtime->worker[tid].result_msg) ;

      G_runtime->worker[tid].cmd = NULL ;         // unset worker's "cmd"
      G_runtime->worker[tid].state = W_IDLE ;     // release worker
    }
  }

  if (f)
  {
    td->cur_f = f ;             // function name
    td->cur_d = d ;             // data params
    td->cur_function++ ;

    // if this is the first time we're encounting this function, then copy
    // its metric labels into "label_base[]" (which references "label_buf"),
    // and update the next "label_base[]" so that it is ready for the next
    // "l:..." entry.

    if (td->cur_function >= td->num_functions)
    {
      td->num_functions++ ;
      int fn_idx = td->cur_function ;
      memcpy(td->label_base[fn_idx], l, strlen(l)) ;
      td->label_base[fn_idx + 1] = td->label_base[fn_idx] + strlen(l) + 1 ;
    }

    // now decide which sensor function we'll execute

    if (strcmp(f, "aread") == 0)                        // aread
      f_sfunction_aread(td) ;
    else
    if (strcmp(f, "f_bme280") == 0)                     // BME280
      f_sfunction_bme280(td) ;
    else
    if (strcmp(f, "f_bmp180") == 0)                     // BMP180
      f_sfunction_bmp180(td) ;
    else
    if (strcmp(f, "f_hcsr04") == 0)                     // HC-SR04
      f_sfunction_hcsr04(td) ;
    else
    if (strcmp(f, "f_sensor_dht22") == 0)               // DHT22
      f_sfunction_dht22(td) ;
    else
    if (strcmp(f, "f_sensor_ds18b20") == 0)             // DS18B20
      f_sfunction_ds18b20(td) ;
  }
}

/*
   This function is called from "ft_sensors()". It wipes and initializes the
   S_td_sensors structure at the supplied "td" address.
*/

void f_init_thread_data (struct td_sensors *td, S_UserThread *self,
                         int retries)
{
  memset(td, 0, sizeof(S_td_sensors)) ;
  for (int t_idx=0 ; t_idx < DEF_MAX_USER_THREADS ; t_idx++)
    if (&G_runtime->utask[t_idx] == self)
    {
      td->t_idx = t_idx ;
      break ;
    }
  td->prev_results = -1 ;                       // set to invalid/uninitialized
  td->retries = retries ;                       // retry sensor reads
  td->label_base[0] = td->label_buf ;           // point to start of buffer
  td->next_run = esp_timer_get_time() ;         // set this to now essentially
  td->ts_init = td->next_run ;                  // used to track (re)init
  td->thread_name = self->name ;                // this thread's name

  // now wipe any previous "result[]" data, but save the last result, which
  // tracks the number of times this function re-inits.

  int count = self->result[DEF_MAX_THREAD_RESULTS-1].i_value ;
  memset(G_runtime->utask[td->t_idx].result, 0,
         DEF_MAX_THREAD_RESULTS * sizeof(S_ThreadResult)) ;

  self->result[DEF_MAX_THREAD_RESULTS-1].l_name[0] = "init" ;
  self->result[DEF_MAX_THREAD_RESULTS-1].l_data[0] = "counter" ;
  self->result[DEF_MAX_THREAD_RESULTS-1].result_type = UTHREAD_RESULT_INT ;
  self->result[DEF_MAX_THREAD_RESULTS-1].i_value = count + 1 ;

  if (G_runtime->config.debug)
    Serial.printf("DEBUG: f_init_thread_data() initialized with t_idx:%d\r\n",
                  td->t_idx) ;
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

  if ((self->num_args != 2) && (self->num_args != 3))
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
    int retries = 0 ;
    if (self->num_args == 3)
      retries = atoi(self->in_args[2]) ;

    self->malloc_buf = malloc(sizeof(S_td_sensors)) ;
    if (self->malloc_buf == NULL)
    {
      strncpy(self->status, "malloc failed", BUF_LEN_UTHREAD_STATUS) ;
      self->state = UTHREAD_STOPPED ;
      return ;
    }
    td = (S_td_sensors*) self->malloc_buf ;
    f_init_thread_data(td, self, retries) ;
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

  if (td->prev_results < 0)                     // do a one time init
    td->prev_results = td->total_results ;

  // if the number of results we obtained changed, then we wither had a sensor
  // failure, or a sensor recovery. Either way, reinitialize "td" to force a
  // re-probe.

  if ((td->cur_result != td->total_results) ||
      (td->total_results != td->prev_results))
  {
    if (G_runtime->config.debug)
      Serial.printf("DEBUG: ft_sensors() cur_result:%d total_results:%d\r\n",
                    td->cur_result, td->total_results) ;

    char err[BUF_LEN_ERR] ;
    snprintf(err, BUF_LEN_ERR,
             "%s{event=\"fault\",f=\"ft_sensors\","
             "reason=\"result_mismatch:%d->%d,prev:%d\"} 1",
             td->thread_name,
             td->cur_result, td->total_results, td->prev_results) ;
    f_mqtt_publish(-1, err) ;

    int retries = 0 ;
    if (self->num_args == 3)
      retries = atoi(self->in_args[2]) ; // pass in "retries" if specified
    f_init_thread_data(td, self, retries) ;
  }
  else
  {
    // end of sensor poll cycle. Update our status and take a nap

    td->prev_results = td->total_results ;
    td->next_run = td->next_run + (interval_secs * 1000000) ;
    long nap_ms = (td->next_run - esp_timer_get_time()) / 1000 ;
    if (nap_ms < 1)
      nap_ms = 1 ;
    snprintf(self->status, BUF_LEN_UTHREAD_STATUS,
             "f:%d r:%d nap:%ldms init:%lld",
             td->num_functions, td->total_results, nap_ms, td->ts_init) ;
    delay(nap_ms) ;
  }
}
