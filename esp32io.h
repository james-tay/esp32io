#include <lwip/netdb.h>

// This structure tracks a single (connected) HTTP client

struct web_client
{
  int sd ;                              // "-1" when not connected
  unsigned long ts_last_activity ;      // millis() of last activity
  int buf_pos ;                         // current insertion point
  char buf[BUF_LEN_WEBCLIENT] ;         // buffer for webclient http header
} ; typedef struct web_client S_WebClient ;

struct worker_data
{
  int id ;                              // worker thread ID
  char name[BUF_LEN_WORKER_NAME] ;      // name in xTaskCreatePinnedToCore()
  TaskHandle_t handle ;                 // from xTaskCreatePinnedToCore() call

  // performance metrics



} ; typedef struct worker_data S_WorkerData ;

// This structure holds all global and shared runtime info

struct runtime_data {

  // serial console data structures

  int serial_buf_pos ;
  char serial_buf[BUF_LEN_CONSOLE] ;

  // web server data structures

  S_WebClient webclients[DEF_WEBSERVER_MAX_CLIENTS] ;
  char metrics_buf[BUF_LEN_METRICS] ;

  // worker threads

  S_WorkerData worker[DEF_WORKER_THREADS] ;
  int next_worker ;

  // various performance metrics

  unsigned long serial_in_bytes ;               // total bytes read
  unsigned long serial_commands ;               // total commands issued
  unsigned long serial_overruns ;               // serial buffer overrun
  unsigned long web_accepts ;                   // clients assigned to a slot
  unsigned long web_busy_rejects ;              // no available slots
  unsigned long web_requests_overrun ;          // web client buffer overrun
  unsigned long web_requests_received ;         // successfully parsed requests
  unsigned long web_invalid_requests ;          // can't parse HTTP request

} ; typedef struct runtime_data S_RuntimeData ;

