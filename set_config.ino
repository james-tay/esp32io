/*
   This is a convenience function called from "f_load_config()". Our job is
   to read the "int" value from "filename" and write it into "value". We
   return 1 if successful, otherwise 0 (probably due to file not found).
*/

int f_load_int(char *filename, int *value)
{
  char s[BUF_LEN_LINE] ;

  File f = SPIFFS.open(filename, "r") ;
  if (f)
  {
    int amt = f.readBytes(s, BUF_LEN_LINE-1) ;
    if (amt > 0)
    {
      s[amt] = 0 ;
      *value = atoi(s) ;
      return(1) ;
    }
  }
  return(0) ;
}

/*
   This is a convenience function called from "f_load_config()". Out job is
   to read the string value from "filename" and write it into "value", which
   is up to "size" bytes long. The number of bytes read is returned, or -1
   if something went wrong.
*/

int f_load_str(char *filename, char *value, int size)
{
  char s[size] ;
  File f = SPIFFS.open(filename, "r") ;
  if (f)
  {
    int amt = f.readBytes(s, size-1) ;
    if (amt >= 0)
    {
      s[amt] = 0 ;
      strncpy(value, s, amt) ;
      return(strlen(s)) ;
    }
  }
  return(-1) ;
}

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

  if (f_load_int("/debug.cfg", &G_runtime->config.debug))
    Serial.printf("BOOT: from /debug.cfg -> %d.\r\n",
                  G_runtime->config.debug) ;

  if (f_load_int("/init_delay_secs.cfg", &G_runtime->config.init_delay_secs))
    Serial.printf("BOOT: from /init_delay_secs.cfg -> %d.\r\n",
                  G_runtime->config.init_delay_secs) ;

  if (f_load_int("/mqtt_check_secs.cfg", &G_runtime->config.mqtt_check_secs))
    Serial.printf("BOOT: from /mqtt_check_secs.cfg -> %d.\r\n",
                  G_runtime->config.mqtt_check_secs) ;

  if (f_load_str("/mqtt_setup.cfg", G_runtime->config.mqtt_setup,
                 BUF_LEN_MQTT_SETUP) > 0)
    Serial.printf("BOOT: from /mqtt_setup.cfg -> (set).\r\n") ;

  if (f_load_str("/mqtt_topic.cfg", G_runtime->config.mqtt_topic,
                 BUF_LEN_MQTT_TOPIC) > 0)
    Serial.printf("BOOT: from /mqtt_topic.cfg -> %s.\r\n",
                  G_runtime->config.mqtt_topic) ;

  if (f_load_str("/wifi_ssid.cfg", G_runtime->config.wifi_ssid,
                 BUF_LEN_WIFI_SSID) > 0)
    Serial.printf("BOOT: from /wifi_ssid.cfg -> %s.\r\n",
                  G_runtime->config.wifi_ssid) ;

  if (f_load_str("/wifi_pw.cfg", G_runtime->config.wifi_pw,
                 BUF_LEN_WIFI_PW) > 0)
    Serial.printf("BOOT: from /wifi_pw.cfg -> (set).\r\n") ;

  if (f_load_int("/wifi_check_secs.cfg", &G_runtime->config.wifi_check_secs))
    Serial.printf("BOOT: from /wifi_check_secs.cfg -> %d.\r\n",
                  G_runtime->config.wifi_check_secs) ;
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
      "debug            <int>           debug mode\r\n"
      "init_delay_secs  <int>           delay before running /init.thread\r\n"
      "mqtt_check_secs  <int>           MQTT connection check interval\r\n"
      "mqtt_setup       <user>:<pw>@<server>:<port>\r\n"
      "mqtt_topic       <string>        MQTT publish topic\r\n"
      "wifi_ssid        <string>        Wifi SSID we connect to\r\n"
      "wifi_pw          <string>        Wifi SSID password\r\n"
      "wifi_check_secs  <int>           Wifi connection check interval\r\n",
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
  if (strcmp(key, "mqtt_check_secs") == 0)
  {
    G_runtime->config.mqtt_check_secs = atoi(value) ;
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "%s -> %d.\r\n", key, G_runtime->config.mqtt_check_secs) ;
  }
  else
  if (strcmp(key, "mqtt_setup") == 0)
  {
    memset(G_runtime->config.mqtt_setup, 0, BUF_LEN_MQTT_SETUP) ;
    strncpy(G_runtime->config.mqtt_setup, value, BUF_LEN_MQTT_SETUP-1) ;
  }
  else
  if (strcmp(key, "mqtt_topic") == 0)
  {
    memset(G_runtime->config.mqtt_topic, 0, BUF_LEN_MQTT_TOPIC) ;
    strncpy(G_runtime->config.mqtt_topic, value, BUF_LEN_MQTT_TOPIC-1) ;
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
