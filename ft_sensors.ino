/*
   One of the side effects of having a large number of sensors (eg, DHT22,
   DS18B20, HC-SR04, ACS712, etc) connected to an ESP32, is that running
   individual threads to poll and expose their metrics results in a high
   amount of memory consumption (due to having to allocate independent thread
   stacks). In many scenarios, we do not need high speed polling of these
   sensors, so it might be more efficient to have a single thread manage the
   polling of multiple sensors and exposing their results.

   This is the motivation behind the "ft_sensors()" user task thread. The
   standard configuration supplied to this function includes the overall
   polling frequency (eg, every 60 secs), and the file which configures each
   polling cycle. Thus each line in this file would tell us,

     - what preparation to perform (eg, power up a group of sensors)
     - the sensor function to call
       - the pin(s) where this sensor(s) is attached to
       - labels to be associated with this sensor's metrics
     - what post-poll actions to perform (eg, power down sensors)

   Consider the following polling cycle file,

     c:hi 23
     f:f_sensor_dht22;d:18;l:location="kitchen",model="dht22"
     c:lo 23
     c:hi 22
     f:f_sensor_ds18b20;d:19;l:location="garage",model="ds18b20"
     c:lo 22
     f:f_hcsr04;d:17,16;l:location="entrance",type="proximity"
     f:aread;d:36;l:location="solar",type="acs712-5"

   From the above example, we see this file is organized into tasks, one task
   per line. Within each task, various parameters are seperated by semi-colons.
   The following parameters are supported:

     c:         A command, any supported command may be executed
     f:         Function name. Only certain sensor functions are supported
     d:         Function data. Depends on the sensor function
     l:         Labels to be included in exposed metrics

   Currently, lines must begin with with a "c:" or a "f:".
*/

void ft_sensors(S_UserThread *self)
{



}
