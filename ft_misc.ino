/*
   This is a watchdog function. Our job is to detect that something might have
   gone wrong and trigger a reload. The primary means of determining this is
   lack of response on both serial console and webserver traffic.
*/

void ft_wd(S_UserThread *self)
{
  if (self->loop == 0)
    self->state = UTHREAD_RUNNING ;

  snprintf(self->status, BUF_LEN_UTHREAD_STATUS,
           "watchdog thread in loop %lld.", self->loop) ;
  delay (1000) ;
}
