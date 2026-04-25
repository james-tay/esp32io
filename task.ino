/*
   USER DEFINED TASK THREADS

   The user may start long running threads which interact with sensors,
   peripherals or just about any workload. Each user thread may want to expose
   workload related metrics (for example, expose an analog reading updated
   every 1 second). The various qualities of a thread may be configured across
   several files, but at the very least a "/<name>.thread" file must be present
   and its contents have the format,

     <thread_function>:<core>,[arg1,argN...]

   This defines a thread whose "main" function is defined by "thread_function"
   and will be run on "core". A optional comma separated list of arguments may
   be specified. If the thread exposes metrics (with optional labels), then
   the metric name may be configured in an optional file "/<name>.labels"
   which has the following format,

     <metric_name>[,<label1>="<value1>",<labelN>="<valueN>",...]

   Note that the "/<name>.labels" file is not used by the user thread, but is
   used by the webserver thread when rendering metrics. That is to say, all
   values exposed by a user thread will have identical metric names. The thread
   identifies the different values by applying different labels on each value
   exposed.

   For example, a thread named "env1" which exposes 2 values. Internally, it
   applies the labels 'measurement="temperature"' and 'measurement="humidity"'
   Let's say we have the file named "/env1.labels" with the contents,

     sensor_env,model="dht22",location="bath"

   Then the metrics which will be exposed will look like this,

     sensor_env{model="dht22",location="bath",measurement="temperature"} 22.3
     sensor_env{model="dht22",location="bath",measurement="humidity"} 76.5

   THREAD STATES AND LIFECYCLE

   The functions in this file focus on managing these user task threads. Each
   user task thread has a "S_UserThread" data structure which tracks state,
   configuration, exposed metrics, etc. For the duration of the user's task
   thread, we will call its "thread_function". This "thread_function" may do
   some work and return quickly, or it may not return for a very long time.
   A "loop" counter will be incremented each time "thread_function" is called.
   The life cycle of a user task thread includes the following states,

     (managed by us)
     UTHREAD_IDLE       - thread is not running
     UTHREAD_STARTING   - allocated for work, configuration in progress

     (set by user thread once it starts)
     UTHREAD_RUNNING    - thread is now running user's task

     (set by us)
     UTHREAD_WRAPUP     - thread is told to perform cleanup and terminate

     (set by user thread once it is done)
     UTHREAD_STOPPED    - thread is no longer doing any work

   Threads are terminated by us using "vTaskDelete()". If the user forcefully
   terminates a thread, the thread's state will be set to THREAD_WRAPUP, and
   the thread will have some amount of time to finish up. A thread may also
   choose to self terminate (eg, incorrect configuration or permanent error).
   In this case, the thread sets its state to UTHREAD_STOPPED and returns. The
   user thread function won't get called anymore but the (freeRTOS) thread is
   still running. The main thread periodically scans for this and cleans up
   the thread with a "vTaskDelete()".

   THREAD CONFIGURATION DATA STRUCTURES

   When a thread is started up, the user's static configuration is loaded from
   a file(s) into static buffers in the thread's data structures. These static
   buffers must be the reference point for all subsequent uses of thread
   configurations. A thread may choose to expose an arbituary number of values
   or metrics during its work. The individual metrics in turn may have labels.
   For example, in the file "/hello.thread", we have the contents,

     myfunc:1,50,60,70

   This line gets loaded into the thread's "conf" buffer initially, but gets
   parsed into separate strings. Eventually the "conf" buffer looks like this,

     myfunc<\00>1<\00>50<\00>60<\00>70<\x00>
*/

/*
   This function is called from "f_handle_core_metrics()". Our job is to
   return the number of user task threads in the UTHREAD_RUNNING state.
*/

int f_task_running()
{
  int total = 0 ;
  for (int slot=0 ; slot < DEF_MAX_USER_THREADS ; slot++)
    if (G_runtime->utask[slot].state == UTHREAD_RUNNING)
      total++ ;
  return(total) ;
}

/*
   This function is called from "f_task_cmd()". Our job is to print out some
   help references for all supported user task thread functions.
*/

