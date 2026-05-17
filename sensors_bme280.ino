/*
   The BME280 contains a bunch of registers used for calibrating sensor
   readings. This structure is used to store them all.
*/

struct bme280_calibration
{
  // temperature calibration

  unsigned short dig_T1 ;
  short          dig_T2 ;
  short          dig_T3 ;

  // pressure calibration

  unsigned short dig_P1 ;
  short          dig_P2 ;
  short          dig_P3 ;
  short          dig_P4 ;
  short          dig_P5 ;
  short          dig_P6 ;
  short          dig_P7 ;
  short          dig_P8 ;
  short          dig_P9 ;

  // humidity calibration

  unsigned char  dig_H1 ;
  short          dig_H2 ;
  unsigned char  dig_H3 ;
  short          dig_H4 ;
  short          dig_H5 ;
  char           dig_H6 ;
} ;
typedef struct bme280_calibration S_bme280_calib ;

/*
   Writes the current "temperature", "humidity" and "pressure" from a BME280.
   It returns 1 if successful, otherwise 0. Note that this function assumes
   that the I2C bus has already been initialized.
*/

int f_bme280(float *temperature, float *humidity, float *pressure)
{
  #define BME280_ADDR 0x76              // all BME280 units are at this address
  #define BME280_REG_T 0x88             // temp calibration registers
  #define BME280_REG_P 0x8E             // pressure calibration registers
  #define BME280_REG_H1 0xA1            // humidity calibration registers part1
  #define BME280_REG_H2 0xE1            // humidity calibration registers part2
  #define BME280_REG_H_OSAMP 0xF2       // humidity oversampling
  #define BME280_REG_TP_OSAMP 0xF4      // temp & humidity oversampling
  #define BME280_H_OSAMP_16 0x05        // x16 humidity oversampling
  #define BME280_TP_OSAMP_16 0xB7       // x16 temp & pressure oversampling

  unsigned char buf[18] ;               // general purpose I2C IO buffer
  S_bme280_calib calib ;                // struct for all calibration params

  // read temperature calibration registers. Note that the bytes read from the
  // BME280 are in little endian, since the ESP32 is big endian, we manually
  // "flip" the bytes.

  buf[0] = BME280_REG_T ;
  if ((f_i2c_io_write(BME280_ADDR, buf, 1) != 1) ||
      (f_i2c_io_read(BME280_ADDR, buf, 6)  != 6))
    return(0) ;                                         // can't talk to BME280
  calib.dig_T1 = (buf[1] << 8) | buf[0] ;
  calib.dig_T2 = (buf[3] << 8) | buf[2] ;
  calib.dig_T3 = (buf[5] << 8) | buf[4] ;

  // read pressure calibration parameters

  buf[0] = BME280_REG_P ;
  if ((f_i2c_io_write(BME280_ADDR, buf, 1) != 1) ||
      (f_i2c_io_read(BME280_ADDR, buf, 18)  != 18))
    return(0) ;                                         // can't talk to BME280
  calib.dig_P1 = (buf[1]  << 8) | buf[0] ;
  calib.dig_P2 = (buf[3]  << 8) | buf[2] ;
  calib.dig_P3 = (buf[5]  << 8) | buf[4] ;
  calib.dig_P4 = (buf[7]  << 8) | buf[6] ;
  calib.dig_P5 = (buf[9]  << 8) | buf[8] ;
  calib.dig_P6 = (buf[11] << 8) | buf[10] ;
  calib.dig_P7 = (buf[13] << 8) | buf[12] ;
  calib.dig_P8 = (buf[15] << 8) | buf[14] ;
  calib.dig_P9 = (buf[17] << 8) | buf[16] ;

  // read humidity calibration parameters, note there are 2 parts

  buf[0] = BME280_REG_H1 ;
  if ((f_i2c_io_write(BME280_ADDR, buf, 1) != 1) ||
      (f_i2c_io_read(BME280_ADDR, buf, 1)  != 1))
    return(0) ;                                         // can't talk to BME280
  calib.dig_H1 = buf[0] ;

  buf[0] = BME280_REG_H2 ;
  if ((f_i2c_io_write(BME280_ADDR, buf, 1) != 1) ||
      (f_i2c_io_read(BME280_ADDR, buf, 7)  != 7))
    return(0) ;                                         // can't talk to BME280
  calib.dig_H2 = (buf[1] << 8) | buf[0] ;
  calib.dig_H3 = buf[2] ;
  calib.dig_H4 = ((int16_t)buf[3] << 4) | (buf[4] & 0x0F) ;
  calib.dig_H5 = ((int16_t)buf[5] << 4) | (buf[4] >> 4) ;
  calib.dig_H6 = (int8_t)buf[6] ;

  // configure sensor oversampling

  buf[0] = BME280_REG_H_OSAMP ;
  buf[1] = BME280_H_OSAMP_16 ;
  if (f_i2c_io_write(BME280_ADDR, buf, 2) != 2)
    return(0) ;
  buf[0] = BME280_REG_TP_OSAMP ;
  buf[1] = BME280_TP_OSAMP_16 ;
  if (f_i2c_io_write(BME280_ADDR, buf, 2) != 2)
    return(0) ;



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
