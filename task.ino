/*
   We're called from "f_action()" when the user wants to manage task threads.
   Parse the command and proceed accordingly.
*/

void f_task_cmd(int idx)
{
  char *tokens[2], *cmd=NULL, *action=NULL ;
  int count = f_parse(G_runtime->worker[idx].cmd, tokens, 2) ;

  if (count == 1)
  {
    strncpy(G_runtime->worker[idx].result_msg,
            "task list          list all current user threads\r\n"
            "task start <name>  start a thread\r\n"
            "task stop <name>   stop a thread\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
  }


}
