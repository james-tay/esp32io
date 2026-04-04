/*
   This is a convenience function which returns the string value of the MQTT
   client's state.
*/

char *f_mqtt_state(int cur_state)
{
  char *state="unknown" ;

  switch(cur_state)
  {
    case MQTT_CONNECTION_TIMEOUT:                               // -4
      state = "MQTT_CONNECTION_TIMEOUT" ; break ;
    case MQTT_CONNECTION_LOST:                                  // -3
      state = "MQTT_CONNECTION_LOST" ; break ;
    case MQTT_CONNECT_FAILED:                                   // -2
      state = "MQTT_CONNECT_FAILED" ; break ;
    case MQTT_DISCONNECTED:                                     // -1
      state = "MQTT_DISCONNECTED" ; break ;
    case MQTT_CONNECTED:                                        // 0
      state = "MQTT_CONNECTED" ; break ;
    case MQTT_CONNECT_BAD_PROTOCOL:                             // 1
      state = "MQTT_CONNECT_BAD_PROTOCOL" ; break ;
    case MQTT_CONNECT_BAD_CLIENT_ID:                            // 2
      state = "MQTT_CONNECT_BAD_CLIENT_ID" ; break ;
    case MQTT_CONNECT_UNAVAILABLE:                              // 3
      state = "MQTT_CONNECT_UNAVAILABLE" ; break ;
    case MQTT_CONNECT_BAD_CREDENTIALS:                          // 4
      state = "MQTT_CONNECT_BAD_CREDENTIALS" ; break ;
    case MQTT_CONNECT_UNAUTHORIZED:                             // 5
      state = "MQTT_CONNECT_UNAUTHORIZED" ; break ;
  }
  return(state) ;
}

/*
   This function is called from "f_mqtt_cmd()" our job is to parse the
   "mqtt_setup" config and then attempt to connect to the MQTT server. In
   order to do this, we'll have to use "strtok_r()" to operate on the buffer.
*/

void f_mqtt_connect(int idx)
{
  #define PUBSUB_CLIENT_NAME "esp32io"  // how we identify ourself to server

  int fault=0, port ;
  char config[BUF_LEN_MQTT_SETUP], *p ;
  char *cfg_user=NULL, *cfg_pw=NULL, *cfg_server=NULL, *cfg_port=NULL ;

  // don't proceed if we're already connected

  if ((G_runtime->pubsub_state) && (G_psClient.connected()))
  {
    strncpy(G_runtime->worker[idx].result_msg, "MQTT already online.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 200 ;
    return ;
  }

  // recall that "mqtt_setup" should be "<user>:<pw>@<server>:<port>"

  if (strlen(G_runtime->config.mqtt_setup) < 1)
    fault = 1 ;
  else
    strcpy(config, G_runtime->config.mqtt_setup) ;

  if (fault==0)
    cfg_user = strtok_r(config, ":", &p) ;              // username
  if ((fault==0) && (cfg_user != NULL))
    cfg_pw = strtok_r(NULL, "@", &p) ;                  // password
  if ((fault==0) && (cfg_pw != NULL))
    cfg_server = strtok_r(NULL, ":", &p) ;              // server
  if ((fault==0) && (cfg_server != NULL))
    cfg_port = strtok_r(NULL, " ", &p) ;                // port
  if (cfg_port == NULL)
    fault = 1 ;
  else
  {
    port = atoi(cfg_port) ;
    if ((port < 1) || (port > 65535))
      fault = 1 ;
  }

  if (fault)
  {
    strncpy(G_runtime->worker[idx].result_msg, "Invalid 'mqtt_setup'.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }
  if (strlen(G_runtime->config.mqtt_topic) < 1)
  {
    strncpy(G_runtime->worker[idx].result_msg, "Invalid 'mqtt_topic'.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }

  // at this point, we have successfully parsed our MQTT configuration

  G_psClient.setServer(cfg_server, port) ;
  if (G_psClient.connect(PUBSUB_CLIENT_NAME, cfg_user, cfg_pw))
  {
    strncpy(G_runtime->worker[idx].result_msg, "Connected\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 200 ;
    G_runtime->pubsub_state = 1 ;
    G_runtime->mqtt_connect_ts = esp_timer_get_time() ;
    G_runtime->mqtt_connects++ ;
  }
  else
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "Connection failed - %s\r\n", f_mqtt_state(G_psClient.state())) ;
    G_runtime->worker[idx].result_code = 500 ;
    G_runtime->pubsub_state = 0 ;
    G_runtime->mqtt_connect_fails++ ;
  }
}

/*
   This function is called from "f_mqtt_cmd()". If our MQTT state is offline,
   then don't bother.
*/

void f_mqtt_status(int idx)
{
  G_runtime->worker[idx].result_code = 200 ; // always return success
  if (G_runtime->pubsub_state == 0)
  {
    strncpy(G_runtime->worker[idx].result_msg, "MQTT subsystem is off\r\n",
            BUF_LEN_WORKER_RESULT) ;
    return ;
  }

  char *status = "offline" ;
  if (G_psClient.connected())
    status = "online" ;
  snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
           "connected:%s state:%s pub_topic_prefix:%s\r\n",
           status, f_mqtt_state(G_psClient.state()),
           G_runtime->config.mqtt_topic) ;
}

/*
   This function is called from "f_action()" when this worker thread's "cmd"
   is a "mqtt ...", thus our job is to handle the MQTT subsystem.
*/

void f_mqtt_cmd(int idx)
{
  char *tokens[2], *cmd=NULL, *key=NULL ;
  if (f_parse(G_runtime->worker[idx].cmd, tokens, 2) != 2)
  {
    strncpy(G_runtime->worker[idx].result_msg,
            "connect    connect to MQTT server\r\n"
            "status     print our current MQTT status\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }
  cmd = tokens[0] ;             // this is always "mqtt"
  key = tokens[1] ;             // the MQTT task the user requested

  if (strcmp(key, "connect") == 0)
  {
    f_mqtt_connect(idx) ;
  }
  else
  if (strcmp(key, "status") == 0)
  {
    f_mqtt_status(idx) ;
  }
  else
  {
    strncpy(G_runtime->worker[idx].result_msg, "Invalid command.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
  }
}
