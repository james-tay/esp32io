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
    return(0) ;

  // read the raw temperature into "raw_t"

  unsigned char buf[3] ;
  buf[0] = 0xF4 ;                       // write to the control register
  buf[1] = 0x2E ;                       // request temperature readings
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

  if (G_runtime->config.debug > 1)
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

  // read raw pressure into "pressure_t"

  buf[0] = 0xF4 ;                       // write to control register
  buf[1] = 0x34 + (BMP180_MODE << 6) ;  // request pressure readings
  if (f_i2c_io_write(BMP180_ADDR, buf, 2) != 2)
    return(0) ;
  delay(26) ;

  buf[0] = 0xF6 ;
  if ((f_i2c_io_write(BMP180_ADDR, buf, 1) != 1) ||
      (f_i2c_io_read(BMP180_ADDR, buf, 3) != 3))
    return(0) ;
  long msb = buf[0] ;
  long lsb = buf[1] ;
  long xlsb = buf[2] ;
  long raw_p = ((msb << 16) + (lsb << 8) + xlsb) >> (8 - BMP180_MODE) ;

  // now calculate the true pressure

  long b6 = b5 - 4000 ;
  x1 = (b2 * (b6 * b6) >> 12) >> 11 ;
  x2 = (ac2 * b6) >> 11 ;
  long x3 = x1 + x2 ;
  long b3 = (((ac1 * 4 + x3) << BMP180_MODE) + 2) / 4 ;
  x1 = ac3 * b6 >> 13 ;
  x2 = ((b1 * (b6 * b6)) >> 12) >> 16 ;
  x3 = ((x1 + x2) + 2) >> 2 ;
  unsigned long b4 = (ac4 * (unsigned long)(x3 + 32768)) >> 15 ;
  unsigned long b7 = (raw_p - b3) * (50000 >> BMP180_MODE) ;
  long p ;
  if (b7 < (unsigned long) 0x80000000)
    p = (b7 * 2) / b4 ;
  else
    p = (b7 / b4) * 2 ;
  x1 = (p >> 8) * (p >> 8) ;
  x1 = (x1 * 3038) >> 16 ;
  x2 = (-7357 * p) >> 16 ;
  p = p + ((x1 + x2 + 3791) >> 4) ;
  *pressure = float(p) / 100.0 ;

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
             "Temperature:%.1fC Pressure:%.2fhPa\r\n", temperature, pressure) ;
    G_runtime->worker[idx].result_code = 200 ;
  }
  else
  {
    strncpy(G_runtime->worker[idx].result_msg, "Failed to read BMP180.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 500 ;
  }
}

/*
   This function is called from "f_user_thread_lifecycle()". Our job is to
   poll a BMP180 periodically and expose its temperature and pressure readings.
*/

void ft_bmp180(S_UserThread *self)
{
  // parse our commandline

  if (self->num_args != 1)
  {
    strncpy(self->status, "Incorrect arguments", BUF_LEN_UTHREAD_STATUS) ;
    self->state = UTHREAD_STOPPED ;
    return ;
  }
  int interval_secs = atoi(self->in_args[0]) ;

  // setup the metrics we'll expose

  if (self->loop == 0)
  {
    self->result[0].l_name[0] = "model" ;
    self->result[0].l_data[0] = "bmp180" ;
    self->result[0].l_name[1] = "measurement" ;
    self->result[0].l_data[1] = "temperature" ;

    self->result[1].l_name[0] = "model" ;
    self->result[1].l_data[0] = "bmp180" ;
    self->result[1].l_name[1] = "measurement" ;
    self->result[1].l_data[1] = "pressure" ;

    self->result[2].l_name[0] = "model" ;
    self->result[2].l_data[0] = "bmp180" ;
    self->result[2].l_name[1] = "io" ;
    self->result[2].l_data[1] = "faults" ;
    self->result[2].result_type = UTHREAD_RESULT_INT ;

    self->result[3].ll_value = esp_timer_get_time() ;   // internal timer
    self->state = UTHREAD_RUNNING ;
  }

  float temperature=0.0, pressure=0.0 ;
  if (f_bmp180(&temperature, &pressure))
  {
    self->result[0].f_value = temperature ;
    self->result[0].result_type = UTHREAD_RESULT_FLOAT ;
    self->result[1].f_value = pressure ;
    self->result[1].result_type = UTHREAD_RESULT_FLOAT ;
  }
  else
  {
    self->result[0].result_type = UTHREAD_RESULT_NONE ;
    self->result[1].result_type = UTHREAD_RESULT_NONE ;
    self->result[2].i_value++ ;
  }

  // calculate when our next run would be, then figure out how long to pause

  self->result[3].ll_value += interval_secs * 1000 * 1000 ;
  long long nap_usec = self->result[3].ll_value - esp_timer_get_time() ;
  if (nap_usec > 0)
  {
    snprintf(self->status, BUF_LEN_UTHREAD_STATUS, "pausing %lld ms",
             nap_usec / 1000) ;
    delay(nap_usec / 1000) ;
  }
}

