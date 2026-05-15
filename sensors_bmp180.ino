/*
   Writes the current "temperature" and "pressure" readings from a BMP180.
   It returns 1 if successful, otherwise 0. Note, this function assumes that
   the I2C bus has already been initialized.
*/

int f_bmp180(float *temperature, float *pressure)
{
  #define BMP180_ADDR 0x77      /* this came from the BMP180 data sheet */
  #define BMP180_MODE 3         /* the pressure oversampling */

  /* read the 11x 16-bit registers on the BMP180 for calibration data */

  short ac1, ac2, ac3, b1, b2, mb, mc, md ;
  unsigned short ac4, ac5, ac6 ;

  if ((f_i2c_reg_read_short(BMP180_ADDR, 0xAA, &ac1) == 0) ||
      (f_i2c_reg_read_short(BMP180_ADDR, 0xAC, &ac2) == 0) ||
      (f_i2c_reg_read_short(BMP180_ADDR, 0xAE, &ac3) == 0) ||
      (f_i2c_reg_read_short(BMP180_ADDR, 0xB0, (short*) &ac4) == 0) ||
      (f_i2c_reg_read_short(BMP180_ADDR, 0xB2, (short*) &ac5) == 0) ||
      (f_i2c_reg_read_short(BMP180_ADDR, 0xB4, (short*) &ac6) == 0) ||
      (f_i2c_reg_read_short(BMP180_ADDR, 0xB6, (short*) &b1) == 0) ||
      (f_i2c_reg_read_short(BMP180_ADDR, 0xB8, (short*) &b2) == 0) ||
      (f_i2c_reg_read_short(BMP180_ADDR, 0xBA, (short*) &mb) == 0) ||
      (f_i2c_reg_read_short(BMP180_ADDR, 0xBC, (short*) &mc) == 0) ||
      (f_i2c_reg_read_short(BMP180_ADDR, 0xBE, (short*) &md) == 0))
  {
    return(0) ;
  }

  // read the raw temperature into "raw_t"

  unsigned char buf[2] ;
  buf[0] = 0xF4 ;
  buf[1] = 0x2E ;
  if (f_i2c_io_write(BMP180_ADDR, buf, 2) != 2)
    return(0) ;
  delay(5) ;

  buf[0] = 0xF6 ;
  if ((f_i2c_io_write(BMP180_ADDR, buf, 1) != 1) ||
      (f_i2c_io_read(BMP180_ADDR, buf, 2) != 2))
    return(0) ;
  int v = (buf[0] << 8) + buf[1] ;
  if (v > 32767)                                // value is actually negative
    v = v - 65536 ;
  short raw_t = (short) v ;

  if (G_runtime->config.debug)
    Serial.printf("DEBUG: f_bmp180()\r\n"
                  "  ac1: %d\r\n"
                  "  ac2: %d\r\n"
                  "  ac3: %d\r\n"
                  "  ac4: %d\r\n"
                  "  ac5: %d\r\n"
                  "  ac6: %d\r\n"
                  "  b1: %d\r\n"
                  "  b2: %d\r\n"
                  "  mb: %d\r\n"
                  "  mc: %d\r\n"
                  "  md: %d\r\n"
                  "  raw_t: %d\r\n",
                  ac1, ac2, ac3, ac4, ac5, ac6, b1, b2, mb, mc, md, raw_t) ;

  // now calculate the true temperature

  long x1 = (raw_t - ac6) * ac5 >> 15 ;
  long x2 = (mc << 11) / (x1 + md) ;
  long b5 = x1 + x2 ;
  long t = (b5 + 8) >> 4 ;
  *temperature = float(t) / 10.0 ;





  return(1) ;
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
             "Temperature:%.1fC Pressure:%.1fhPa\r\n", temperature, pressure) ;
    G_runtime->worker[idx].result_code = 200 ;
  }
  else
  {
    strncpy(G_runtime->worker[idx].result_msg, "Failed to read BMP180.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 500 ;
  }
}
