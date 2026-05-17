/*
   Writes the current "temperature", "humidity" and "pressure" from a BME280.
   It returns 1 if successful, otherwise 0. Note that this function assumes
   that the I2C bus has already been initialized.
*/

int f_bme280(float *temperature, float *humidity, float *pressure)
{



  return(1) ;
}

/*
   This function is called from f_i2c_cmd()" by worker thread "idx". Our job
   is to return temperature, humidity and pressure readings to the caller.
*/

void f_bme280_cmd(int idx)
{
  float temperature=0.0, humidity=0.0, pressure=0.0 ;

  if (f_bme280(&temperature, &humidity, &pressure))
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "Temperature:%.1fC Humidity:%.1f%% Pressure:%.1fhPa\r\n",
             temperature, humidity, pressure) ;
    G_runtime->worker[idx].result_code = 200 ;
  }
  else
  {
    strncpy(G_runtime->worker[idx].result_msg, "Failed to read BME280.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 500 ;
  }
}
