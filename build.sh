#!/bin/bash
#
# REQUIREMENTS
#
#   # arduino-cli core install esp32:esp32@3.3.6
#
#   Versions
#     - Arduino CLI version 1.2.2
#     - Core esp32:esp32 version 3.3.6
#
# USAGE
#
#   Examples,
#
#     $ export CHIP=esp32s3 ; export SERIAL_PORT=/dev/ttyACM0
#     $ ./build.sh compile && ./build.sh upload_and_connect
#
#     $ export CHIP=esp32cam ; export SERIAL_PORT=/dev/ttyUSB1
#     $ ./build.sh compile && ./build.sh upload_and_connect
#
#   (optionally hardcode initial / fallback wifi credentials)
#
#     $ export WIFI_SSID="mywifi" ; export WIFI_PW="secret"
#     $ ./build.sh compile

ESPTOOL="$HOME/.arduino15/packages/esp32/tools/esptool_py/5.1.0/esptool"
BOARD="esp32:esp32"
BAUD="115200"

# User must set "CHIP" and "SERIAL_PORT"

if [ -z "$CHIP" ] ; then
  echo "FATAL! CHIP is not set ('esp32', 'esp32s3' or 'esp32cam)."
  exit 1
fi
if [ -z "$SERIAL_PORT" ] ; then
  echo "FATAL! SERIAL_PORT is not set (eg, '/dev/ttyUSB1')."
  exit 1
fi

case $CHIP in
'esp32')
  BOARD_OPTS="--fqbn $BOARD:$CHIP"
  SERIAL_TOOL="screen $SERIAL_PORT $BAUD"
  ;;
'esp32s3')
  BOARD_OPTS="--fqbn $BOARD:$CHIP"
  SERIAL_TOOL="screen $SERIAL_PORT $BAUD"
  ;;
'esp32cam')
  BOARD_OPTS="--fqbn $BOARD:$CHIP:PartitionScheme=min_spiffs"
  SERIAL_TOOL="arduino-cli monitor -p $SERIAL_PORT \
                --config baudrate=$BAUD --config dtr=off --config rts=off"
  ;;
*)
  echo "FATAL! Unsupported CHIP."
  exit 1
  ;;
esac

# place the build products in a specific path

BUILD_PATH=".build/$BOARD:$CHIP"
mkdir -p $BUILD_PATH || exit 1

# now perform the user requested action

case $1 in
'compile')
  TIME="`date "+%Y%m%d-%H%M%S"`"
  COMMIT="`git rev-parse --short HEAD`"
  CPPFLAGS="-DBUILD_COMMIT=\"$COMMIT\" -DBUILD_TIME=\"$TIME\""

  # if the user specified wifi SSID and password, incorporate it now.

  if [ -n "$WIFI_SSID" ] && [ -n "$WIFI_PW" ] ; then
    CPPFLAGS="$CPPFLAGS -DWIFI_SSID=\"$WIFI_SSID\" -DWIFI_PW=\"$WIFI_PW\""
  fi

  echo "NOTICE: Compiling for $BOARD:$CHIP - $COMMIT - $TIME"
  arduino-cli compile $BOARD_OPTS --build-path $BUILD_PATH \
    --build-property compiler.cpp.extra_flags="$CPPFLAGS" .
  ;;

'upload')
  echo "NOTICE: Uploading using platform $BOARD:$CHIP to $SERIAL_PORT."
  arduino-cli upload -p $SERIAL_PORT $BOARD_OPTS \
    --build-path $BUILD_PATH --verify .
  ;;

'connect')
  export TERM=vt100 # prevents "screen" from receiving scrollwheel events.
  $SERIAL_TOOL
  ;;

'erase')

  # make sure we can probe the device before we wipe everything on it.

  export CHIP="auto" # let "esptool" handle this
  export NO_COLOR=1 # disable color in "esptool" output
  $ESPTOOL --chip $CHIP --port $SERIAL_PORT flash-id
  if [ $? -ne 0 ] ; then
    exit 1
  fi
  $ESPTOOL --chip $CHIP --port $SERIAL_PORT erase-flash
  ;;

*)
  echo "Usage: $0 { compile | upload | connect | erase }"
  ;;
esac

