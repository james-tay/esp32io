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
   these may be configured in an optional file "</<name>.tags" which has the
   following format,

     <metric_name>[,<label1>="<value1>",<labelN>="<valueN>",...]

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

   Threads are terminated by us using "vTaskDelete()". Before forcefully
   terminating them, the thread's state will be set to THREAD_WRAPUP, and
   the thread will have some amount of time to finish up.

   THREAD CONFIGURATION DATA STRUCTURES

   When a thread is started up, the user's static configuration is loaded from
   a file(s) into static buffers in the thread's data structures. These static
   buffers must be the reference point for all subsequent uses of thread
   configurations. A thread may choose to expose an arbituary number of values
   or metrics during its work. The individual metrics in turn may have labels.


*/

/*
   This is the lifecycle of a thread created by "xTaskCreatePinnedToCore()"
   in "f_task_start()".
*/

void f_user_thread_lifecycle(void *param)
{
  S_UserThread *self = (S_UserThread*) param ;

  while (1)
    delay(1000) ;




}

/*
   This is a convenience function which reads from "filename", pullint up to
   "max_size" bytes into "buf". On success the number of bytes is returned,
   otherwise -1 to indicate something went wrong (probably no such file).
*/

int f_read_single_line(char *filename, char *buf, int max_size)
{
  File f = SPIFFS.open(filename, "r") ;
  if (f.size() < 1)
    return(-1) ;                                // file is probabl absent

  int amt = f.readBytes(buf, max_size-1) ;
  if (amt > 0)
    buf[amt] = 0 ;

  f.close() ;
  return(amt) ;
}

/*
   This function is called from "f_task_cmd()", our job is to start a user
   thread "name". This mostly involves initializing and parsing configuration
   into the thread's "S_UserThread" structure and "xTaskCreatePinnedToCore()".
*/

void f_task_start(int idx, char *name)
{
  int amt, slot ;
  char line[BUF_LEN_LINE], filename[DEF_MAX_FILENAME_LEN] ;
  char *pos=NULL, *spec=NULL, *args=NULL, *ft_name=NULL, *core=NULL ;

  // read "/<name>.thread" and parse <thread_function>:<core>,[arg1,argN...]

  snprintf(filename, DEF_MAX_FILENAME_LEN, "/%s.thread", name) ;
  if (f_read_single_line(filename, line, BUF_LEN_LINE) < 1)
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "Cannot read thread config '%s'.\r\n", filename) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }

  pos = strstr(line, ",") ;
  if (pos)
    { spec = line ; args = pos + 1 ; *pos = 0 ; }
  if (spec)
  {
    pos = strstr(spec, ":") ;
    if (pos)
      { ft_name = spec ; core = pos + 1 ; *pos = 0 ; }
  }

  // identify and configure an "S_UserThread" structure

  for (slot=0 ; slot < DEF_MAX_USER_THREADS ; slot++)
    if (G_runtime->utask[slot].state == UTHREAD_IDLE)
      break ;
  if (slot == DEF_MAX_USER_THREADS)
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "At thread limit %d, cannot start new thread.\r\n", slot) ;
    G_runtime->worker[idx].result_code = 500 ;
    return ;
  }

  // initialize the S_UserThread data structure

  memset(&G_runtime->utask[slot], 0, sizeof(S_UserThread)) ;
  G_runtime->utask[slot].state = UTHREAD_STARTING ;
  strncpy(G_runtime->utask[slot].conf, args, DEF_MAX_THREAD_CONF) ;
  strncpy(G_runtime->utask[slot].name, name, DEF_MAX_USER_THREAD_NAME) ;

  // start the user thread

  if (xTaskCreatePinnedToCore (
        f_user_thread_lifecycle,        // function to run
        name,                           // thread's name
        DEF_THREAD_STACKSIZE,           // thread's stacksize
        &G_runtime->utask[slot],        // param to pass into thread
        DEF_USER_THREAD_PRIORITY,       // priority (higher is more important)
        &G_runtime->utask[slot].tid,    // task handle
        atoi(core)                      // core ID
        ) == pdPASS)
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "Started thread '%s' on core %d.\r\n", name, atoi(core)) ;
    G_runtime->worker[idx].result_code = 200 ;

    if (G_runtime->config.debug)
      Serial.printf("DEBUG: f_task_start() "
                    "slot:%d name:%s core:%s args:%s tid:%d\r\n",
                    slot, ft_name, core, args, G_runtime->utask[slot].tid) ;
  }
  else
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "Failed to start thread '%s'.\r\n", name) ;
    G_runtime->worker[idx].result_code = 500 ;
  }
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

  if ((strcmp(action, "start") == 0) && (name != NULL))
    f_task_start(idx, name) ;
  else
  {
    strncpy(G_runtime->worker[idx].result_msg, "Invalid command.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 200 ;
  }
}
