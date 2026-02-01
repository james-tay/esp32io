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

  char *cmd=NULL, *key=NULL, *value=NULL, *p=NULL ;

  cmd = strtok_r(G_runtime->worker[idx].cmd, " ", &p) ;
  if ((cmd == NULL) || (strcmp(cmd, "set") != 0))
  {
    strcpy(G_runtime->worker[idx].result_msg, "Unexpected command") ;
    G_runtime->worker[idx].result_code = 500 ;
    return ;
  }
  key = strtok_r(NULL, " ", &p) ;
  if (key != NULL)
    value = p ; // ie, the rest of the string

  if ((key == NULL) || (value == NULL) ||
      (strlen(key) < 1) || (strlen(value) < 1)) // invalid input, print usage
  {
    strncpy(G_runtime->worker[idx].result_msg,
      "wifi_ssid  <value>\r\n"
      "wifi_pw    <value>\r\n",
      BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }

  // if we made it here, then let's act on the supplied "key" and "value"

  snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
           "Updating '%s' %d bytes", key, strlen(value)) ;
  G_runtime->worker[idx].result_code = 200 ;

  if (strcmp(key, "wifi_ssid") == 0)
  {
    memset(G_runtime->config.wifi_ssid, 0, BUF_LEN_WIFI_SSID) ;
    strncpy(G_runtime->config.wifi_ssid, value, BUF_LEN_WIFI_SSID-1) ;
  }
  else
  if (strcmp(key, "wifi_pw") == 0)
  {
    memset(G_runtime->config.wifi_ssid, 0, BUF_LEN_WIFI_SSID) ;
    strncpy(G_runtime->config.wifi_ssid, value, BUF_LEN_WIFI_SSID-1) ;
  }
  else                                  // user specified an invalid "key"
  {
    strcpy(G_runtime->worker[idx].result_msg, "Invalid key.") ;
    G_runtime->worker[idx].result_code = 400 ;
  }
}
