/*
   This function is called from "f_i2c_cmd()" by worker thread "idx". We
   call "Wire.end()" which releases all allocated resources. Can also be
   used to "reset" the I2C bus.
*/

void f_i2c_end_cmd(int idx)
{
  Wire.end() ;
  strncat(G_runtime->worker[idx].result_msg, "I2C de-initialized.\r\n",
          BUF_LEN_WORKER_RESULT) ;
  G_runtime->worker[idx].result_code = 200 ;
}

/*
   This is a general purpose function which reads "len" bytes into "buf" from
   an I2C device address "dev". Typically the user would have already written
   to a register, but this is device dependent. The total number of bytes read
   is returned.
*/

int f_i2c_io_read(unsigned char dev, unsigned char *buf, int len)
{
  int total_read=0 ;
  unsigned char *p=buf ;

  Wire.requestFrom(dev, len) ;
  while ((total_read < len) && (Wire.available() > 0))
  {
    *p = Wire.read() ;
    total_read++ ;
    p++ ;
  }
  return(total_read) ;
}

/*
   This function is called from "f_i2c_cmd()" by worker thread "idx". Our job
   is to read "num_bytes" from the I2C device "hex_dev". The bytes read are
   printed as hex.
*/

void f_i2c_read_cmd(int idx, char *hex_dev, int num_bytes)
{
  unsigned char buf[DEF_I2C_IO_BYTES] ;

  if (num_bytes < 0)
    num_bytes = 0 ;
  if (num_bytes > DEF_I2C_IO_BYTES)
    num_bytes = DEF_I2C_IO_BYTES ;

  // before we call "f_i2c_io_read()", we need to convert the hex string

  unsigned char dev_value = (unsigned char) strtoul(hex_dev, NULL, 16) ;
  int amt = f_i2c_io_read(dev_value, buf, num_bytes) ;

  // now dress up "buf" as hex in "result_msg"

  snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
           "Read %d bytes: ", amt) ;
  int remainder = BUF_LEN_WORKER_RESULT -
                  strlen(G_runtime->worker[idx].result_msg) ;
  char *p = G_runtime->worker[idx].result_msg +
            strlen(G_runtime->worker[idx].result_msg) ;
  for (int i=0 ; i < amt ; i++)
  {
    snprintf(p, remainder, "%02x ", buf[i]) ;
    p = p + 3 ;
    remainder = remainder - 3 ;
  }
  strncat(G_runtime->worker[idx].result_msg, "\r\n", remainder) ;

  if (amt == num_bytes)
    G_runtime->worker[idx].result_code = 200 ;
  else
    G_runtime->worker[idx].result_code = 500 ;
}

/*
   This function is called from "f_i2c_cmd()" by worker thread "idx". Our job
   is to initialize the ESP32's I2C hardware by configuring the specified "sda"
   and "scl" pins.
*/

void f_i2c_init_cmd(int idx, int sda, int scl, char *s_clk_khz)
{
  int clock_khz = 100 ;

  if (s_clk_khz != NULL)
    clock_khz = atoi(s_clk_khz) ;
  if (clock_khz > DEF_I2C_MAX_CLOCK_KHZ)
    clock_khz = DEF_I2C_MAX_CLOCK_KHZ ;
  if (clock_khz < DEF_I2C_MIN_CLOCK_KHZ)
    clock_khz = DEF_I2C_MIN_CLOCK_KHZ ;

  if (Wire.getClock() > 0)
  {
    strncpy(G_runtime->worker[idx].result_msg, "Already initialized.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }

  Wire.begin(sda, scl, clock_khz * 1000) ;
  snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
           "I2C master using sda:%d scl:%d at %d hz.\r\n",
           sda, scl, Wire.getClock()) ;
  G_runtime->worker[idx].result_code = 200 ;
}

/*
   This function is called from "f_i2c_cmd()". Our job is to scan the I2C bus
   and print any devices found (at various addresses). Note that we'll be
   using 7-bit addressing, but note that addresses below 0x08 and above 0x78
   are reserved.
*/

void f_i2c_scan_cmd(int idx)
{
  int remainder ;
  char s[8] ;
  unsigned char addr ;

  for (addr = 0x08 ; addr < 0x78 ; addr++)
  {
    Wire.beginTransmission(addr) ;
    if (Wire.endTransmission() == 0)    // this means something ACK'ed
    {
      if (strlen(G_runtime->worker[idx].result_msg) == 0)
      {
        snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
                 "Found devices at - 0x%02x", addr) ;
      }
      else
      {
        sprintf(s, " 0x%hhx", addr) ;
        remainder = BUF_LEN_WORKER_RESULT -
                    strlen(G_runtime->worker[idx].result_msg) ;
        strncat(G_runtime->worker[idx].result_msg, s, remainder) ;
      }
    }
  }

  remainder = BUF_LEN_WORKER_RESULT -
              strlen(G_runtime->worker[idx].result_msg) ;
  if (strlen(G_runtime->worker[idx].result_msg) == 0)
    strncpy(G_runtime->worker[idx].result_msg, "No devices found.\r\n",
            remainder) ;
  else
    strncat(G_runtime->worker[idx].result_msg, "\r\n", remainder) ;
  G_runtime->worker[idx].result_code = 200 ;
}

/*
   A general purpose function which writes "buf" which is "len" bytes long,
   to "dev". The total number of bytes successfully written is returned, or
   0 if the I2C device did not ack.
*/

int f_i2c_io_write(unsigned char dev, unsigned char *buf, int len)
{
  int total_written=0 ;

  Wire.beginTransmission(dev) ;
  total_written = Wire.write(buf, len) ;        // adds to outgoing buffer
  if (Wire.endTransmission() != 0)
    return(0) ;                                 // opsie something went wrong
  return(total_written) ;
}

