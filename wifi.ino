/*
   This function is supplied the output of "WiFi.status()" in "status". It
   then writes the string value of this status into "s". The caller must
   ensure that "s" is large enough.
*/

void f_wifi_status_string(int status, char *s)
{
  switch (status)
  {
    case WL_CONNECTED:
      strcpy(s, "WL_CONNECTED") ; break ;
    case WL_NO_SHIELD:
      strcpy(s, "WL_NO_SHIELD") ; break ;
    case WL_IDLE_STATUS:
      strcpy(s, "WL_IDLE_STATUS") ; break ;
    case WL_NO_SSID_AVAIL:
      strcpy(s, "WL_NO_SSID_AVAIL") ; break ;
    case WL_SCAN_COMPLETED:
      strcpy(s, "WL_SCAN_COMPLETED") ; break ;
    case WL_CONNECT_FAILED:
      strcpy(s, "WL_CONNECT_FAILED") ; break ;
    case WL_CONNECTION_LOST:
      strcpy(s, "WL_CONNECTION_LOST") ; break ;
    case WL_DISCONNECTED:
      strcpy(s, "WL_DISCONNECTED") ; break ;
    default:
      strcpy(s, "UNKNOWN") ; break ;
  }
}

/*
   This is a convenience function, called from "f_wifi_cmd()". Our job is
   to reset the wifi hardware, meaning that we perform a disconnect and
   shutdown of the wifi hardware, followed by bringing it back online again,
   ready to be connected.
*/

void f_wifi_reset()
{
  WiFi.disconnect(true, true) ;         // turn off radio & clear credentials
  WiFi.setAutoReconnect(false) ;        // don't try to reconnect
  WiFi.mode(WIFI_MODE_NULL) ;           // disable all wifi functionality
  delay(100) ;
  WiFi.mode(WIFI_STA) ;                 // bring wifi back online
}

/*
   This function is supplied a wifi "ssid" and "pw". Our job is to scan for
   the AP with the best RSSI and then connect to it. On success we return
   the dBm value of that AP (which is a negative number). If something went
   wrong we return the values of "WiFi.status()" plus 10. This is because its
   values begin with 0 onwards. Eg, 0=WL_IDLE_STATUS, 1=WL_NO_SSID_AVAIL, etc.
   While we're scanning for "ssid", if we can't find it, then this function
   returns 1.
*/

int f_wifi_connect(char *ssid, char *pw)
{
  #define INVALID_RSSI -255
  int best_rssi=INVALID_RSSI, best_channel ;
  char cur_ssid[BUF_LEN_WIFI_SSID] ;
  unsigned char best_bssid[6] ;

  int num_nets = WiFi.scanNetworks() ;
  for (int i=0 ; i < num_nets ; i++)
  {
    WiFi.SSID(i).toCharArray(cur_ssid, BUF_LEN_WIFI_SSID) ;
    if (strcmp(cur_ssid, ssid) == 0)
    {
      if (G_runtime->config.debug)
        Serial.printf("DEBUG: f_wifi_connect() %s on chan:%d at %d dBm.\r\n",
                      WiFi.BSSIDstr(i).c_str(), WiFi.channel(i), WiFi.RSSI(i)) ;

      if (WiFi.RSSI(i) > best_rssi)
      {
        // this just might be the best AP to connect to

        best_rssi = WiFi.RSSI(i) ;
        best_channel = WiFi.channel(i) ;
        memcpy(best_bssid, WiFi.BSSID(i), 6) ;
      }
    }
  }

  if (best_rssi == INVALID_RSSI) // did not find "ssid"
    return(1) ;

  // if we got here, then try to connect to the best AP

  WiFi.begin(ssid, pw, best_channel, best_bssid, true) ;

  int retries = DEF_WIFI_BEGIN_WAIT_SECS ;
  int wifi_status ;
  while (retries > 0)
  {
    wifi_status = WiFi.status() ;
    if (G_runtime->config.debug)
    {
      char s[32] ;
      f_wifi_status_string(wifi_status, s) ;
      Serial.printf("DEBUG: f_wifi_connect() status:%s\r\n", s) ;
    }

    if ((wifi_status == WL_CONNECTED) || (wifi_status == WL_CONNECT_FAILED))
      break ;
    delay(1000) ;
    retries-- ;
  }
  if (wifi_status == WL_CONNECTED)
    return(best_rssi) ;
  else
    return(wifi_status + 10) ;
}

/*
   This function is called from "f_wifi()". Our job is to render our current
   wifi status into "result_msg" of worker thread "idx".
*/

