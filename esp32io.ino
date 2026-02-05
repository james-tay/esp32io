/*
   OVERVIEW

   This program manages the operations on an ESP32. Commands can be sent to
   this device via serial console or via a web server over wifi. All commands
   are sent to a queue, where a pool of worker threads evaluate them. Once each
   task is completed, its response is delivered back to the user.

   INTERNAL THREADS

   The serial console is implemented by a single thread which reads bytes from
   the user. Once a complete command is received, it is assigned to a worker
   thread and the serial console thread blocks with "ulTaskNotifyTake()". Once
   a worker thread has completed this task, it calls "xTaskNotifyGive()" to
   unblock the serial console thread.

   The web server runs in another thread, typically listening for new TCP
   connections or receiving bytes on connected TCP sockets. Thus it uses
   "select()" to block until there is activity. Once a user command has been
   identified, it is similarly handed to a worker thread. When this task has
   been completed by the worker thread, the worker thread needs to notify the
   web server thread of the completed work. To accomplish this, the web server
   thread creates a UDP server socket which binds to loopback. This socket
   descriptor is also monitored in the webserver thread's "select()". The
   worker thread then sends a UDP packet to signal the web server thread that
   a result is ready. This UDP packet's payload consists of a single byte,
   which is the calling webclient "idx".

   In general, all internal threads (eg, webserver, workers, etc) will run on
   core-0. We'll try to reserve core-1 for user threads which might be time
   or performance critical.

   INTERNAL DATA STRUCTURES

   One thing we want to avoid is calling "malloc()" many times, resulting in
   memory fragmentation. Instead, if we pack all known fixed length buffers
   into a single struct, then we can call exactly 1 "malloc()" at boot and use
   a single contiguous block of memory. This is the motivation of "G_runtime".

   When a command is received on the serial console or webclient, the serial
   console thread or webserver thread identifies an available worker thread.
   The worker thread's "cmd" points to the buffer storing this command and the
   worker is woken up with a call to "xTaskNotifyGive()". Once the worker has
   done its work, it writes to "result_code" and "result_msg" before notifying
   the serial console thread or webserver thread that it is done. Thus, the
   worker thread has the following states,
     W_IDLE  - blocked, can be selected for work
     W_SETUP - selected by "f_get_next_worker()", worker thread is still idle
     W_BUSY  - thread woken up by caller and is working on the task
     W_DONE  - thread results prepared, the caller retrieves the results and
               MUST release the worker by setting its state back to W_IDLE

   Whenever we try to identify a W_IDLE worker thread, we'd typically use
   "G_runtime->next_worker". If that worker is not W_IDLE, then we try the
   next one and so on. Each time we try another worker thread, we delay our
   attempt until we reach DEF_WORKER_FIND_MAX_MS.
*/

#define DEF_SERIAL_BAUD	115200          // serial port (over USB)
#define DEF_RGBLED_PIN 48               // RGB led on ESP32-S3 dev board
#define DEF_RGBLED_BLINK_MS 5           // how long LED stays on
#define DEF_RGBLED_BLINK_INT_SEC 5      // how ofter to blink LED
#define DEF_THREAD_STACKSIZE 8192       // stack size when thread is created
#define DEF_WEBSERVER_EVENT_PORT 65501  // UDP mesg indicating task completion
#define DEF_WEBSERVER_MAX_CLIENTS 4     // maximum concurrent HTTP clients
#define DEF_WEBSERVER_MAX_IDLE_MS 8000  // disconnect idle http clients
#define DEF_WORKER_THREADS 4            // threads which execute commands
#define DEF_WORKER_FIND_MAX_MS 500      // max delay between finding workers
#define DEF_WIFI_BEGIN_WAIT_SECS 30     // how long to wait after WiFi.begin()

// thread scheduling priorities

#define DEF_WORKER_PRIORITY 1           // thread scheduling priority
#define DEF_CONSOLE_THREAD_PRIORITY 2   // thread scheduling priority
#define DEF_WEBSERVER_THREAD_PRIORITY 3 // thread scheduling priority

// various buffer sizes

#define BUF_LEN_CONSOLE 256             // user command buffer on serial
#define BUF_LEN_WEBCLIENT 256           // buffer for webclient HTTP header
#define BUF_LEN_WEB_URL 256             // maximum allowed URL length
#define BUF_LEN_METRICS 2048            // buffer for "/metrics" response
#define BUF_LEN_WORKER_NAME 12          // how long worker thread name is
#define BUF_LEN_WORKER_RESULT 2048      // worker thread's "result_msg" buffer
#define BUF_LEN_WIFI_SSID 32            // maximum wifi SSID allowed length
#define BUF_LEN_WIFI_PW 64              // maximum wifi password allowed
#define BUF_LEN_LINE 128                // generic metrics, http response, etc

// worker thread states

#define W_IDLE  0                       // blocked, can be assigned work
#define W_SETUP 1                       // selected for work, but still idle
#define W_BUSY  2                       // thread is awake and running
#define W_DONE  3                       // caller reads results and sets W_IDLE

#include <WiFi.h>
#include <SPIFFS.h>

