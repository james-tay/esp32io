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
   high enough to match the relay module's 5v supply. To workaround this, place
   an LED with its anode connected to the GPIO pin.

   When the GPIO pin is high, the voltage across the LED is insufficient for a
   forward bias, thus the LED is off and effectively has a massive resistance,
   this deactivates the relay. When the GPIO pin is low, then current flows
   through the LED, the voltage at the anode is now low(er) at about 1.8v,
   which is low enough to activate the relay.
*/

void ft_relay(S_UserThread *self)
{
  #define RELAY_DELAY_MSEC 100                  // how often we check status

  static thread_local int pin, timeout_secs ;

  if (self->loop == 0)
  {
    if (self->num_args != 2)
    {
      strncpy(self->status, "Incorrect arguments", BUF_LEN_UTHREAD_STATUS) ;
      self->state = UTHREAD_STOPPED ;
      return ;
    }

    pin = atoi(self->in_args[0]) ;              // GPIO pin to control relay
    timeout_secs = atoi(self->in_args[1]) ;     // time out to deactivate relay
    self->state = UTHREAD_RUNNING ;
  }




  delay(RELAY_DELAY_MSEC) ;
}
