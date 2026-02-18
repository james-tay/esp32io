/*
   This function is called from "f_action()". Our job is to perform a software
   update by downloading from the supplied URL.
*/

void f_ota_cmd(int idx)
{
  #define OTA_PROTO "http://"

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


}
