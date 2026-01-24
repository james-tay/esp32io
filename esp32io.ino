/*
   Overview

   This program manages the operations on an ESP32. Commands can be sent to
   the device via serial console or via a web server over wifi. All commands
   are sent to a queue.

*/

#define DEF_SERIAL_BAUD	115200          // serial port (over USB)
#define DEF_RGBLED_PIN 48               // RGB led on ESP32-S3 dev board
#define DEF_RGBLED_BLINK_MS 10          // how long LED stays on
#define DEF_THREAD_STACKSIZE 2048       // stack size when thread is created
#define DEF_CONSOLE_THREAD_PRIORITY 1   // thread scheduling priority

#define BUF_LEN_CONSOLE 256             // user command buffer on serial

#include <WiFi.h>

#include "esp32io.h"

// Global Variables

S_RuntimeData G_runtime ;

// ============================================================================

void f_serial_console_thread(void *param)
{
  int buf_pos = 0 ;
  char *buf = NULL, c ;

  buf = (char*) malloc(BUF_LEN_CONSOLE) ;
  while (1)
  {
    // sit here and loop until we read a full command

    buf_pos = 0 ;
    buf[0] = 0 ;
    Serial.printf("> ") ;
    while (1)
    {
      Serial.readBytes(&c, 1) ;                 // Block until a char arrives
      if ((c == 8) || (c == 127))               // handle BS or DEL
      {
        if (buf_pos > 0) // do we need to erase a char ?
        {
          Serial.printf("\b \b") ;
          buf_pos-- ;
          buf[buf_pos] = 0 ;
        }
      }
      else
      if ((c == '\n') || (c == '\r') || (buf_pos == BUF_LEN_CONSOLE - 1))
      {
        Serial.printf("\r\n") ;
        if (buf_pos == BUF_LEN_CONSOLE - 1)
        {
          Serial.printf("WARNING: Command exceeds %d bytes, ignoring.\r\n",
                        BUF_LEN_CONSOLE - 1) ;
          buf[0] = 0 ;
          buf_pos = 0 ;
        }
        break ;
      }
      else
      if (c > 0)                                // a normal char
      {
        Serial.printf("%c", c) ;
        buf[buf_pos] = c ;
        buf[buf_pos+1] = 0 ;
        buf_pos++ ;
      }
    }

    // now handle the command

    if (strlen(buf) > 0)
    {
      Serial.printf("Handling command '%s'(%d)\r\n", buf, strlen(buf)) ;
    }
  }
}

// ============================================================================

void setup ()
{
  // initalize data structures

  memset(&G_runtime, 0, sizeof(G_runtime)) ;

  // print out some info to show that we're alive.

  delay (1000) ;
  WiFi.mode(WIFI_STA) ;
  Serial.begin(DEF_SERIAL_BAUD) ;
  Serial.printf("\r\nBOOT: Running esp32io git commit %s, built %s.\r\n",
                BUILD_COMMIT, BUILD_TIME) ;
  Serial.printf("BOOT: Wifi mac: %s\r\n", WiFi.macAddress().c_str()) ;
  Serial.printf("BOOT: Chip temperature: %.2fC\r\n", temperatureRead()) ;

  // start the "f_serial_console_thread"

  xTaskCreatePinnedToCore (
    f_serial_console_thread,            // function to run
    "serial_console_thread",            // name for debugging
    DEF_THREAD_STACKSIZE,               // stack size
    NULL,                               // param to pass
    DEF_CONSOLE_THREAD_PRIORITY,        // priority (higher is more important)
    NULL,                               // task handle
    0) ;                                // core ID

  // blink RGB LED to indicate we've completed initialization.

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

