/*
   OVERVIEW

   This program manages the operations on an ESP32. Commands can be sent to
   this device via serial console or via a web server over wifi. All commands
   are sent to a queue, where a pool of worker threads evaluate them. Once each
   task is completed, its response is delivered back to the user.

   INTERNAL THREADS

   The serial console is implemented by a single thread which reads commands
   from the user. Once a command is complete, it is put into the queue and
   the thread blocks with "ulTaskNotifyTake()". Once a worker thread has
   dequeued and completed the task, it calls "xTaskNotifyGive()" to unblock
   the serial console thread.

   The web server runs in a single thread, typically listening for new TCP
   connections or receiving bytes on connected TCP sockets. Thus it uses
   "select()" to block until there is activity. Once a user command has been
   identified, it is placed in the queue. When this task has been completed by
   some worker thread, the worker thread needs to notify the web server thread
   of the completed work. To accomplish this, the web server thread creates a
   UDP server socket which binds to loopback. This socket descriptor is also
   monitored in the webserver thread's "select()". The worker thread then sends
   a UDP packet to signal the web server thread that a result is ready.

   INTERNAL DATA STRUCTURES

   One thing we want to avoid is calling "malloc()" many times, resulting in
   memory fragmentation. Instead, if we pack all known fixed length buffers
   into a single struct, then we can call 1x "malloc()" at boot and use a
   single contiguous block of memory. This is the motivation of "G_runtime".
*/

#define DEF_SERIAL_BAUD	115200          // serial port (over USB)
#define DEF_RGBLED_PIN 48               // RGB led on ESP32-S3 dev board
#define DEF_RGBLED_BLINK_MS 10          // how long LED stays on
#define DEF_THREAD_STACKSIZE 8192       // stack size when thread is created
#define DEF_CONSOLE_THREAD_PRIORITY 1   // thread scheduling priority
#define DEF_WEBSERVER_THREAD_PRIORITY 2 // thread scheduling priority
#define DEF_WEBSERVER_EVENT_PORT 65501  // UDP mesg indicating task completion
#define DEF_WEBSERVER_MAX_CLIENTS 4     // maximum concurrent HTTP clients
#define DEF_WORKER_THREADS 4            // threads which execute commands

#define BUF_LEN_CONSOLE 256             // user command buffer on serial
#define BUF_LEN_WEBCLIENT 256           // buffer for webclient HTTP header
#define BUF_LEN_METRICS 1024            // buffer for "/metrics" response

#include <WiFi.h>

#include "esp32io.h"

// Global Variables

S_RuntimeData *G_runtime=NULL ;

// ============================================================================

void f_serial_console_thread(void *param)
{
  delay(1000) ; // wait for setup() to complete
  while (1)
  {
    // sit here and loop until we read a full command

    G_runtime->serial_buf_pos = 0 ;
    G_runtime->serial_buf[0] = 0 ;
    Serial.printf("> ") ;

    while (1)
    {
      char c = 0 ;                      // in case readBytes() times out
      Serial.readBytes(&c, 1) ;         // block until a char arrives
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

    // now handle the command

    if (G_runtime->serial_buf_pos > 0)
    {
      G_runtime->serial_commands++ ;
      Serial.printf("Handling command '%s'(%d)\r\n",
                    G_runtime->serial_buf, G_runtime->serial_buf_pos) ;
    }
  }
}

// ============================================================================

void setup ()
{
  // initalize our runtime data structures

  G_runtime = (S_RuntimeData*) malloc(sizeof(S_RuntimeData)) ;
  memset(G_runtime, 0, sizeof(S_RuntimeData)) ;

  // print out some info to show that we're booting up

  delay (1000) ;
  WiFi.mode(WIFI_STA) ;
  Serial.begin(DEF_SERIAL_BAUD) ;
  Serial.setTimeout(1000) ;
  Serial.printf("\r\nBOOT: Running esp32io git commit %s, built %s.\r\n",
                BUILD_COMMIT, BUILD_TIME) ;
  Serial.printf("BOOT: G_runtime is %d bytes.\r\n", sizeof(S_RuntimeData)) ;
  Serial.printf("BOOT: Wifi mac: %s\r\n", WiFi.macAddress().c_str()) ;
  Serial.printf("BOOT: Chip temperature: %.2fC\r\n", temperatureRead()) ;

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
    NULL,                               // param to pass
    DEF_CONSOLE_THREAD_PRIORITY,        // priority (higher is more important)
    NULL,                               // task handle
    0) ;                                // core ID

  // start the "f_webserver_thread"

  xTaskCreatePinnedToCore (
    f_webserver_thread,                 // function to run
    "thr_webserver",                    // name which shows up in crash dumps
    DEF_THREAD_STACKSIZE,               // stack size
    NULL,                               // param to pass
    DEF_WEBSERVER_THREAD_PRIORITY,      // priority (higher is more important)
    NULL,                               // task handle
    0) ;                                // core ID

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
  delay (10000) ;

  // blink the LED to indicate we're alive.

  neopixelWrite(DEF_RGBLED_PIN, 0, 0, 255) ;
  delay (DEF_RGBLED_BLINK_MS) ;
  neopixelWrite(DEF_RGBLED_PIN, 0, 0, 0) ;
}

