#!/bin/bash

SERIAL_PORT="/dev/ttyACM0"

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

  arduino-cli compile -b esp32:esp32:esp32s3 \
    --build-property compiler.cpp.extra_flags="$CPPFLAGS" $ARGS .
  ;;
'upload')
  arduino-cli upload -p $SERIAL_PORT -b esp32:esp32:esp32s3 --verify .
  ;;
'upload_and_connect')
  export TERM=vt100 # prevents "screen" from receiving scrollwheel events.
  arduino-cli upload -p $SERIAL_PORT -b esp32:esp32:esp32s3 --verify . && \
    screen $SERIAL_PORT 115200
  ;;
*)
  echo "Usage: $0 { compile | upload | upload_and_connect }"
  ;;
esac
