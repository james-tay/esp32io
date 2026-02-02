/*
   We're called when this worker thread's "cmd" is a "wifi ...", thus our job
   is to perform wifi management. We parse out "key" and "value", and print
   out our help if "key" is invalid. Note that it's our responsibility to set
   the worker thread's "result_msg" and "result_code".
*/

void f_wifi(int idx)
{
  char line[BUF_LEN_WIFI_SSID+32] ; // generic buffer for rendering a line

  // parse our "wifi..." command, or print help

  char *tokens[2], *cmd=NULL, *key=NULL ;
  if (f_parse(G_runtime->worker[idx].cmd, tokens, 2) != 2)
  {
    strncpy(G_runtime->worker[idx].result_msg,
      "scan       search and report all Wifi SSIDs\r\n"
      "status     prints the current Wifi status\r\n",
      BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }
  cmd = tokens[0] ;
  key = tokens[1] ;

  if (strcmp(key, "scan") == 0)
  {
    char ssid[BUF_LEN_WIFI_SSID] ;
    int num_nets = WiFi.scanNetworks() ;

    sprintf(G_runtime->worker[idx].result_msg, "Found %d wifi networks.\r\n",
            num_nets) ;
    for (int i=0 ; i < num_nets ; i++)
    {
      WiFi.SSID(i).toCharArray(ssid, BUF_LEN_WIFI_SSID-1) ;
      sprintf(line, "%2d. ch %d, %d dBm [%s] %s\r\n",
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
    char s[40] ; // wifi status string
    unsigned char mac[6] ; // our wifi mac address

    int wifi_status = WiFi.status() ;
    switch (wifi_status)
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
        strcpy(s, "UNKNOWN\r\n") ; break ;
    }

    WiFi.macAddress(mac) ;
    sprintf(line, "Current status: %s\r\n", s) ;
    strcat(G_runtime->worker[idx].result_msg, line) ;
    sprintf(line, "Current SSID: %s\r\n", WiFi.SSID()) ;
    strcat(G_runtime->worker[idx].result_msg, line) ;
    sprintf(line, "Current BSSID: %s\r\n", WiFi.BSSIDstr().c_str()) ;
    strcat(G_runtime->worker[idx].result_msg, line) ;
    sprintf(line, "Current RSSI: %d dBm\r\n", WiFi.RSSI()) ;
    strcat(G_runtime->worker[idx].result_msg, line) ;
    sprintf(line, "Wifi mac: %x:%x:%x:%x:%x:%x\r\n",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]) ;
    strcat(G_runtime->worker[idx].result_msg, line) ;
    sprintf(line, "Wifi IP: %s/%s\r\n",
            WiFi.localIP().toString().c_str(),
             WiFi.subnetMask().toString().c_str()) ;
    strcat(G_runtime->worker[idx].result_msg, line) ;
    G_runtime->worker[idx].result_code = 200 ;
  }
  else                                  // user specified an invalid "key"
  {
    strcpy(G_runtime->worker[idx].result_msg, "Invalid key.\r\n") ;
    G_runtime->worker[idx].result_code = 400 ;
  }
}
