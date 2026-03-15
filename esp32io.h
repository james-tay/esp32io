// arduino-esp32 - https://github.com/espressif/arduino-esp32

#include <WiFi.h>
#include <SPIFFS.h>
#include <Update.h>

// esp-idf - https://github.com/espressif/esp-idf/tree/master/components

#include "netdb.h"
#include "esp_sntp.h"
#include "esp_flash.h"
#include "esp_camera.h"
#include "esp_chip_info.h"
#include "esp_netif_sntp.h"

// general defines

#define DEF_SERIAL_BAUD 115200          // serial port (over USB)
#define DEF_RGBLED_PIN 48               // RGB led on ESP32-S3 dev board
#define DEF_RGBLED_BLINK_MS 5           // how long LED stays on
#define DEF_RGBLED_BLINK_INT_SEC 5      // how ofter to blink LED
#define DEF_WEBSERVER_EVENT_PORT 65501  // UDP mesg indicating task completion
#define DEF_WEBSERVER_MAX_CLIENTS 4     // maximum concurrent HTTP clients
#define DEF_WEBSERVER_MAX_IDLE_MS 8000  // disconnect idle http clients
#define DEF_WORKER_THREADS 4            // threads which execute commands
#define DEF_WORKER_FIND_MAX_MS 500      // max delay between finding workers
#define DEF_WIFI_BEGIN_WAIT_SECS 30     // how long to wait after WiFi.begin()
#define DEF_WIFI_CHK_INT_SECS 30        // how often to check wifi status
#define DEF_MAX_FILENAME_LEN 30         // maximum filename length on SPIFFS
#define DEF_NTP_TIMEOUT_MSEC 10000      // how long we wait for ntp to sync
#define DEF_INIT_THREAD_START_SECS 60   // autorun "/init.thread" at this time
#define DEF_ANON_CALLER -255            // anonymous worker thread "caller"
#define DEF_UTHREAD_CALLER_OFFSET 1000  // user task thread's "caller" offset

// stack sizes for various threads

#define DEF_STACKSIZE_WORKER 8192       // worker threads
#define DEF_STACKSIZE_WEBSERVER 4096    // webserver thread
#define DEF_STACKSIZE_CONSOLE 3072      // serial console thread
#define DEF_STACKSIZE_UTHREAD 8192      // user task thread

// user thread limits

#define DEF_MAX_USER_THREADS 12         // number of user defined thread tasks
#define DEF_MAX_USER_THREAD_NAME 16     // user defined thread's name
#define DEF_MAX_THREAD_RESULTS 16       // number of metrics exposed
#define DEF_MAX_THREAD_LABELS 8         // labels per metric
#define DEF_MAX_THREAD_CONF 80          // total bytes of all thread args
#define DEF_MAX_THREAD_ARGS 8           // user defined arguments
#define DEF_MAX_THREAD_WRAPUP_MSEC 3000 // how long before killing thread

// user thread result value types

#define UTHREAD_RESULT_NONE 0           // no user thread result saved here
#define UTHREAD_RESULT_INT 1            // an "int" data type
#define UTHREAD_RESULT_FLOAT 2          // a "float" data type
#define UTHREAD_RESULT_LONGLONG 3       // a "long long" data type

// thread scheduling priorities (higher means more important)

#define DEF_WORKER_PRIORITY 1           // worker threads
#define DEF_CONSOLE_THREAD_PRIORITY 2   // serial console thread
#define DEF_WEBSERVER_THREAD_PRIORITY 3 // webserver (includes task dispatch)
#define DEF_USER_THREAD_PRIORITY 4      // user task thread

// various buffer sizes

#define BUF_LEN_CONSOLE 256             // user command buffer on serial
#define BUF_LEN_WEBCLIENT 1024          // buffer for webclient HTTP header
#define BUF_LEN_WEB_URL 256             // maximum allowed URL length
#define BUF_LEN_METRICS 2048            // buffer for "/metrics" response
#define BUF_LEN_WORKER_NAME 12          // how long worker thread name is
#define BUF_LEN_WORKER_RESULT 1024      // worker thread's "result_msg" buffer
#define BUF_LEN_WIFI_SSID 32            // maximum wifi SSID allowed length
#define BUF_LEN_WIFI_PW 64              // maximum wifi password allowed
#define BUF_LEN_LINE 128                // generic metrics, http response, etc
#define BUF_LEN_ERR 96                  // generic error message buffer
#define BUF_LEN_UTHREAD_STATUS 80       // optional user thread status message
#define BUF_LEN_UTASK_FILESIZE 1024     // max size of user task files

// worker thread states

#define W_IDLE  0                       // blocked, can be assigned work
#define W_SETUP 1                       // selected for work, but still idle
#define W_BUSY  2                       // thread is awake and running
#define W_DONE  3                       // caller reads results and sets W_IDLE

// user thread states

#define UTHREAD_IDLE 0                  // ready for work
#define UTHREAD_STARTING 1              // allocated for work, configuring now
#define UTHREAD_RUNNING 2               // user thread's code is running
#define UTHREAD_WRAPUP 3                // cleanup and prepare to terminate
#define UTHREAD_STOPPED 4               // user thread's code is not running

// This structure tracks a single (connected) HTTP client