#include "esp32io.h"

// Global Variables

S_RuntimeData *G_runtime=NULL ;

// ============================================================================

/*
   This function is called whenever we need to find an available worker thread.
   We'll search using "G_runtime->next_worker", until we find a worker thread
   in the W_IDLE state. Each time we search for the next worker, we'll delay
   the search by "find_delay_ms", but not longer than DEF_WORKER_FIND_MAX_MS.
   Once an available worker is identified, we immediately set it to W_SETUP so
   that it's clear that the worker has been assigned. The available worker
   thread ID is then returned.
*/

int f_get_next_worker()
{
  int find_delay_ms = 1 ;
  while (1)
  {
    // we may be called from serial console thread or the webserver thread

    xSemaphoreTake(G_runtime->L_worker, portMAX_DELAY) ;
    int tid = G_runtime->next_worker ;
    G_runtime->next_worker++ ;
    if (G_runtime->next_worker == DEF_WORKER_THREADS)
      G_runtime->next_worker = 0 ;

    // see if this worker is available, otherwise try again after a delay

    if (G_runtime->worker[tid].state == W_IDLE)
    {
      G_runtime->worker[tid].state = W_SETUP ;  // mark worker as taken
      xSemaphoreGive(G_runtime->L_worker) ;
      return(tid) ;
    }
    xSemaphoreGive(G_runtime->L_worker) ;

    delay(find_delay_ms) ;
    find_delay_ms++ ;
    if (find_delay_ms > DEF_WORKER_FIND_MAX_MS)
      find_delay_ms = DEF_WORKER_FIND_MAX_MS ;
  }
}

/*
   This function is called from "f_serial_console_thread()". Our job is to
   hand off "G_runtime->serial_buf" to a worker thread, wait for the task
   completion, and send the response back to the user (ie, serial port).
*/

void f_serial_command()
{
  long long ts_start = esp_timer_get_time() ;
  int tid = f_get_next_worker() ;
  G_runtime->worker[tid].caller = -1 ;                  // identify as serial
  G_runtime->worker[tid].cmd = G_runtime->serial_buf ;  // current user command
  xTaskNotifyGive(G_runtime->worker[tid].w_handle) ;    // unblock worker

  // wait for worker thread to tell us it's done

  ulTaskNotifyTake(pdTRUE, portMAX_DELAY) ;

  // if we got here, that means the worker thread woke us up

  long long ts_end = esp_timer_get_time() ;
  Serial.printf("%s[code:%d time:%dms]\r\n",
                G_runtime->worker[tid].result_msg,
                G_runtime->worker[tid].result_code,
                (ts_end - ts_start) / 1000) ;

  G_runtime->worker[tid].cmd = NULL ;
  G_runtime->worker[tid].state = W_IDLE ;               // release worker
}

/*
   This function forms the thread life cycle of the serial console thread. We
   concern ourselves with interacting with bytes on the serial port. When a
   complete command is accumulated in "G_runtime->serial_buf", we hand off the
   work to "f_serial_command()".
*/

void f_serial_console_thread(void *param)
{
  delay(1000) ; // wait for setup() to complete
  while (1)
  {
    G_runtime->serial_buf_pos = 0 ;
    G_runtime->serial_buf[0] = 0 ;
    Serial.printf("> ") ;

    // sit here and loop until we read a full command

    while (1)
    {
      char c = 0 ;                      // in case readBytes() times out
      Serial.readBytes(&c, 1) ;         // wait for char, or serial timeout
      if (c != 0)
        G_runtime->serial_ts_last_read = esp_timer_get_time() ;

      if ((c == 8) || (c == 127))       // handle BS or DEL
      {
        if (G_runtime->serial_buf_pos > 0) // do we need to erase a char ?
        {
          Serial.printf("\b \b") ;
          G_runtime->serial_buf_pos-- ;
          G_runtime->serial_buf[G_runtime->serial_buf_pos] = 0 ;
        }
      }
      else
      if ((c == '\n') || (c == '\r') ||
          (G_runtime->serial_buf_pos == BUF_LEN_CONSOLE - 1))
      {
        Serial.printf("\r\n") ;
        if (G_runtime->serial_buf_pos == BUF_LEN_CONSOLE - 1)
        {
          Serial.printf("WARNING: Command exceeds %d bytes, ignoring.\r\n",
                        BUF_LEN_CONSOLE - 1) ;
          G_runtime->serial_buf[0] = 0 ;
          G_runtime->serial_buf_pos = 0 ;
          G_runtime->serial_overruns++ ;
        }
        break ;
      }
      else
      if (c > 0)                                // a normal char
      {
        Serial.printf("%c", c) ;
        G_runtime->serial_buf[G_runtime->serial_buf_pos] = c ;
        G_runtime->serial_buf[G_runtime->serial_buf_pos+1] = 0 ;
        G_runtime->serial_buf_pos++ ;
        G_runtime->serial_in_bytes++ ;
      }
    }

    // if we're here, that means we've received a full command

    if (G_runtime->serial_buf_pos > 0)
    {
      G_runtime->serial_commands++ ;
      f_serial_command() ;
    }
  }
}