void f_task_help(int idx)
{
  strncpy(G_runtime->worker[idx].result_msg,
          "[Thread Config Files]\r\n"
          "/init.thread         system default boot up tasks\r\n"
          "/<name>.thread       configures thread function and params\r\n"
          "/<name>.labels       set metric name and labels (optional)\r\n"
          "\r\n"
          "[User Task Threads - Config Refrence]\r\n"
          "ft_aread:<c>,<pollMs>,<inPin>,<pwrPin>[,<loThres>,<hiThres>]\r\n"
          "ft_dht22:<c>,<dataPin>,<pwrPin>,<intervalSecs>\r\n"
          "ft_dread:<c>,<pollMs>,<inPin>,<pwrPin>,<0|1=pullup>[,<thresMs>]\r\n"
          "ft_ds18b20:<c>,<dataPin>,<pwrPin>,<intervalSecs>\r\n"
          "ft_serial:<c>,<tcpPort>,<baud>,<rxPin>,<txPin>,<pollMs>\r\n"
          "ft_utasks:<c>,<filename>\r\n"
          "ft_wd:<c>,<startupSecs>,<intervalSecs>,<noActivitySecs>\r\n",
          BUF_LEN_WORKER_RESULT) ;
  G_runtime->worker[idx].result_code = 400 ;
}

/*
   This function is called from "f_task_cmd()". Our job is to list all threads
   which are not idle and to print info including their "status" buffer.
*/

void f_task_list(int idx)
{
  int remainder ;
  char line[BUF_LEN_LINE], *s_state=NULL ;
  long long age_secs ;
  long long now = esp_timer_get_time() ;

  for (int slot=0 ; slot < DEF_MAX_USER_THREADS ; slot++)
    if (strlen(G_runtime->utask[slot].name) > 0)
    {
      switch(G_runtime->utask[slot].state)
      {
        case UTHREAD_IDLE:
          s_state = "idle" ; break ;
        case UTHREAD_STARTING:
          s_state = "starting" ; break ;
        case UTHREAD_RUNNING:
          s_state = "running" ; break ;
        case UTHREAD_WRAPUP:
          s_state = "wrapup" ; break ;
        case UTHREAD_STOPPED:
          s_state = "stopped" ; break ;
        default:
          s_state = "unknown" ; break ;
      }
      age_secs = (now - G_runtime->utask[slot].ts_start) / 1000000 ;
      snprintf(line, BUF_LEN_LINE,
               "slot:%d %s:%d,%s age:%llds loop:%lld status:%s\r\n",
               slot,
               G_runtime->utask[slot].name, G_runtime->utask[slot].core,
               s_state, age_secs,
               G_runtime->utask[slot].loop, G_runtime->utask[slot].status) ;
      remainder = BUF_LEN_WORKER_RESULT -
                    strlen(G_runtime->worker[idx].result_msg) - 1 ;
      strncat(G_runtime->worker[idx].result_msg, line, remainder) ;
    }
  G_runtime->worker[idx].result_code = 200 ;
}

/*
   This is the lifecycle of a thread created by "xTaskCreatePinnedToCore()"
   in "f_task_start()".
*/

void f_user_thread_lifecycle(void *param)
{
  S_UserThread *self = (S_UserThread*) param ;
  self->ts_start = esp_timer_get_time() ;

  if (G_runtime->config.debug)
    Serial.printf("DEBUG: f_user_thread_lifecycle() name:%s ft_addr:%x\r\n",
                  self->name, self->ft_addr) ;

  // user thread's main loop

  while ((self->state == UTHREAD_STARTING) || (self->state == UTHREAD_RUNNING))
  {
    self->ft_addr(self) ;
    self->loop++ ;
    delay(1) ;
  }

  self->state = UTHREAD_STOPPED ; // set this, if user thread didn't already

  while (1) // await death, freeRTOS doesn't like thread functions to return
    delay(1000) ;
}

/*
   This function is called from "f_task_start()". Our job is to locate the
   function "ft_name" and place its address into the "ft_addr" of the specified
   user thread "slot". We return 1 on success, otherwise 0.
*/

int f_set_ft_addr(int slot, char *ft_name)
{
  if (strcmp(ft_name, "ft_aread") == 0)
    G_runtime->utask[slot].ft_addr = ft_aread ;
  else
  if (strcmp(ft_name, "ft_dread") == 0)
    G_runtime->utask[slot].ft_addr = ft_dread ;
  else
  if (strcmp(ft_name, "ft_dht22") == 0)
    G_runtime->utask[slot].ft_addr = ft_dht22 ;
  else
  if (strcmp(ft_name, "ft_ds18b20") == 0)
    G_runtime->utask[slot].ft_addr = ft_ds18b20 ;
  else
  if (strcmp(ft_name, "ft_serial") == 0)
    G_runtime->utask[slot].ft_addr = ft_serial ;
  else
  if (strcmp(ft_name, "ft_utasks") == 0)
    G_runtime->utask[slot].ft_addr = ft_utasks ;
  else
  if (strcmp(ft_name, "ft_wd") == 0)
    G_runtime->utask[slot].ft_addr = ft_wd ;

  if (G_runtime->utask[slot].ft_addr != NULL)
    return(1) ;
  else
    return(0) ;
}

