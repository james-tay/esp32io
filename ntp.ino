/*
   This function is called from "f_action()". Our job is to parse the supplied
   user ntp command and attempt to set the system clock from the specified
   ntp server.
*/

void f_ntp_cmd(int idx)
{
  char *tokens[2], *cmd=NULL, *server=NULL ;
  if (f_parse(G_runtime->worker[idx].cmd, tokens, 2) != 2)
  {
    strncpy(G_runtime->worker[idx].result_msg, "No NTP server specified.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }
  server = tokens[1] ;

  esp_sntp_config_t ntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(server) ;
  esp_netif_sntp_init(&ntp_cfg) ;

  // we're now ready to perform an ntp sync. In order to calculate our time
  // jump, call "esp_timer_get_time()" before/after to calculate how much
  // time elapsed, and also "gettimeofday()" before/after to capture the
  // time jump.

  long long ts_start, ts_end, time_elapsed, time_jumped ;
  struct timeval tv_start, tv_end ;

  ts_start = esp_timer_get_time() ;
  gettimeofday(&tv_start, NULL) ;

  esp_err_t r = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(DEF_NTP_TIMEOUT_MSEC)) ;
  if (r == ESP_OK)
  {
    ts_end = esp_timer_get_time() ;
    gettimeofday(&tv_end, NULL) ;

    time_elapsed = ts_end - ts_start ;
    time_jumped = ((tv_end.tv_sec * 1000000) + tv_end.tv_usec) -
                  ((tv_start.tv_sec * 1000000) + tv_start.tv_usec) ;
    time_jumped = time_jumped - time_elapsed ;

    if (G_runtime->ntp_updates == 0)
      snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
               "NTP initial sync with '%s'.\r\n", server) ;
    else
      snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
               "NTP sync'ed with '%s', jumped %d usec.\r\n",
               server, time_jumped) ;

    G_runtime->worker[idx].result_code = 200 ;
    G_runtime->ntp_updates++ ;
    G_runtime->ts_last_ntp_sync = esp_timer_get_time() ;
  }
  else
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "NTP sync with '%s' failed.\r\n", server) ;
    G_runtime->worker[idx].result_code = 500 ;
  }
  esp_netif_sntp_deinit() ;
}
