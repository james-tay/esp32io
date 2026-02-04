#include <lwip/netdb.h>

// This structure tracks a single (connected) HTTP client

struct web_client
{
  int sd ;                              // "-1" when not connected
  unsigned long ts_last_activity ;      // millis() of last activity
  int worker ;                          // worker thread ID, -1 if unassigned
  int buf_pos ;                         // current insertion point
  char buf[BUF_LEN_WEBCLIENT] ;         // buffer for webclient http header
  unsigned long ts_start ;              // time we hand off to worker thread
  unsigned long ts_end ;                // time we're notified of our result
} ; typedef struct web_client S_WebClient ;

// This structure is used by a single worker thread.

struct worker_data
{
  int id ;                              // worker thread ID
  int state ;                           // W_IDLE, W_SETUP, W_BUSY or W_DONE
  char name[BUF_LEN_WORKER_NAME] ;      // name in xTaskCreatePinnedToCore()
  TaskHandle_t w_handle ;               // from xTaskCreatePinnedToCore() call
  int caller ;                          // -1=serial, otherwise webclient "idx"
  char *cmd ;                           // command we're assigned to work on
  int result_code ;                     // we generally use HTTP status codes
  char result_msg[BUF_LEN_WORKER_RESULT] ;

  // performance metrics

  unsigned long cmds_executed ;         // total number of commands executed
  unsigned long total_busy_ms ;         // total number of millisecs busy
  unsigned long ts_start ;              // millis() of when we started work
  unsigned long ts_last_cmd ;           // millis() of when we completed work

} ; typedef struct worker_data S_WorkerData ;

// This structure holds all user configuration

struct config_data {

  // wifi configuration

  char wifi_ssid[BUF_LEN_WIFI_SSID] ;
  char wifi_pw[BUF_LEN_WIFI_PW] ;

  // misc settings

  int debug ;                           // 0=none, 1=info

} ; typedef struct config_data S_ConfigData ;

// This structure holds all global and shared runtime info, including config.

struct runtime_data {

  // our user configuration

  S_ConfigData config ;

  // serial console data structures

  int serial_buf_pos ;
  char serial_buf[BUF_LEN_CONSOLE] ;
  TaskHandle_t sconsole_handle ;        // from xTaskCreatePinnedToCore() call

  // web server data structures

  S_WebClient webclients[DEF_WEBSERVER_MAX_CLIENTS] ;
  int notify_sd ;
  char url_path[BUF_LEN_WEB_URL] ;
  char url_params[BUF_LEN_WEB_URL] ;
  char metrics_buf[BUF_LEN_METRICS] ;

  // worker threads

  S_WorkerData worker[DEF_WORKER_THREADS] ;
  int next_worker ;

  // acquire these locks before interacting with the specified resources

  SemaphoreHandle_t L_worker ;          // "next_worker"

  // various performance metrics

  unsigned long serial_in_bytes ;               // total bytes read
  unsigned long serial_commands ;               // total commands issued
  unsigned long serial_overruns ;               // serial buffer overrun
  unsigned long serial_ts_last_read ;           // timestamp of last read()
  unsigned long web_accepts ;                   // clients assigned to a slot
  unsigned long web_ts_last_accept ;            // time of last accept()
  unsigned long web_busy_rejects ;              // no available slots
  unsigned long web_requests_overrun ;          // web client buffer overrun
  unsigned long web_requests_received ;         // successfully parsed requests
  unsigned long web_invalid_requests ;          // can't parse HTTP request
  unsigned long web_idle_timeouts ;             // HTTP client idled too long

} ; typedef struct runtime_data S_RuntimeData ;

