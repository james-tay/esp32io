void ft_wd(S_UserThread *self)
{
  if (self->loop == 0)
    self->state = UTHREAD_RUNNING ;

  snprintf(self->status, BUF_LEN_UTHREAD_STATUS,
           "watchdog thread in loop %lld.", self->loop) ;
  delay (1000) ;
}