/*
   This function is called from "f_task_start()" when we're ready to start
   a user thread. Our job is to perform this action and set the "idx" worker's
   "result_code" and "result_msg" accordingly.
*/

void f_task_create(int idx, int slot, int core, char *name)
{
  if (G_runtime->config.debug)
    Serial.printf("DEBUG: f_task_create() name:%s ft_addr:%x\r\n",
                  name, G_runtime->utask[slot].ft_addr) ;

  if (xTaskCreatePinnedToCore (
        f_user_thread_lifecycle,        // function to run
        name,                           // thread's name
        DEF_STACKSIZE_UTHREAD,          // thread's stacksize
        &G_runtime->utask[slot],        // param to pass into thread
        DEF_USER_THREAD_PRIORITY,       // priority (higher is more important)
        &G_runtime->utask[slot].tid,    // task handle
        core                            // core ID
        ) == pdPASS)
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "Started thread '%s' on core %d.\r\n", name, core) ;
    G_runtime->worker[idx].result_code = 200 ;

    if (G_runtime->config.debug)
      Serial.printf("DEBUG: f_task_create() "
                    "slot:%d name:%s core:%d tid:%d\r\n",
                    slot, name, core, G_runtime->utask[slot].tid) ;
  }
  else
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "Failed to start thread '%s'.\r\n", name) ;
    G_runtime->worker[idx].result_code = 500 ;
  }
}

/*
   This function is called from "f_task_cmd()", our job is to start a user
   thread "name". This mostly involves initializing and parsing configuration
   into the thread's "S_UserThread" structure and "xTaskCreatePinnedToCore()".
*/

void f_task_start(int idx, char *name)
{
  int slot ;
  char line[BUF_LEN_LINE], filename[DEF_MAX_FILENAME_LEN] ;
  char *pos=NULL, *spec=NULL, *args=NULL, *ft_name=NULL, *core=NULL ;

  // if a thread going by "name" is already running, don't continue

  for (slot=0 ; slot < DEF_MAX_USER_THREADS ; slot++)
    if (((G_runtime->utask[slot].state == UTHREAD_STARTING) ||
         (G_runtime->utask[slot].state == UTHREAD_RUNNING)) &&
        (strcmp(G_runtime->utask[slot].name, name) == 0))
    {
      snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
               "Thread '%s' is already running, loop %lld.\r\n",
               name, G_runtime->utask[slot].loop) ;
      G_runtime->worker[idx].result_code = 500 ;
      return ;
    }

  // read "/<name>.thread" and parse <thread_function>:<core>,[arg1,argN...]

  snprintf(filename, DEF_MAX_FILENAME_LEN, "/%s.thread", name) ;
  if (f_read_single_line(filename, line, BUF_LEN_LINE) < 1)
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "Cannot read thread config '%s'.\r\n", filename) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }

  // parse out "ft_name", "core" and "args" first

  pos = strstr(line, ",") ;
  if (pos)
    { spec = line ; args = pos + 1 ; *pos = 0 ; }
  if (spec)
  {
    pos = strstr(spec, ":") ;
    if (pos)
      { ft_name = spec ; core = pos + 1 ; *pos = 0 ; }
  }

  // check that we parsed everything we need

  if ((ft_name == NULL) || (core == NULL) || (args == NULL))
  {
    strncpy(G_runtime->worker[idx].result_msg, "Invalid thread config.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }

  // lock "L_uthread" and grab an availble "S_UserThread" structure

  xSemaphoreTake(G_runtime->L_uthread, portMAX_DELAY) ;
  for (slot=0 ; slot < DEF_MAX_USER_THREADS ; slot++)
    if (G_runtime->utask[slot].state == UTHREAD_IDLE)
      break ;
  if (slot == DEF_MAX_USER_THREADS)
  {
    xSemaphoreGive(G_runtime->L_uthread) ;
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "At thread limit %d, cannot start new thread.\r\n", slot) ;
    G_runtime->worker[idx].result_code = 500 ;
    return ;
  }

  // initialize the S_UserThread data structure

  memset(&G_runtime->utask[slot], 0, sizeof(S_UserThread)) ;
  G_runtime->utask[slot].state = UTHREAD_STARTING ;     // indicate as taken
  xSemaphoreGive(G_runtime->L_uthread) ;

  strncpy(G_runtime->utask[slot].conf, args, DEF_MAX_THREAD_CONF-1) ;
  strncpy(G_runtime->utask[slot].name, name, DEF_MAX_USER_THREAD_NAME-1) ;
  G_runtime->utask[slot].core = atoi(core) ;

  // try identify and set the user thread function

  if (f_set_ft_addr(slot, ft_name) == 0)
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "Could not locate '%s' function address.\r\n", ft_name) ;
    G_runtime->worker[idx].result_code = 400 ;
    G_runtime->utask[slot].state = UTHREAD_IDLE ;       // release this slot
    return ;
  }

  // have "args" point to the slot's "conf" since this is now a persistent
  // buffer. Use it to parse all user supplied task "args" into thread's
  // "in_args" array.

  int num=0 ;
  char *p=NULL ;
  args = G_runtime->utask[slot].conf ;
  G_runtime->utask[slot].in_args[num] = strtok_r(args, ",", &p) ;
  if (G_runtime->utask[slot].in_args[num] != NULL)
    for (num=1 ; num < DEF_MAX_THREAD_ARGS ; num++)
    {
      G_runtime->utask[slot].in_args[num] = strtok_r(NULL, ",", &p) ;
      if (G_runtime->utask[slot].in_args[num] == NULL)
        break ;
    }
  G_runtime->utask[slot].num_args = num ;

  f_task_create(idx, slot, atoi(core), name) ;   // ready to start user thread
}

