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
   up the serial port with,
     Serial2.begin(...)
   and when it's time to terminate, this thread calls,
     Serial2.end()
   and finally releases "L_uart". At this point, this function returns.
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

  // acquire UART lock

  if (xSemaphoreTake(G_runtime->L_uart, 0) != pdTRUE)
  {
    close(listen_sd) ;
    strncpy(self->status, "Cannot lock UART", BUF_LEN_UTHREAD_STATUS) ;
    self->state = UTHREAD_STOPPED ;
    return ;
  }




  // in our main loop, make sure we never block for more than half of
  // DEF_MAX_THREAD_WRAPUP_MSEC, so that we can perform an orderly exit




  xSemaphoreGive(G_runtime->L_uart) ;
  close(listen_sd) ;
}