void f_wifi_status(int idx)
{
  char s[40] ;                          // wifi status string
  char line[BUF_LEN_WIFI_SSID+80] ;     // report one detected SSID
  unsigned char mac[6] ;                // our wifi mac address

  WiFi.macAddress(mac) ;
  f_wifi_status_string(WiFi.status(), s) ;
  sprintf(line, "Current status: %s\r\n", s) ;
  strcpy(G_runtime->worker[idx].result_msg, line) ;
  sprintf(line, "Current SSID: %s\r\n", WiFi.SSID()) ;
  strcat(G_runtime->worker[idx].result_msg, line) ;
  sprintf(line, "Current BSSID: %s\r\n", WiFi.BSSIDstr().c_str()) ;
  strcat(G_runtime->worker[idx].result_msg, line) ;
  sprintf(line, "Current RSSI: %d dBm\r\n", WiFi.RSSI()) ;
  strcat(G_runtime->worker[idx].result_msg, line) ;
  sprintf(line, "Wifi channel: %d\r\n", WiFi.channel()) ;
  strcat(G_runtime->worker[idx].result_msg, line) ;
  sprintf(line, "Wifi mac: %x:%x:%x:%x:%x:%x\r\n",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]) ;
  strcat(G_runtime->worker[idx].result_msg, line) ;
  sprintf(line, "Wifi IP: %s/%s\r\n",
          WiFi.localIP().toString().c_str(),
           WiFi.subnetMask().toString().c_str()) ;
  strcat(G_runtime->worker[idx].result_msg, line) ;
}

/*
   We're called when this worker thread's "cmd" is a "wifi ...", thus our job
   is to perform wifi management. We parse out "key" and "value", and print
   out our help if "key" is invalid. Note that it's our responsibility to set
   the worker thread's "result_msg" and "result_code".
*/

void f_wifi_cmd(int idx)
{
  char line[BUF_LEN_WIFI_SSID+80] ; // generic buffer for rendering a line

  // parse our "wifi..." command, or print help

  char *tokens[2], *cmd=NULL, *key=NULL ;
  if (f_parse(G_runtime->worker[idx].cmd, tokens, 2) != 2)
  {
    strncpy(G_runtime->worker[idx].result_msg,
      "connect          connect to currently configured AP\r\n"
      "disconnect       disconnect and reset wifi radio\r\n"
      "scan             search and report all Wifi SSIDs\r\n"
      "status           prints the current Wifi status\r\n",
      BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }
  cmd = tokens[0] ;
  key = tokens[1] ;
  G_runtime->worker[idx].result_code = 200 ;    // assume success first

  if (strcmp(key, "connect") == 0)
  {
    if ((strlen(G_runtime->config.wifi_ssid) < 1) ||
        (strlen(G_runtime->config.wifi_pw) < 1))
    {
      strcpy(G_runtime->worker[idx].result_msg,
             "Please set 'wifi_ssid' and 'wifi_pw' first.\r\n") ;
      G_runtime->worker[idx].result_code = 400 ;
    }
    else
    {
      int result_dbm = f_wifi_connect(G_runtime->config.wifi_ssid,
                                      G_runtime->config.wifi_pw) ;
      if (result_dbm > 0)       // this actually means error
      {
        if (result_dbm == 1)
          strcpy(line, "SSID not available") ;
        else
          f_wifi_status_string(result_dbm - 10, line) ;
        snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
                 "Could not connect to '%s' - %s\r\n",
                 G_runtime->config.wifi_ssid, line) ;
        G_runtime->worker[idx].result_code = 500 ;
        f_wifi_reset() ;
      }
      else                      // normal dBm is almost always negative
      {
        snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
                 "Connected to '%s' at %d dBm.\r\n",
                 G_runtime->config.wifi_ssid, result_dbm) ;
      }
    }
  }
  else
  if (strcmp(key, "disconnect") == 0)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      sprintf(G_runtime->worker[idx].result_msg, "Disconnecting from %s.\r\n",
              WiFi.SSID()) ;
    }
    else
      strcpy(G_runtime->worker[idx].result_msg, "Not connected.\r\n") ;
    f_wifi_reset() ;
  }
  else
  if (strcmp(key, "scan") == 0)
  {
    char ssid[BUF_LEN_WIFI_SSID] ;
    int num_nets = WiFi.scanNetworks() ;

    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "Found %d wifi networks.\r\n", num_nets) ;
    for (int i=0 ; i < num_nets ; i++)
    {
      WiFi.SSID(i).toCharArray(ssid, BUF_LEN_WIFI_SSID-1) ;
      ssid[BUF_LEN_WIFI_SSID-1] = 0 ;
      snprintf(line, BUF_LEN_WIFI_SSID+80, "%2d. ch %d, %d dBm [%s] %s\r\n",
               i+1, WiFi.channel(i), WiFi.RSSI(i), ssid,
               WiFi.BSSIDstr(i).c_str()) ;
      strncat(G_runtime->worker[idx].result_msg, line,
              BUF_LEN_WORKER_RESULT -
              strlen(G_runtime->worker[idx].result_msg)) ;
    }
  }
  else
  if (strcmp(key, "status") == 0)
  {
    f_wifi_status(idx) ;
  }
  else                                  // user specified an invalid "key"
  {
    strcpy(G_runtime->worker[idx].result_msg, "Invalid key.\r\n") ;
    G_runtime->worker[idx].result_code = 400 ;
  }
}