/*
   This function is called from "f_task_cmd()", our job is to stop a user
   thread "name". This begins by setting its state to UTHREAD_WRAPUP before
   calling "vTaskDelete()".
*/

void f_task_stop(int idx, char *name)
{
  // see if a user thread going by "name" even exists

  int slot ;
  for (slot=0 ; slot < DEF_MAX_USER_THREADS ; slot++)
    if (((G_runtime->utask[slot].state == UTHREAD_STARTING) ||
         (G_runtime->utask[slot].state == UTHREAD_RUNNING)) &&
        (strcmp(G_runtime->utask[slot].name, name) == 0))
      break ;
  if (slot == DEF_MAX_USER_THREADS)
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "No '%s' thread currently active.\r\n", name) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }

  // put the thread into UTHREAD_WRAPUP, and give it some time to cleanup

  long long now = esp_timer_get_time() ;
  long long cutoff = now + (DEF_MAX_THREAD_WRAPUP_MSEC * 1000) ;
  G_runtime->utask[slot].state = UTHREAD_WRAPUP ;
  while ((now < cutoff) && (G_runtime->utask[slot].state == UTHREAD_WRAPUP))
  {
    delay(DEF_MAX_THREAD_WRAPUP_MSEC / 50) ;
    now = esp_timer_get_time() ;
  }

  // terminate thread and then mark this slot as available for work. To
  // prevent double killing, acquire "L_uthread" (ie, main "loop()" may have
  // beat us to terminating this thread).

  xSemaphoreTake(G_runtime->L_uthread, portMAX_DELAY) ;
  if ((G_runtime->utask[slot].state == UTHREAD_WRAPUP) or
      (G_runtime->utask[slot].state == UTHREAD_STOPPED))
  {
    vTaskDelete(G_runtime->utask[slot].tid) ;
    if (G_runtime->utask[slot].malloc_buf != NULL)      // thread local buffer
    {
      free(G_runtime->utask[slot].malloc_buf) ;
      G_runtime->utask[slot].malloc_buf = NULL ;
    }
    G_runtime->utask[slot].state = UTHREAD_IDLE ;
  }
  xSemaphoreGive(G_runtime->L_uthread) ;

  snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
           "Thread '%s' terminated after %lld loops.\r\n",
           name, G_runtime->utask[slot].loop) ;
  G_runtime->worker[idx].result_code = 200 ;
}

/*
   We're called from "f_action()" by the worker thread "idx" when the user
   wants to manage task threads. Parse the command and proceed accordingly.
*/

void f_task_cmd(int idx)
{
  char *tokens[3], *cmd=NULL, *action=NULL, *name=NULL ;
  int count = f_parse(G_runtime->worker[idx].cmd, tokens, 3) ;

  if (count == 1)
  {
    strncpy(G_runtime->worker[idx].result_msg,
            "task help          print user thread param references\r\n"
            "task list          list all current user threads\r\n"
            "task start <name>  start a thread\r\n"
            "task stop <name>   stop a thread\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }
  cmd = tokens[0] ;
  if (count > 1) action = tokens[1] ;
  if (count > 2) name = tokens[2] ;

  if (strcmp(action, "help") == 0)
    f_task_help(idx) ;
  else
  if (strcmp(action, "list") == 0)
    f_task_list(idx) ;
  else
  if ((strcmp(action, "start") == 0) && (name != NULL))
    f_task_start(idx, name) ;
  else
  if ((strcmp(action, "stop") == 0) && (name != NULL))
    f_task_stop(idx, name) ;
  else
  {
    strncpy(G_runtime->worker[idx].result_msg, "Invalid command.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 200 ;
  }
}
