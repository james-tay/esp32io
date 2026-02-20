#define OTA_PROTO "http://"             // the only protocol we support
#define OTA_PORT 80                     // the HTTP port we support
#define OTA_POLL_MSEC 50                // how often we poll for response
#define OTA_TIMEOUT_SECS 10             // how long we'll wait for a response
#define OTA_MIN_CONTENT_LENGTH 262144   // minimum possible firmware size
#define OTA_CONTENT_TYPE "application/octet-stream"     // ie, binary data

void f_ota_download(int idx, WiFiClient client, int content_length)
{



}

/*
   This function is called from "f_ota_cmd()". Our job is to connect to "host"
   and pull our firmware image at "uri". The outcome is recorded in the worker
   thread at "idx".
*/

void f_ota_http_get(int idx, char *host, char *uri)
{
  char buf[BUF_LEN_WEBCLIENT] ;
  WiFiClient client ;

  // now try perform the HTTP GET using "WiFiClient" for the TCP connection

  if (client.connect(host, OTA_PORT) == false)
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "Cannot connect to '%s' on port %d.\r\n", host, OTA_PORT) ;
    G_runtime->worker[idx].result_code = 500 ;
    return ;
  }
  snprintf(buf, BUF_LEN_WEBCLIENT,
           "GET /%s HTTP/1.1\n"
           "Host: %s\n"
           "Cache-Control: no-cache\n"
           "Connection: close\n\n",
           uri, host) ;
  client.print(buf) ;

  // sit here polling until we get a response from the webserver

  unsigned long now = millis() ;
  unsigned long cut_off = now + (OTA_TIMEOUT_SECS * 1000) ;
  while ((client.available() == 0) && (now < cut_off))
  {
    delay(OTA_POLL_MSEC) ;
    now = millis() ;
  }
  if (now >= cut_off)
  {
    strncpy(G_runtime->worker[idx].result_msg, "No response from server.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 500 ;
    client.stop() ;
    return ;
  }

  // data is arriving, read the HTTP response header line by line

  int content_length=0 ;
  while ((client.available() > 0) && (now < cut_off))
  {
    String s = client.readStringUntil('\n') ;
    s.trim() ;
    if (s.length() == 0)                                // end of HTTP headers
      break ;
    if (G_runtime->config.debug)
      Serial.printf("DEBUG: f_ota_http_get(): %s\r\n", s.c_str()) ;

    if ((s.startsWith("HTTP/1.1")) && (s.indexOf("200") < 0))
    {
      snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
               "Received - %s\r\n", s.c_str()) ;
      G_runtime->worker[idx].result_code = 500 ;
      client.stop() ;
      return ;
    }
    if (s.startsWith("Content-Type:"))
    {
      char *p = strstr(s.c_str(), " ") ;
      if (strcmp(p+1, OTA_CONTENT_TYPE) != 0)           // sanity check
      {
        snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
                 "Invalid type - %s\r\n", s.c_str()) ;
        G_runtime->worker[idx].result_code = 500 ;
        client.stop() ;
        return ;
      }
    }
    if (s.startsWith("Content-Length:"))                // sanity check
    {
      char *p = strstr(s.c_str(), " ") ;
      content_length = atoi(p + 1) ;
      if (content_length < 1)
      {
        snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
                 "Invalid length - %s\r\n", s.c_str()) ;
        G_runtime->worker[idx].result_code = 500 ;
        client.stop() ;
        return ;
      }
    }
  }

  // at this point, decide if we're proceeding with the OTA

  if (content_length > OTA_MIN_CONTENT_LENGTH)
    f_ota_download(idx, client, content_length) ;
  else
  {
    strncpy(G_runtime->worker[idx].result_msg, "Invalid content-length.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 500 ;
  }
  client.stop() ;
}


/*
   This function is called from "f_action()". Our job is to parse and check
   the URL supplied by the user. If this passes some basic checks, we'll call
   "f_ota_http_get()" to proceed with the task.
*/

void f_ota_cmd(int idx)
{
  // parse the URL into parts, in particular protocol/host/uri.

  char *tokens[2], *cmd=NULL, *url=NULL ;
  if (f_parse(G_runtime->worker[idx].cmd, tokens, 2) != 2)
  {
    strncpy(G_runtime->worker[idx].result_msg, "Invalid usage.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }
  url = tokens[1] ;

  if ((url == NULL) || (strlen(url) < strlen(OTA_PROTO)+1) ||
      (strncmp(OTA_PROTO, url, strlen(OTA_PROTO)) != 0))
  {
    strncpy(G_runtime->worker[idx].result_msg, "Invalid protocol.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }

  char *host = url + strlen(OTA_PROTO) ;
  char *uri = strstr (host, "/") ;
  if (uri == NULL)
    uri = "" ;                          // url does not include a path
  else
  {
    int offset = uri - host ;
    host[offset] = 0 ;                  // separate "host" from "uri"
    uri++ ;
  }
  if (G_runtime->config.debug)
    Serial.printf("DEBUG: f_ota_cmd() host(%s) uri(%s)\r\n", host, uri) ;

  f_ota_http_get(idx, host, uri) ;
}
