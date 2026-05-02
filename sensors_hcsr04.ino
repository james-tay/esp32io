/*
   This function is called from "f_action()" when a worker thread wakes up to
   process this command. Our job is to manage the user command, which involves
   printing the help message or parsing the command arguments.
*/

void f_hcsr04_cmd(int idx)
{
  // parse the "hcsr04..." command, or print help

  char *tokens[4] ;
  int trig_pin, echo_pin, samples ;
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
  




}
