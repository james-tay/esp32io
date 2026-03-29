/*
   This function is called from "ft_serial()" when we're ready to interface
   an (incoming) TCP client with a UART. Our job is to focus on the main loop
   for the lifecycle of the serial/TCP service. We want to use "select()" to
   multiplex between "listen_sd", "client_sd" and the UART
*/

void f_serial_io(S_UserThread *self, int listen_sd)
{
  int num_fds, result, client_sd=-1 ;
  size_t amt ;
  long long loops=0 ;
  char iobuf[BUF_LEN_LINE] ;
  fd_set rfds ;
  struct timeval tv ;

  // use short "select()" cycles, be ready for a quick exit in the main loop

  while(self->state == UTHREAD_RUNNING)
  {
    tv.tv_sec = 0 ;
    tv.tv_usec = G_runtime->config.uart_poll_ms * 1000 ;
    FD_ZERO(&rfds) ;
    FD_SET(listen_sd, &rfds) ;          // always monitor "listen_sd"
    num_fds = listen_sd ;

    if (client_sd >= 0)                 // monitor "client_sd" if connected
    {
      FD_SET(client_sd, &rfds) ;
      num_fds = max(num_fds, client_sd) ;
    }

    result = select(num_fds+1, &rfds, NULL, NULL, &tv) ;
    if (result)
    {
      if (FD_ISSET(listen_sd, &rfds))   // incoming TCP Client
      {
        int new_sd = accept(listen_sd, NULL, NULL) ;
        if (client_sd >= 0)
          close(new_sd) ;               // already have a connected client
        else
          client_sd = new_sd ;          // really accept new client
      }
      if ((client_sd >=0) && (FD_ISSET(client_sd, &rfds))) // data from client
      {
        amt = read(client_sd, iobuf, BUF_LEN_LINE) ;
        if (amt < 1)
        {
          close(client_sd) ;            // TCP client disconnected ?
          client_sd = -1 ;
        }
        else
          uart_write_bytes(UART_NUM_2, iobuf, amt) ;
      }
    }

    // now take a peek at the UART to see if data came in

    amt = 0 ;
    uart_get_buffered_data_len(UART_NUM_2, &amt) ;
    if (amt > 0)
    {
      amt = uart_read_bytes(UART_NUM_2, iobuf, amt, 0) ;
      if (amt > 0)
        write(client_sd, iobuf, amt) ;
    }

    loops++ ;
    snprintf(self->status, BUF_LEN_UTHREAD_STATUS, "busy:%d loops:%lld",
             result, loops) ;
  }

  if (client_sd >= 0)
    close(client_sd) ;
}

/*
   This function is called from "f_user_thread_lifecycle()". Our job is to
   setup a listening TCP socket and then pass a TCP client's IO to a pair of
   pins configured for TTL serial IO (bidirectional). This function does not
   return unless the task (thread) is being terminated (ie, UTHREAD_WRAPUP).
   This thread makes use of the ESP32's hardware serial UART. Note that there
   are typically multiple UARTs on an ESP32, specifically,

     Serial = UART0 - USB programming & debugging
     Serial1 = UART1 - internal flash
     Serial2 = UART2 - peripherals (this is the one we're using)

   To prevent other threads from accessing UART2, this thread must acquire
   "L_uart" in order to proceed. Once the lock is acquired, this thread sets
   up the serial port such that it interacts via a file descriptor. When it's
   time to terminate, this thread cleans up and finally releases "L_uart". At
   this point, this function returns.
*/

void ft_serial(S_UserThread *self)
{
  if (self->num_args != 4)
  {
    strncpy(self->status, "Incorrect arguments", BUF_LEN_UTHREAD_STATUS) ;
    self->state = UTHREAD_STOPPED ;
    return ;
  }

  int port = atoi(self->in_args[0]) ;
  int baud = atoi(self->in_args[1]) ;
  int rx_pin = atoi(self->in_args[2]) ;
  int tx_pin = atoi(self->in_args[3]) ;

  // setup the listening socket

  int listen_sd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP) ;
  if (listen_sd < 0)
  {
    strncpy(self->status, "Cannot create listen_sd", BUF_LEN_UTHREAD_STATUS) ;
    self->state = UTHREAD_STOPPED ;
    return ;
  }
  struct sockaddr_in addr ;
  memset(&addr, 0, sizeof(addr)) ;
  addr.sin_family = AF_INET ;
  addr.sin_addr.s_addr = INADDR_ANY ;
  addr.sin_port = htons(port) ;
  if (bind(listen_sd, (const struct sockaddr*) &addr, sizeof(addr)) < 0)
  {
    close(listen_sd) ;
    strncpy(self->status, "Cannot bind listen_sd", BUF_LEN_UTHREAD_STATUS) ;
    self->state = UTHREAD_STOPPED ;
    return ;
  }
  if (listen(listen_sd, 1) != 0)
  {
    close(listen_sd) ;
    strncpy(self->status, "Cannot listen on sd", BUF_LEN_UTHREAD_STATUS) ;
    self->state = UTHREAD_STOPPED ;
    return ;
  }

  // acquire UART lock, as there's only 1x hardware UART for user applications

  if (xSemaphoreTake(G_runtime->L_uart, 0) != pdTRUE)
  {
    close(listen_sd) ;
    strncpy(self->status, "Cannot lock UART", BUF_LEN_UTHREAD_STATUS) ;
    self->state = UTHREAD_STOPPED ;
    return ;
  }

  // Initialize the serial port and then hand work over to "f_serial_io()"

  self->state = UTHREAD_RUNNING ;
  uart_config_t uart_config = {
      .baud_rate = baud,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT
    } ;
  uart_param_config(UART_NUM_2, &uart_config) ;
  if (uart_driver_install(UART_NUM_2,
                          1024,                 // serial RX buffer size
                          1024,                 // serial TX buffer size
                          0,                    // queue size (ie, no queue)
                          NULL,                 // uart queue (ie, no queue)
                          0) != ESP_OK)         // interrupt flags (ie, none)
    strncpy(self->status, "uart_driver_install() failed",
            BUF_LEN_UTHREAD_STATUS) ;
  else
  {
    if (uart_set_pin(UART_NUM_2, tx_pin, rx_pin,
                     UART_PIN_NO_CHANGE,                // RTS pin (ie, unused)
                     UART_PIN_NO_CHANGE) != ESP_OK)     // CTS pin (ie, unused)
      strncpy(self->status, "uart_set_pin() failed", BUF_LEN_UTHREAD_STATUS) ;
    else
      f_serial_io(self, listen_sd) ;            // serial/tcp main loop
  }

  // release resources associated with the UART before releasing the lock

  uart_wait_tx_done(UART_NUM_2, pdMS_TO_TICKS(100)) ; // ie, 100ms
  uart_driver_delete(UART_NUM_2) ;
  xSemaphoreGive(G_runtime->L_uart) ;
  close(listen_sd) ;
  self->state = UTHREAD_STOPPED ;
}