// ============================================================================

void setup ()
{
  // initalize our runtime data structures

  G_runtime = (S_RuntimeData*) malloc(sizeof(S_RuntimeData)) ;
  memset(G_runtime, 0, sizeof(S_RuntimeData)) ;
  G_runtime->L_worker = xSemaphoreCreateMutex() ;

  // print out some info to show that we're booting up

  delay (1000) ;
  if (SPIFFS.begin())
    G_runtime->fs_online = 1 ;
  WiFi.mode(WIFI_STA) ;
  Serial.begin(DEF_SERIAL_BAUD) ;
  Serial.setTimeout(1000) ;
  Serial.printf("\r\nBOOT: Running esp32io git commit %s, built %s.\r\n",
                BUILD_COMMIT, BUILD_TIME) ;
  Serial.printf("BOOT: G_runtime is %d bytes.\r\n", sizeof(S_RuntimeData)) ;
  Serial.printf("BOOT: Wifi mac: %s\r\n", WiFi.macAddress().c_str()) ;
  Serial.printf("BOOT: Chip temperature: %.2fC\r\n", temperatureRead()) ;
  Serial.printf("BOOT: SPIFFS mounted: %d\r\n", G_runtime->fs_online) ;

  // if we have compile time wifi config, set it up now.

  #if defined(WIFI_SSID) && defined(WIFI_PW)
    Serial.printf("BOOT: Connecting to %s.", WIFI_SSID) ;
    WiFi.begin(WIFI_SSID, WIFI_PW) ;
    for (int retry=0 ; retry < 30 ; retry++)
    {
      if (WiFi.status() == WL_CONNECTED)
        break ;
      Serial.printf(".") ;
      delay(1000) ;
    }
    Serial.printf("\r\nBOOT: IP: %d.%d.%d.%d\r\n",
                  WiFi.localIP()[0], WiFi.localIP()[1],
                  WiFi.localIP()[2], WiFi.localIP()[3]) ;
  #endif

  // start the "f_serial_console_thread"

  xTaskCreatePinnedToCore (
    f_serial_console_thread,            // function to run
    "thr_console",                      // name which shows up in crash dumps
    DEF_THREAD_STACKSIZE,               // stack size
    NULL,                               // param to pass into thread
    DEF_CONSOLE_THREAD_PRIORITY,        // priority (higher is more important)
    &G_runtime->sconsole_handle,        // task handle
    0) ;                                // core ID

  // start the "f_webserver_thread"

  xTaskCreatePinnedToCore (
    f_webserver_thread,                 // function to run
    "thr_webserver",                    // name which shows up in crash dumps
    DEF_THREAD_STACKSIZE,               // stack size
    NULL,                               // param to pass into thread
    DEF_WEBSERVER_THREAD_PRIORITY,      // priority (higher is more important)
    NULL,                               // task handle
    0) ;                                // core ID

  // create worker threads

  for (int i=0 ; i < DEF_WORKER_THREADS ; i++)
  {
    G_runtime->worker[i].id = i ;
    snprintf(G_runtime->worker[i].name, BUF_LEN_WORKER_NAME, "worker%d", i) ;
    xTaskCreatePinnedToCore (
      f_worker_thread,                  // function to run
      G_runtime->worker[i].name,        // name which shows up in crash dumps
      DEF_THREAD_STACKSIZE,             // stack size
      &G_runtime->worker[i].id,         // param to pass into thread
      DEF_WORKER_PRIORITY,              // priority (higher is more important)
      &G_runtime->worker[i].w_handle,   // task handle
      0) ;                              // core ID
  }

  // blink RGB LED to indicate we've completed initialization

  neopixelWrite(DEF_RGBLED_PIN, 255, 0, 0) ;
  delay (333) ;
  neopixelWrite(DEF_RGBLED_PIN, 0, 255, 0) ;
  delay (333) ;
  neopixelWrite(DEF_RGBLED_PIN, 0, 0, 255) ;
  delay (333) ;
  neopixelWrite(DEF_RGBLED_PIN, 0, 0, 0) ;
}

void loop ()
{
  long long now = esp_timer_get_time() ;

  if (now > G_runtime->ts_last_blink + (DEF_RGBLED_BLINK_INT_SEC * 1000000))
  {
    // blink the LED to indicate we're alive. The color indicates wifi status.

    if (WiFi.status() == WL_CONNECTED)
      neopixelWrite(DEF_RGBLED_PIN, 0, 0, 255) ;
    else
      neopixelWrite(DEF_RGBLED_PIN, 255, 0, 0) ;
    delay (DEF_RGBLED_BLINK_MS) ;
    neopixelWrite(DEF_RGBLED_PIN, 0, 0, 0) ;

    G_runtime->ts_last_blink += DEF_RGBLED_BLINK_INT_SEC * 1000000 ;
  }

  if (G_runtime->request_reload)
  {
    // user requested a reload. Set LED to red until we die

    neopixelWrite(DEF_RGBLED_PIN, 255, 0, 0) ;
    delay(1000) ;
    ESP.restart() ;
  }

  delay (1000) ;
}

