void f_worker_thread(void *param)
{
  int myidx = *((int*)param) ;

  Serial.printf("BOOT: %s thread started.\r\n",
                G_runtime->worker[myidx].name) ;

  while (1)
  {
    // sit here and wait until somebody tells us to do work

    ulTaskNotifyTake(pdTRUE, portMAX_DELAY) ;
    G_runtime->worker[myidx].state = W_BUSY ;

    // put in dummy response to indicate we did work.

    snprintf(G_runtime->worker[myidx].result_msg, BUF_LEN_WORKER_RESULT,
             "hello world from %s.", G_runtime->worker[myidx].name) ;
    G_runtime->worker[myidx].result_code = 200 ;
    G_runtime->worker[myidx].state = W_DONE ;

    // notify the caller that we're done

    if (G_runtime->worker[myidx].caller < 0)    // serial console thread
    {
      xTaskNotifyGive(G_runtime->sconsole_handle) ;
    }
  }
}
