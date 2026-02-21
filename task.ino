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
     THREAD_READY       - thread is not running
     THREAD_STARTING    - allocated for work, configuration in progress

     (set by user thread once it starts)
     THREAD_RUNNING     - thread is now running user's task

     (set by us)
     THREAD_WRAPUP      - thread is told to perform cleanup and terminate

     (set by user thread once it is done)
     THREAD_STOPPED     - thread is no longer doing any work

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
   This is a convenience function which reads from "filename", pullint up to
   "max_size" bytes into "buf". On success the number of bytes is returned,
   otherwise -1 to indicate something went wrong (probably no such file).
*/

int f_read_single_line(char *filename, char *buf, int max_size)
{
  if (G_runtime->config.debug)
    Serial.printf("DEBUG: f_read_single_line() filename:%s max_size:%d\r\n",
                  filename, max_size) ;


  return(0) ;
}

/*
   This function is called from "f_task_cmd()", our job is to start a user
   thread "name". This mostly involves initializing and parsing configuration
   into the thread's "S_UserThread" structure and "xTaskCreatePinnedToCore()".
*/

void f_task_start(int idx, char *name)
{
  int amt ;
  char line[BUF_LEN_LINE], filename[DEF_MAX_FILENAME_LEN] ;

  // try read the file "/<name>.thread"

  snprintf(filename, DEF_MAX_FILENAME_LEN, "/%s.thread", name) ;
  amt = f_read_single_line(filename, line, BUF_LEN_LINE) ;





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