/*
   This function is called from "f_i2c_cmd()" by worker thread "idx". Our job
   is to perform an I2C write to the device at "hex_dev". The "hex_data" string
   contains comma separated hex bytes, and may be NULL if we dont' actually
   intend to write any data.
*/

void f_i2c_write_cmd(int idx, char *hex_dev, char *hex_data)
{
  int len=0 ;
  char *p, *hex_str ;
  unsigned char buf[DEF_I2C_IO_BYTES] ;

  // parse "hex_data" string, write the raw byte values into "buf" while
  // making a note in "len".

  hex_str = strtok_r(hex_data, ",", &p) ;
  while (hex_str)
  {
    buf[len] = (unsigned char) strtoul(hex_str, NULL, 16) ;
    hex_str = strtok_r(NULL, ",", &p) ;
    len++ ;
  }

  unsigned char dev_value = (unsigned char) strtoul(hex_dev, NULL, 16) ;
  int amt = f_i2c_io_write(dev_value, buf, len) ;

  snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
           "Wrote %d bytes.\r\n", amt) ;
  if (amt == len)
    G_runtime->worker[idx].result_code = 200 ;
  else
    G_runtime->worker[idx].result_code = 500 ;
}

/*
   This is a convenience function which reads an 8-bit value at "addr" from
   "dev". The result is written to "result". On success this function returns
   1, otherwise 0 if something went wrong.
*/

int f_i2c_reg_read_char(unsigned char dev, unsigned char addr,
                        unsigned char *result)
{
  if (f_i2c_io_write(dev, &addr, 1) != 1)               // I2C write failed
  {
    if (G_runtime->config.debug)
      Serial.printf("DEBUG: f_i2c_reg_read_char(): cannot write to 0x%x.\r\n",
                    dev) ;
    return(0) ;
  }
  if (f_i2c_io_read(dev, result, sizeof(char)) == sizeof(char))
    return(1) ;
  else
  {
    if (G_runtime->config.debug)
      Serial.printf("DEBUG: f_i2c_reg_read_char(): read from 0x%x failed.\r\n",
                    dev) ;
    return(0) ;
  }
}

/*
   This is a convenience function which reads a 16-bit value at "addr" from
   "dev". The result is written to "result". On success this function returns
   1, otherwise 0 if something went wrong.
*/

int f_i2c_reg_read_short(unsigned char dev, unsigned char addr, short *result)
{
  unsigned char buf[2] ;

  if (f_i2c_io_write(dev, &addr, 1) != 1)               // I2C write failed
  {
    if (G_runtime->config.debug)
      Serial.printf("DEBUG: f_i2c_reg_read_short(): cannot write to 0x%x.\r\n",
                    dev) ;
    return(0) ;
  }
  if (f_i2c_io_read(dev, buf, sizeof(short)) == sizeof(short))
  {
    int v = (buf[0] << 8) + buf[1] ;
    if (v > 32767)                              // value is actually negative
      v = v - 65536 ;
    *result = (short) v ;
    return(1) ;
  }
  else
  {
    if (G_runtime->config.debug)
      Serial.printf("DEBUG: f_i2c_reg_read_short(): read from 0x%x failed.\r\n",
                    dev) ;
    return(0) ;
  }
}

/*
   This function is called from "f_action()" by the worker thread at "idx".
   Our job is to parse the user supplied "i2c ..." command to identify what
   to do next.
*/

void f_i2c_cmd(int idx)
{
  // parse our "i2c ..." command, or print help

  int num_tokens ;
  char *tokens[5], *cmd=NULL ;
  
  num_tokens = f_parse(G_runtime->worker[idx].cmd, tokens, 5) ;
  if (num_tokens < 2)
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "i2c bme280                                read from a BME280\r\n"
             "i2c bmp180                                read from a BMP180\r\n"
             "i2c end                                   de-initialize I2C\r\n"
             "i2c read <hexDev> <numBytes>              read data\r\n"
             "i2c init <sdaPin> <sclPin> [clk_khz]      setup I2C master\r\n"
             "i2c scan                                  scan for devices\r\n"
             "i2c write <hexDev> [<hexByte>,...]        write byte(s)\r\n",
             DEF_I2C_MIN_CLOCK_KHZ,
             DEF_I2C_MAX_CLOCK_KHZ) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }
  cmd = tokens[1] ;

  if ((strcmp(cmd, "bme280") == 0) && (num_tokens == 2))        // bme280
    f_bme280_cmd(idx) ;
  else
  if ((strcmp(cmd, "bmp180") == 0) && (num_tokens == 2))        // bmp180
    f_bmp180_cmd(idx) ;
  else
  if ((strcmp(cmd, "end") == 0) && (num_tokens == 2))           // end
    f_i2c_end_cmd(idx) ;
  else
  if ((strcmp(cmd, "read") == 0) && (num_tokens == 4))          // read
    f_i2c_read_cmd(idx, tokens[2], atoi(tokens[3])) ;
  else
  if ((strcmp(cmd, "init") == 0) && (num_tokens >= 4))          // init
    f_i2c_init_cmd(idx, atoi(tokens[2]), atoi(tokens[3]), tokens[4]) ;
  else
  if ((strcmp(cmd, "scan") == 0) && (num_tokens == 2))          // scan
    f_i2c_scan_cmd(idx) ;
  else
  if ((strcmp(cmd, "write") == 0) && (num_tokens >= 3))         //write
    f_i2c_write_cmd(idx, tokens[2], tokens[3]) ;
  else
  {
    strncpy(G_runtime->worker[idx].result_msg, "Invalid action\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
  }
}
