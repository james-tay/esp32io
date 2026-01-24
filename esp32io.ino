#define DEF_SERIAL_BAUD	115200
#define DEF_RGBLED_PIN 48

#include <WiFi.h>

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
