/*
   OVERVIEW

   This program manages the operations on an ESP32. Commands can be sent to
   this device via serial console or via a web server over wifi. All commands
   are then assigned to one of several worker threads. Once each command is
   completed, its response is delivered back to the user. The serial console
   baud rate is set to DEF_SERIAL_BAUD. Thus the main framework of this program
   is to manage,
     - listen for commands on either serial port or the webserver
     - handle up to DEF_WEBSERVER_MAX_CLIENTS concurrent HTTP clients
     - execute commands across DEF_WORKER_THREADS worker threads

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
   core:0. We'll try to reserve core:1 for user threads which might be time
   or performance critical.

   The following diagram is a summary of how commands, and their results are
   handed off between threads.

                [console]
                ========
                f_serial_command()
                - wake worker with xTaskNotifyGive()
                                         |
                                         |
     [webserver]                         |     [worker]
     ===========                         |     ========
     f_handle_webclient()                |     f_worker_thread()
         |                               |       - block at ulTaskNotifyTake()
         |--> f_handle_camera()          |           |
         |    - identify worker          `---------->|
         |    - xTaskNotifyGive()                    |
         |         |                                 |
         |         `-------------------------------->|
         v                                           |
     f_handle_webrequest()                           |
       - identify worker                             |
       - xTaskNotifyGive()                           |
           |                                         |
           `---------------------------------------->|
                                                     |
                                                     v
                                               f_action()
                                                 - does various tasks
                [console]                            |       |
                =========                            |       |
                f_serial_command()                   |       |
                - block on ulTaskNotifyTake() <------o       v
                                                     |  f_process_camera()
                                                     |       |
                                                     |       |
                                                     v       v
                                               (notify result is ready)
                                                 - serial: xTaskNotifyGive()
                                                 - webclient: UDP pkt
     [webserver]                                     |
     ===========                                     |
     (activity on "event_sd")  <---------------------'
     f_handle_result()
       - send http response (optional)
       - f_close_webclient()
       - mark worker as idle

   WEBSERVER <-> WORKER - HAND OFF

   1. Handing off to a worker thread - The following occurs in the calling
      thread (ie, webserver or console threads) leading up to the hand off.
       a) call "f_get_next_worker()" to identify next worker thread
       b) set worker's "cmd" to point to the task it is to execute
       c) set worker's "caller" to identify the webclient or console
       d) call "xTaskNotifyGive()" on the worker's "w_handle"

   2. Hand off from a worker back to its caller - Since different types of
      callers may invoke a worker, the following situations may occur,
       a) worker signals the serial console thread via "ulTaskNotifyTake()"
       b) worker signals webserver thread by sending a UDP packet
          - usually the webserver thread sends the command's response to the
            webclient before calling "f_close_webclient()".
       c) if worker is handling a "/cam" endpoint, the worker thread will,
          - send the response to the web client
          - worker thread's "result_code" is left at "0", before signalling
            the webserver thread with a UDP packet. The "result_code" of "0"
            tells the webserver thread that the web client response has already
            been taken care of.

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

   CONFIGURATION

   User configuration is consolidated in the S_ConfigData structure. During
   initial boot up, various configuration files will be searched. If they
   exist, then their value will be loaded into the runtime configuration. The
   naming format for each of these files is
     /<name>.cfg

   For example, if the following files exist,
     /wifi_ssid.cfg
     /wifi_pw.cfg

   Then they get loaded into,
     G_runtime->config.wifi_ssid
     G_runtime->config.wifi_pw

   The MQTT subsystem can be optionally configured by setting "mqtt_setup"
   and "mqtt_topic". The MQTT subsystem remains in an uninitialized state
   until the "mqtt connect" command is issued.

   COMMAND EXECUTION

   In general, commands are entered from the serial console, or via the web
   server. In either scenario, a worker thread is identified and

     1. its "cmd" is pointed to the buffer containing the command.
     2. its "caller" is set to the webclient index, or "-1" if serial console.

   The worker thread is awoken via "xTaskNotifyGive()". At this point the
   worker thread will,

     1. reset its "result_msg" and "result_code"
     2. calls "f_action()" which evaluates the "cmd". At the end of this stage,
        the worker thread's "result_msg" and "result_code" will be set to
        indicate outcome.
     3. notify the caller that its command results are ready,
        a) for the serial console, the worker calls "xTaskNotifyGive()" to
           unblock the serial console thread.
        b) for a web client, the worker sends a UDP packet which indicates
           the web client index the result is meant for.

   Expanding on this mechanism, consider the scenario where a user task thread
   intends to ride on the commands supported by "f_action()". Along the same
   lines, consider a series of commands which a user task thread wants to
   execute. In such scenarios, the user task thread hands off the "cmd" to
   a worker thread, similar to how it's handled by the serial console thread.
   However, the user task thread sets the worker thread's "caller" to a value
   "DEF_UTHREAD_CALLER_OFFSET + <user_task_thread_slot>". In this way, a
   worker thread can identify the user task thread, and thus signal work
   completion appropriately.

   When this program boots up, we want user defined actions performed
   automatically after a certain boot up delay (eg, after 60 secs). These
   actions could configure a camera or set certain pin states, etc. This
   is done by the main "loop()" once our uptime crosses this boot up period.
   Essentially a worker thread is assigned the "task start init" command. In
   this context, the worker thread's "caller" is set to -255 (ie, anonymous).
   Thus, the user can provide the "/init.thread" file, which will be started
   automatically after boot.
*/

