#!/bin/bash

SERIAL_PORT="/dev/ttyACM0"

case $1 in
'compile')
  TIME="`date "+%Y%m%d-%H%M%S"`"
  COMMIT="`git rev-parse --short HEAD`"

  arduino-cli compile -b esp32:esp32:esp32s3 \
    --build-property \
        compiler.cpp.extra_flags="
          -DBUILD_COMMIT=\"$COMMIT\" \
          -DBUILD_TIME=\"$TIME\"" \
    .
  ;;
'upload')
  arduino-cli upload -p $SERIAL_PORT -b esp32:esp32:esp32s3 --verify .
  ;;
'upload_and_connect')
  arduino-cli upload -p $SERIAL_PORT -b esp32:esp32:esp32s3 --verify . && \
    screen $SERIAL_PORT 115200
  ;;
*)
  echo "Usage: $0 { compile | upload | upload_and_connect }"
  ;;
esac