struct web_client
{
  int sd ;                              // "-1" when not connected
  long long ts_last_activity ;          // timestamp of last activity
  int worker ;                          // worker thread ID, -1 if unassigned
  int buf_pos ;                         // current insertion point
  char buf[BUF_LEN_WEBCLIENT] ;         // buffer for webclient http header
  long long ts_start ;                  // time we hand off to worker thread
  long long ts_end ;                    // time we're notified of our result
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
  long long ts_start ;                  // timestamp of when we started work
  long long ts_last_cmd ;               // timestamp of when we completed work

} ; typedef struct worker_data S_WorkerData ;

// This structure holds camera configuration, it is malloc()'ed when needed

struct camera_data {

  // this is basically an opaque structure used by the camera subsystem

  camera_config_t cam_setup ;           // from "esp_camera.h"

  // runtime performance metrics

  unsigned long xclk_mhz ;              // camera's XCLK frequency
  unsigned long frames_ok ;             // total frames captured successfully
  unsigned long frames_bad ;            // total esp_camera_fb_get() failures
  unsigned long bad_xmits ;             // could not send data to client
  unsigned long last_frame_size ;       // image jpeg bytes
  unsigned long last_capture_usec ;     // frame capture time
  unsigned long last_xmit_msec ;        // time to send image to client

} ; typedef struct camera_data S_CamData ;

// a single result in a user thread, to be exposed as a metric

struct thread_result {
  int num_labels ;                      // number of labels for this metric
  char *l_name[DEF_MAX_THREAD_LABELS] ; // array of pointers to label names
  char *l_data[DEF_MAX_THREAD_LABELS] ; // array of pointers to label values
  char result_type ;                    // whether result is "int" or "float"
  int i_value ;                         // this result's int value
  double f_value ;                      // this result's float value
  long long ll_value ;                  // this result's long long value
} ; typedef struct thread_result S_ThreadResult ;

// This structure holds all data to support a single user task thread

struct user_thread {
  int state ;                           // user thread's current state
  int core ;                            // which CPU core thread runs on
  TaskHandle_t tid ;                    // set by xTaskCreatePinnedToCore()
  char name[DEF_MAX_USER_THREAD_NAME] ; // user defined thread's name
  long long loop ;                      // calls to the thread's function
  long long ts_start ;                  // time when thread came to life

  // thread's user configuration comes here

  int num_args ;                        // actual arguments in "in_args"
  char *in_args[DEF_MAX_THREAD_ARGS] ;  // fixed array to all possible args
  char conf[DEF_MAX_THREAD_CONF] ;      // buffer for all thread arguments
  void (*ft_addr)(struct user_thread*) ; // the "<ft_xxx>()" this thread runs

  // thread's runtime data and results come here

  char status[BUF_LEN_UTHREAD_STATUS] ; // user thread status string buffer
  S_ThreadResult result[DEF_MAX_THREAD_RESULTS] ;       // all metrics exposed

} ; typedef struct user_thread S_UserThread ;

// This structure holds all user configuration

struct config_data {

  // wifi configuration

  char wifi_ssid[BUF_LEN_WIFI_SSID] ;
  char wifi_pw[BUF_LEN_WIFI_PW] ;
  int wifi_check_secs ;

  // misc settings

  int init_delay_secs ;                 // secs before running "/init.thread"
  int debug ;                           // 0=none, 1=info

} ; typedef struct config_data S_ConfigData ;

// This structure holds all global and shared runtime info, including config.

struct runtime_data {

  // our user configuration

  S_ConfigData config ;

  // general runtime status

  int fs_online ;                       // what SPIFFS.begin() returned
  int request_reload ;                  // 0=normal, 1=reload
  char cmd_buf[BUF_LEN_LINE] ;          // used in main "loop()"
  long long ts_last_blink ;             // timestamp of last LED blink
  long long ts_last_wifi_check ;        // timestamp of last wifi status check

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

  // camera configuration (keep this light in case it's not used)

  S_CamData *cam_data ;                 // only malloc()'ed on "cam init ..."

  // worker threads

  S_WorkerData worker[DEF_WORKER_THREADS] ;
  int next_worker ;

  // user defined task threads

  S_UserThread utask[DEF_MAX_USER_THREADS] ;

  // acquire these locks before interacting with the specified resources

  SemaphoreHandle_t L_worker ;          // "next_worker"
  SemaphoreHandle_t L_serial_in ;       // unlocked when serial data arrives
  SemaphoreHandle_t L_uthread ;         // lock at thread setup and termination

  // various performance metrics

  unsigned long ntp_updates ;                   // total successful ntp syncs
  long long ts_last_ntp_sync ;                  // timestamp of last ntp sync
  unsigned long serial_in_bytes ;               // total bytes read
  unsigned long serial_commands ;               // total commands issued
  unsigned long serial_overruns ;               // serial buffer overrun
  long long serial_ts_last_loop ;               // timestamp of last loop run
  long long serial_ts_last_read ;               // timestamp of last read()
  unsigned long web_accepts ;                   // clients assigned to a slot
  long long web_ts_last_accept ;                // time of last accept()
  unsigned long web_busy_rejects ;              // no available slots
  unsigned long web_requests_overrun ;          // web client buffer overrun
  unsigned long web_requests_received ;         // successfully parsed requests
  unsigned long web_invalid_requests ;          // can't parse HTTP request
  unsigned long web_idle_timeouts ;             // HTTP client idled too long
  unsigned long wifi_connects ;                 // calls to f_wifi_connect()

} ; typedef struct runtime_data S_RuntimeData ;