// my custom header

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
   This function is called when data is available on the serial port for us.
   The callback was setup in "f_serial_console_thread()".
*/

void f_on_serial_recv()
{
  static BaseType_t priority = pdFALSE ;
  xSemaphoreGiveFromISR(G_runtime->L_serial_in, &priority) ;
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

  // set a callback unlock "L_serial_in" when data arrives on the serial port
  Serial.onReceive(f_on_serial_recv) ;

  // our main loop ... try reading commands from the serial port.

  while (1)
  {
    G_runtime->serial_buf_pos = 0 ;
    G_runtime->serial_buf[0] = 0 ;
    Serial.printf("> ") ;

    // sit here and loop until we read a full command

    while (1)
    {
      // if there's no data to read, block until "L_serial_in" is released

      if (Serial.available() == 0)
        xSemaphoreTake(G_runtime->L_serial_in, portMAX_DELAY) ;
      G_runtime->serial_ts_last_loop = esp_timer_get_time() ;

      // if we got here, there's something to read on the serial port

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

void setup()
{
  delay (1000) ;

  // initalize our runtime data structures

  G_runtime = (S_RuntimeData*) malloc(sizeof(S_RuntimeData)) ;
  memset(G_runtime, 0, sizeof(S_RuntimeData)) ;
  G_runtime->L_worker = xSemaphoreCreateMutex() ;
  G_runtime->L_uthread = xSemaphoreCreateMutex() ;
  G_runtime->L_uart = xSemaphoreCreateMutex() ;
  G_runtime->L_pubsub = xSemaphoreCreateMutex() ;
  G_runtime->L_serial_in = xSemaphoreCreateBinary() ;
  G_runtime->config.wifi_check_secs = DEF_WIFI_CHK_INT_SECS ;
  G_runtime->config.init_delay_secs = DEF_INIT_THREAD_START_SECS ;
  G_runtime->config.uart_poll_ms = DEF_UART_POLL_MS ;

  // print out some info to show that we're booting up

  WiFi.mode(WIFI_STA) ;
  Serial.begin(DEF_SERIAL_BAUD) ;
  Serial.setTimeout(1000) ;
  Serial.printf("\r\nBOOT: Running esp32io git commit %s, built %s.\r\n",
                BUILD_COMMIT, BUILD_TIME) ;

  if (SPIFFS.begin())
    G_runtime->fs_online = 1 ;
  Serial.printf("BOOT: G_runtime is %d bytes.\r\n", sizeof(S_RuntimeData)) ;
  Serial.printf("BOOT: Wifi mac: %s\r\n", WiFi.macAddress().c_str()) ;
  Serial.printf("BOOT: Chip temperature: %.2fC\r\n", temperatureRead()) ;
  Serial.printf("BOOT: SPIFFS mounted: %d\r\n", G_runtime->fs_online) ;
  if (G_runtime->fs_online)
    f_load_config() ;

  // start the "f_serial_console_thread"

  xTaskCreatePinnedToCore (
    f_serial_console_thread,            // function to run
    "thr_console",                      // name which shows up in crash dumps
    DEF_STACKSIZE_CONSOLE,              // stack size
    NULL,                               // param to pass into thread
    DEF_CONSOLE_THREAD_PRIORITY,        // priority (higher is more important)
    &G_runtime->sconsole_handle,        // task handle
    0) ;                                // core ID

  // start the "f_webserver_thread"

  xTaskCreatePinnedToCore (
    f_webserver_thread,                 // function to run
    "thr_webserver",                    // name which shows up in crash dumps
    DEF_STACKSIZE_WEBSERVER,            // stack size
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
      DEF_STACKSIZE_WORKER,             // stack size
      &G_runtime->worker[i].id,         // param to pass into thread
      DEF_WORKER_PRIORITY,              // priority (higher is more important)
      &G_runtime->worker[i].w_handle,   // task handle
      0) ;                              // core ID
  }

  // our first choice is user configured wifi, if absent use compile time
  // credentials if present

  char *wifi_ssid = G_runtime->config.wifi_ssid ;
  char *wifi_pw = G_runtime->config.wifi_pw ;

  #if defined(WIFI_SSID) && defined(WIFI_PW)
    if ((wifi_ssid[0] == 0) || (wifi_pw[0] == 0))
      { wifi_ssid = WIFI_SSID ; wifi_pw = WIFI_PW ; }
  #endif

  if ((strlen(wifi_ssid) > 0) && (strlen(wifi_pw) > 0))
  {
    Serial.printf("BOOT: Connecting to '%s'.\r\n", wifi_ssid) ;
    f_wifi_connect(wifi_ssid, wifi_pw) ;
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

void loop()
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

  // if it's time to run "/init.thread", do it now

  if ((G_runtime->config.init_delay_secs != 0) &&
      (now / 1000000 > G_runtime->config.init_delay_secs))
  {
    if (G_runtime->config.debug)
      Serial.printf("DEBUG: main thread is running '/init.thread'.\r\n") ;

    // "cmd" must point to a writable buffer because of "f_parse()"
    strncpy(G_runtime->cmd_buf, "task start init", BUF_LEN_LINE) ;

    int tid = f_get_next_worker() ;
    G_runtime->worker[tid].caller = DEF_ANON_CALLER ;
    G_runtime->worker[tid].cmd = G_runtime->cmd_buf ;
    xTaskNotifyGive(G_runtime->worker[tid].w_handle) ;
    G_runtime->config.init_delay_secs = 0 ; // this disables another run
  }

  // periodically check if our wifi is not connected, and reconnect if needed

  if (now > G_runtime->ts_last_wifi_check +
            (G_runtime->config.wifi_check_secs * 1000000))
  {
    if ((WiFi.status() != WL_CONNECTED) &&
        (strlen(G_runtime->config.wifi_ssid) > 0) &&
        (strlen(G_runtime->config.wifi_pw) > 0))
      f_wifi_connect(G_runtime->config.wifi_ssid, G_runtime->config.wifi_pw) ;
    G_runtime->ts_last_wifi_check = now ;
  }

  // if user requested a reload, set LED to red until we die

  if (G_runtime->request_reload)
  {
    neopixelWrite(DEF_RGBLED_PIN, 255, 0, 0) ;
    delay(1000) ;
    ESP.restart() ;
  }

  // periodically check if user task threads exited (voluntarily)

  if (xSemaphoreTake(G_runtime->L_uthread, 0) == pdTRUE)
  {
    for (int slot=0 ; slot < DEF_MAX_USER_THREADS ; slot++)
      if (G_runtime->utask[slot].state == UTHREAD_STOPPED)
      {
        vTaskDelete(G_runtime->utask[slot].tid) ;
        G_runtime->utask[slot].state = UTHREAD_IDLE ;
        if (G_runtime->config.debug)
          Serial.printf("DEBUG: loop() reaped thread '%s' at loop %lld.\r\n",
                        G_runtime->utask[slot].name,
                        G_runtime->utask[slot].loop) ;
      }
    xSemaphoreGive(G_runtime->L_uthread) ;
  }

  delay (1000) ;
}

