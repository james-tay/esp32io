#!/bin/bash

ESPTOOL="$HOME/.arduino15/packages/esp32/tools/esptool_py/5.1.0/esptool"
BOARD="esp32:esp32"

# ESP32-S3 settings

#CHIP="esp32s3"
#SERIAL_PORT="/dev/ttyACM0"

# ESP32 (classic) settings

CHIP="esp32"
SERIAL_PORT="/dev/ttyUSB1"

# =============================================================================

case $1 in
'compile')
  shift 1
  ARGS=$@
  TIME="`date "+%Y%m%d-%H%M%S"`"
  COMMIT="`git rev-parse --short HEAD`"
  CPPFLAGS="-DBUILD_COMMIT=\"$COMMIT\" -DBUILD_TIME=\"$TIME\""

  # if the user specified wifi SSID and password, incorporate it now.

  if [ -n "$WIFI_SSID" ] && [ -n "$WIFI_PW" ] ; then
    CPPFLAGS="$CPPFLAGS -DWIFI_SSID=\"$WIFI_SSID\" -DWIFI_PW=\"$WIFI_PW\""
  fi

  arduino-cli compile -b $BOARD:$CHIP \
    --build-property compiler.cpp.extra_flags="$CPPFLAGS" $ARGS .
  ;;
'upload')
  arduino-cli upload -p $SERIAL_PORT -b $BOARD:$CHIP --verify .
  ;;
'upload_and_connect')
  export TERM=vt100 # prevents "screen" from receiving scrollwheel events.
  arduino-cli upload -p $SERIAL_PORT -b $BOARD:$CHIP --verify . && \
    screen $SERIAL_PORT 115200
  ;;
'erase')

  # try to probe the device before we wipe everything on it.

  export NO_COLOR=1 # disable color in "esptool" output
  $ESPTOOL --chip $CHIP --port $SERIAL_PORT flash-id
  if [ $? -ne 0 ] ; then
    exit 1
  fi
  $ESPTOOL --chip $CHIP --port $SERIAL_PORT erase-flash
  ;;
*)
  echo "Usage: $0 { compile | upload | upload_and_connect | erase }"
  ;;
esac
