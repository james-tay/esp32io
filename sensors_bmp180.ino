/*
   Writes the current "temperature" and "pressure" readings from a BMP180.
   It returns 1 if successful, otherwise 0. Note, this function assumes that
   the I2C bus has already been initialized.
*/

int f_bmp180(float *temperature, float *pressure)
{


  return(0) ;
}

/*
   This function is called from "f_i2c_cmd()" by worker thread "idx". Our job
   is to return temperature and pressure data to the caller.
*/

void f_bmp180_cmd(int idx)
{
  float temperature=0.0, pressure=0.0 ;

  if (f_bmp180(&temperature, &pressure))
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "Temperature:%.3C Pressure:%.3hPa\r\n", temperature, pressure) ;
    G_runtime->worker[idx].result_code = 200 ;
  }
  else
  {
    strncpy(G_runtime->worker[idx].result_msg, "Failed to read BMP180.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 500 ;
  }
}
