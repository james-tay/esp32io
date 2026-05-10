/*
   This function is called from "f_i2c_cmd()". Our job is to initialize the
   ESP32's I2C hardware by configuring the specified "sda" and "scl" pins.
*/

void f_i2c_init(int idx, int sda, int scl)
{
  Wire.begin(sda, scl) ;
  snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
           "I2C master using sda:%d scl:%d.\r\n", sda, scl) ;
  G_runtime->worker[idx].result_code = 200 ;
}

/*
   This function is called from "f_i2c_cmd()". Our job is to scan the I2C bus
   and print any devices found (at various addresses). Note that we'll be
   using 7-bit addressing, but note that addresses below 0x08 and above 0x78
   are reserved.
*/

void f_i2c_scan(int idx)
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
    strncpy(G_runtime->worker[idx].result_msg,
            "i2c read <hexDev> <hexAddr> <numBytes>     read data\r\n"
            "i2c init <sdaPin> <sclPin>                 setup I2C master\r\n"
            "i2c scan                                   scan for devices\r\n"
            "i2c write <hexDev> <hexByte>[,...]         write byte(s)\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }
  cmd = tokens[1] ;

  if ((strcmp(cmd, "init") == 0) && (num_tokens == 4))          // init
    f_i2c_init(idx, atoi(tokens[2]), atoi(tokens[3])) ;
  else
  if ((strcmp(cmd, "scan") == 0) && (num_tokens == 2))          // scan
    f_i2c_scan(idx) ;
  else
  {
    strncpy(G_runtime->worker[idx].result_msg, "Invalid action\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
  }
}
