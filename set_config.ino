/*
   We're called when this worker thread's "cmd" is a "set ...", thus our job
   is to perform this work, or print a help message if we're called with
   incorrect arguments. Note that it's our responsibility to set the worker
   thread's "result_msg" and "result_code".
*/

void f_set_config(int idx)
{
  // parse our "set..." command into fields "cmd", "key" and "value". We
  // expect that "cmd" is "set".

  char *tokens[3], *cmd=NULL, *key=NULL, *value=NULL ;
  if (f_parse(G_runtime->worker[idx].cmd, tokens, 3) != 3)      // print help
  {
    strncpy(G_runtime->worker[idx].result_msg,
      "debug      <value>\r\n"
      "wifi_ssid  <value>\r\n"
      "wifi_pw    <value>\r\n",
      BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }
  cmd = tokens[0] ;
  key = tokens[1] ;
  value = tokens[2] ;

  // if we made it here, then let's act on the supplied "key" and "value"

  snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
           "Updating '%s' %d bytes\r\n", key, strlen(value)) ;
  G_runtime->worker[idx].result_code = 200 ;

  if (strcmp(key, "debug") == 0)
  {
    G_runtime->config.debug = atoi(value) ;
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "Updating '%s' to %d.\r\n", key, G_runtime->config.debug) ;
  }
  else
  if (strcmp(key, "wifi_ssid") == 0)
  {
    memset(G_runtime->config.wifi_ssid, 0, BUF_LEN_WIFI_SSID) ;
    strncpy(G_runtime->config.wifi_ssid, value, BUF_LEN_WIFI_SSID-1) ;
  }
  else
  if (strcmp(key, "wifi_pw") == 0)
  {
    memset(G_runtime->config.wifi_pw, 0, BUF_LEN_WIFI_SSID) ;
    strncpy(G_runtime->config.wifi_pw, value, BUF_LEN_WIFI_SSID-1) ;
  }
  else                                  // user specified an invalid "key"
  {
    strcpy(G_runtime->worker[idx].result_msg, "Invalid key.\r\n") ;
    G_runtime->worker[idx].result_code = 400 ;
  }
}
