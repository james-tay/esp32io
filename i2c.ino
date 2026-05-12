/*
   This is a general purpose function which reads "len" bytes into "buf" from
   an I2C device address "dev". Typically the user would have already written
   to a register, but this is device dependent. The total number of bytes read
   is returned.
*/

int i2c_io_read(unsigned char dev, unsigned char *buf, int len)
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

  // before we call "i2c_io_read()", we need to convert the hex string

  unsigned char dev_value = (unsigned char) strtoul(hex_dev, NULL, 16) ;
  int amt = i2c_io_read(dev_value, buf, num_bytes) ;

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

void f_i2c_init_cmd(int idx, int sda, int scl)
{
  if (Wire.getClock() > 0)
  {
    strncpy(G_runtime->worker[idx].result_msg, "Already initialized.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }

  Wire.begin(sda, scl) ;
  snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
           "I2C master using sda:%d scl:%d at %dhz.\r\n",
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
  unsigned char buf[DEF_I2C_IO_BYTES] ;

  // parse "hex_data" string, write the raw byte values into "buf" while
  // making a note in "len".





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
  char *tokens[4], *cmd=NULL ;
  
  num_tokens = f_parse(G_runtime->worker[idx].cmd, tokens, 5) ;
  if (num_tokens < 2)
  {
    strncpy(G_runtime->worker[idx].result_msg,
            "i2c read <hexDev> <numBytes>             read data\r\n"
            "i2c init <sdaPin> <sclPin>               setup I2C master\r\n"
            "i2c scan                                 scan for devices\r\n"
            "i2c write <hexDev> [<hexByte>,...]       write byte(s)\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }
  cmd = tokens[1] ;

  if ((strcmp(cmd, "read") == 0) && (num_tokens == 4))          // read
    f_i2c_read_cmd(idx, tokens[2], atoi(tokens[3])) ;
  else
  if ((strcmp(cmd, "init") == 0) && (num_tokens == 4))          // init
    f_i2c_init_cmd(idx, atoi(tokens[2]), atoi(tokens[3])) ;
  else
  if ((strcmp(cmd, "scan") == 0) && (num_tokens == 2))          // scan
    f_i2c_scan_cmd(idx) ;
  if ((strcmp(cmd, "write") == 0) && (num_tokens >= 3))         //write
    f_i2c_write_cmd(idx, tokens[2], tokens[3]) ;
  else
  {
    strncpy(G_runtime->worker[idx].result_msg, "Invalid action\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
  }
}
