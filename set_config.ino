/*
   This function is called from setup() very early in our boot up stage, just
   after our SPIFFS has been mounted. Our job is to identify any "/<name>.cfg"
   files and to load them into "G_runtime->config" where applicable.
*/

void f_load_config()
{
  File f ;
  char s[BUF_LEN_LINE] ;

  // go thru each of the fields in S_ConfigData.

  f = SPIFFS.open("/debug.cfg", "r") ;                  // debug
  if (f)
  {
    int amt = f.readBytes(s, BUF_LEN_LINE-1) ;
    if (amt > 0)
    {
      G_runtime->config.debug = atoi(s) ;
      Serial.printf("BOOT: read from /debug.cfg -> %d.\r\n",
                    G_runtime->config.debug) ;
    }
    f.close() ;
  }
  f = SPIFFS.open("/init_delay_secs.cfg", "r") ;        // init_delay_secs
  if (f)
  {
    int amt = f.readBytes(s, BUF_LEN_LINE-1) ;
    if (amt > 0)
    {
      G_runtime->config.init_delay_secs = atoi(s) ;
      Serial.printf("BOOT: read from /init_delay_secs.cfg -> %d.\r\n",
                    G_runtime->config.init_delay_secs) ;
    }
    f.close() ;
  }
  f = SPIFFS.open("/wifi_ssid.cfg", "r") ;              // wifi_ssid
  if (f)
  {
    int amt = f.readBytes(G_runtime->config.wifi_ssid, BUF_LEN_WIFI_SSID-1) ;
    if (amt > 0)
    {
      G_runtime->config.wifi_ssid[amt] = 0 ;
      Serial.printf("BOOT: read %d bytes from /wifi_ssid.cfg.\r\n", amt) ;
    }
    else
      G_runtime->config.wifi_ssid[0] = 0 ;
    f.close() ;
  }
  f = SPIFFS.open("/wifi_pw.cfg", "r") ;                // wifi_pw
  if (f)
  {
    int amt = f.readBytes(G_runtime->config.wifi_pw, BUF_LEN_WIFI_PW-1) ;
    if (amt > 0)
    {
      G_runtime->config.wifi_pw[amt] = 0 ;
      Serial.printf("BOOT: read %d bytes from /wifi_pw.cfg.\r\n", amt) ;
    }
    else
      G_runtime->config.wifi_pw[0] = 0 ;
    f.close() ;
  }
  f = SPIFFS.open("/wifi_check_secs.cfg", "r") ;        // wifi_check_secs
  if (f)
  {
    int amt = f.readBytes(s, BUF_LEN_LINE-1) ;
    if (amt > 0)
    {
      G_runtime->config.wifi_check_secs = atoi(s) ;
      Serial.printf("BOOT: read from /wifi_check_secs.cfg -> %d.\r\n",
                    G_runtime->config.wifi_check_secs) ;
    }
    f.close() ;
  }
}

/*
   We're called when this worker thread's "cmd" is a "set ...", thus our job
   is to perform this work, or print a help message if we're called with
   incorrect arguments. Note that it's our responsibility to set the worker
   thread's "result_msg" and "result_code".
*/

void f_set_cmd(int idx)
{
  // parse our "set..." command into fields "cmd", "key" and "value". We
  // expect that "cmd" is "set".

  char *tokens[3], *cmd=NULL, *key=NULL, *value=NULL ;
  if (f_parse(G_runtime->worker[idx].cmd, tokens, 3) != 3)      // print help
  {
    strncpy(G_runtime->worker[idx].result_msg,
      "debug            <int>\r\n"
      "init_delay_secs  <int>\r\n"
      "wifi_ssid        <string>\r\n"
      "wifi_pw          <string>\r\n"
      "wifi_check_secs  <int>\r\n",
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
             "%s -> %d.\r\n", key, G_runtime->config.debug) ;
  }
  else
  if (strcmp(key, "init_delay_secs") == 0)
  {
    G_runtime->config.init_delay_secs = atoi(value) ;
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "%s -> %d.\r\n", key, G_runtime->config.init_delay_secs) ;
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
    memset(G_runtime->config.wifi_pw, 0, BUF_LEN_WIFI_PW) ;
    strncpy(G_runtime->config.wifi_pw, value, BUF_LEN_WIFI_PW-1) ;
  }
  else
  if (strcmp(key, "wifi_check_secs") == 0)
  {
    G_runtime->config.wifi_check_secs = atoi(value) ;
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "%s -> %d.\r\n", key, G_runtime->config.wifi_check_secs) ;
  }
  else                                  // user specified an invalid "key"
  {
    strncpy(G_runtime->worker[idx].result_msg, "Invalid command.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
  }
}
