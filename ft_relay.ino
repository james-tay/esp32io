/*
   This thread implements safe relay control. This thread controls the GPIO
   pin which controls the relay and must be running before calling the "relay"
   command to turn it on/off. Specifically, the desired relay state is
   indicated by this thread's "self->result[0].i_value". Thus the "relay
   command directly manipulates this "i_value", and this thread performs the
   necessary actions.

   This code has been tested on active low relay modules (without optocoupler).
   During typical operation, the relay module's "IN" pin is naturally high and
   needs to be pulled low to activate the relay. To deactivate the relay, the
   "IN" pin must go high. However, the 3.3v output from an ESP32 might not be
   high enough to match the relay module's 5v supply. Even if the "IN" pin is
   configured as an input pin, the voltage rises up to 3.8v, which is not high
   enough to deactivate the relay. To workaround this, place an LED with its
   anode connected to the GPIO pin.

   When the GPIO pin is high (ie, 3.2v), the voltage across the LED is 1.2v,
   and is insufficient for a forward bias, thus the LED is off and effectively
   has a massive resistance and its cathode rises to 4.4v, this deactivates the
   relay.

   When the GPIO pin is low, then current flows through the LED, the voltage
   at the cathode is now low, at about 1.7v, which is low enough to activate
   the relay.

   When the GPIO pin is configured as an input, its impedence goes high, and
   its voltage is 3v. But on the LED's cathode, it is 4.4v, which deactivates
   the relay.
*/

struct td_relay {
  int pin ;             // GPIO pin which controls the relay
  int timeout_secs ;    // turn off if not (re)commanded on within this time
} ;
typedef struct td_relay S_td_relay ;

void ft_relay(S_UserThread *self)
{
  #define RELAY_DELAY_MSEC 100                  // how often we check status

  S_td_relay *td=NULL ;

  if (self->loop == 0)
  {
    if (self->num_args != 2)
    {
      strncpy(self->status, "Incorrect arguments", BUF_LEN_UTHREAD_STATUS) ;
      self->state = UTHREAD_STOPPED ;
      return ;
    }

    self->malloc_buf = malloc(sizeof(S_td_relay)) ;
    if (self->malloc_buf == NULL)
    {
      strncpy(self->status, "malloc failed", BUF_LEN_UTHREAD_STATUS) ;
      self->state = UTHREAD_STOPPED ;
      return ;
    }
    memset(self->malloc_buf, 0, sizeof(S_td_relay)) ;
    td = (S_td_relay*) self->malloc_buf ;

    td->pin = atoi(self->in_args[0]) ;          // GPIO pin to control relay
    td->timeout_secs = atoi(self->in_args[1]) ; // time out to deactivate relay
    self->state = UTHREAD_RUNNING ;
  }




  delay(RELAY_DELAY_MSEC) ;
}
