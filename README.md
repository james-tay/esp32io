# ESP32IO

## What is ESP32IO

Long ago, [mygpio](https://github.com/james-tay/mygpio/tree/master) was
written to provide REST as a means of easy interaction with an ESP32. This
software facilitated interaction with various sensors, task thread management
and prometheus metric export. Over the years certain features proved their
utility, while others didn't. Certain limitations also started creeping up. In
particular, a single thread which handled serial console, web service and
primary task execution, led to an overall poor experience.

ESP32IO is a complete rewrite of this software to modernize the interaction
with the ESP32. The prominent features now include

  - dedicated serial console thread
  - dedicated web server thread which handles web client IO
  - worker thread pool (configurable) which executes the actual tasks
  - lots more prometheus metrics exposed
  - time is now tracked with long long (not unsigned int, ie, 49.7 day limit)
  - support for the ESP32, ESP32-CAM and ESP32-S3 (new)
  - still supports OTA updating

Note that one of the main down sides of this new architecture, is reduced
free heap. This "loss" is primarily attributed to the use of more threads.

## Build and Setup

Take a look at `build.sh`, it describes how to setup 
[arduino-cli](https://github.com/arduino/arduino-cli) environment on a linux
machine. The `build.sh` script compiles and flashes the software via the
ESP32's USB-serial interface. It also helps connect you to the ESP32's serial
console. For example, to compile this code for an ESP32-S3,

```
$ export CHIP=esp32s3
$ export SERIAL_PORT=/dev/ttyACM0
$ ./build.sh compile
NOTICE: Compiling for esp32:esp32:esp32s3 - c1154c4 - 20260502-225831
Sketch uses 1089307 bytes (83%) of program storage space. Maximum is 1310720 bytes.
Global variables use 56180 bytes (17%) of dynamic memory, leaving 271500 bytes for local variables. Maximum is 327680 bytes.
```

To flash the ESP32-S3,

```
$ ./build.sh upload
NOTICE: Uploading using platform esp32:esp32:esp32s3 to /dev/ttyACM0.
esptool v5.1.0
Connected to ESP32-S3 on /dev/ttyACM0:
...
...
```

To connect to the ESP32-S3's USB serial console,
``` 
$ ./build.sh connect
```

Once you're connected, you should see the `>` prompt. From here, you can type
`help` to access its command reference. To perform the initial setup, format
the SPIFFS filesystem. For example,

```
> fs format
> reload
```

After the ESP32 has rebooted, create 2 files which hold your wifi settings.
In this example, the wifi SSID is `sunshine` and its password is `moonlight`,

```
> fs write /wifi_ssid.cfg sunshine
> fs write /wifi_pw.cfg moonlight
> reload
```

At this point, the ESP32 should be connected to wifi. You should be able to
interact with the ESP32 using `curl` from this point on (you can still use
the serial console if you wish). For example,

```
$ curl http://<esp32>/v1?cmd=version
```

Some commands may have multiple arguments, separate arguments with a `+`.
For example,

```
$ curl http://<esp32>/v1?cmd=wifi+status
```

Prometheus metrics can be scraped at,

```
$ curl http://<esp32>/metrics
```

## Setting Up User Task Threads

A user task thread can be configured to regularly interact with a peripheral
device or sensor. In the following example, we create a thread running on CPU
core `1` which polls a DHT22 sensor. This sensor's data pin is on GPIO36, is
powered from GPIO35 and is polled every 30 seconds.

```
$ curl http://<esp32>/v1?cmd=fs+write+/env1.thread+ft_dht22:1,36,35,30
```

Once this file is created, we can launch the thread.

```
$ curl http://<esp32>/v1?cmd=task+start+env1
```

For a quick reference on the configuration of various user task threads, see

```
$ curl http://<esp32>/v1?cmd=task+help
```

The sensor data is now exposed as prometheus metrics. To customize the metrics
and its labels, create a file containing the desired metric name and any
optional labels desired. For example,

```
$ curl http://<esp32>/v1?cmd=fs+write+/env1.labels+sensor,model=\"dht22\",location=\"kitchen\"
```

Once the file is written, the change takes effect immediately. When scraping
the ESP32, the following metric is now exposed,

```
sensor{model="dht22",location="kitchen",measurement="temperature"} 22.700001
sensor{model="dht22",location="kitchen",measurement="humidity"} 57.200001
...
```

