#define DEF_SERIAL_BAUD	115200
#define DEF_RGBLED_PIN 48
#define DEF_THREAD_STACKSIZE 2048
#define DEF_CONSOLE_THREAD_DELAY_MS 20
#define DEF_CONSOLE_THREAD_PRIORITY 1
#define BUF_LEN_CONSOLE 256

#include <WiFi.h>

// ============================================================================

void f_serial_console_thread(void *param)
{
  int buf_pos = 0 ;
  char *buf = NULL ;

  buf = (char*) malloc(BUF_LEN_CONSOLE) ;
  while (1)
  {
    // sit here until we read a full command

    buf_pos = 0 ;
    buf[0] = 0 ;
    Serial.printf("> ") ;
    while (1)
    {
      if (Serial.available() > 0)
      {
        char c = (char) Serial.read() ;
        if ((c == 8) || (c == 127))                     // handle BS or DEL
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
        if (c > 0)                                      // a normal char
        {
          Serial.printf("%c", c) ;
          buf[buf_pos] = c ;
          buf[buf_pos+1] = 0 ;
          buf_pos++ ;
        }
      }
      else
        vTaskDelay(DEF_CONSOLE_THREAD_DELAY_MS) ; // resets the task watchdog.
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
  delay (1000) ;
}

