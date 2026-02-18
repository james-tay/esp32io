/*
   For camera pins , see "Schematic Diagram" / "Camera Connections" in

     https://randomnerdtutorials.com/esp32-cam-ai-thinker-pinout/

   Note
   - built-in LED is on GPIO33 (pin lo = LED on)

   OVERVIEW

   Functions in this file have the following main entrypoints,

     - f_handle_camera() - called by the webserver thread because the "/cam"
       endpoint was called. This function must not block as this interferes
       with the webserver. Although multiple webclients are supported, the
       webserver is single threaded. This function focuses on handing off the
       task to a worker thread.

     - f_cam_cmd() - called by a worker thread when the user sends a "cam..."
       command. It is more acceptable for this function to block, but since
       this is typically configuration tasks, no significant blocking is
       actually expected.

     - f_process_camera() - called by a worker thread when the webserver has
       handed off a "/cam" http request for the worker to process. The worker
       thread services the request (this may take a long time) and hands the
       webclient back to the webserver thread to clean up.

   The information handed off by the webserver to the worker includes,

     - "caller" - which points to the webclient making the request. This may
       be either a "cam..." command or the "/cam" http request.
     - "cmd" - which points to the "cam..." command or the "/cam" http request.
       The worker thread determines the context and calls,
       - f_cam_cmd()
       - f_process_camera()
*/

#define CAM_PIN_PWDN 32                 // set this HIGH to power off camera
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27
#define CAM_PIN_D7 35
#define CAM_PIN_D6 34
#define CAM_PIN_D5 39
#define CAM_PIN_D4 36
#define CAM_PIN_D3 21
#define CAM_PIN_D2 19
#define CAM_PIN_D1 18
#define CAM_PIN_D0 5
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22

#define CAM_XCLK_MIN_MHZ 8              // minimum allowed XCLK frequency (mhz)
#define CAM_XCLK_MAX_MHZ 20             // maximum allowed XCLK frequency (mhz)
#define CAM_DEF_JPEG_QUALITY 10         // 0-63, lower is higher quality

/*
   This function is called from "f_handle_webrequest()" when the webclient at
   "idx" has called the "/cam" endpoint. It is important for this function to
   not block (recall that the webserver thread has other web clients). This
   function focuses on handing the task over to a worker thread.
*/

void f_handle_camera(int idx, char *uri)
{
  int tid = f_get_next_worker() ;
  if (G_runtime->config.debug)
    Serial.printf("DEBUG: f_handle_camera() webclient:%d->worker:%d (%s)\r\n",
                  idx, tid, uri) ;

  G_runtime->webclients[idx].worker = tid ;
  G_runtime->webclients[idx].ts_start = esp_timer_get_time() ;
  G_runtime->worker[tid].caller = idx ;
  G_runtime->worker[tid].cmd = uri ;
  xTaskNotifyGive(G_runtime->worker[tid].w_handle) ;
}

/*
   This function is called from "f_action()" in a worker thread with "idx".
   Our job is to acquire a camera frame and send it to a web client.
*/

void f_process_camera(int idx)
{
  char *s=NULL, line[BUF_LEN_LINE] ;
  int caller = G_runtime->worker[idx].caller ;
  S_WebClient *client = &G_runtime->webclients[caller] ;
  if (G_runtime->config.debug)
    Serial.printf("DEBUG: f_process_camera() worker:%d->webclient:%d (%s)\r\n",
                  idx, caller, G_runtime->worker[idx].cmd) ;

  // if the camera subsystem is not initialized, print and error and stop here

  if (G_runtime->cam_data == NULL)
  {
    strncpy(G_runtime->worker[idx].result_msg, "Camera not initialized.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 500 ;
    return ;
  }

  // capture a frame and make sure it's in JPEG format

  long long ts_start = esp_timer_get_time() ;
  camera_fb_t *fb = esp_camera_fb_get() ;
  if (fb == NULL)
  {
    s = "HTTP/1.1 503 Unavailable\n" ;
    write(client->sd, s, strlen(s)) ;
    s = "Connection: close\n\n" ;
    write(client->sd, s, strlen(s)) ;
    s = "Cannot get camera frame, esp_camera_fb_get() failed.\n" ;
    write(client->sd, s, strlen(s)) ;
    G_runtime->cam_data->frames_bad++ ;
    return ;
  }
  long long ts_end = esp_timer_get_time() ;
  G_runtime->cam_data->last_frame_size = fb->len ;
  G_runtime->cam_data->last_capture_usec = ts_end - ts_start ;

  if (G_runtime->config.debug)
    Serial.printf("DEBUG: f_process_camera() format:%d fb_len:%d\r\n",
                    fb->format, fb->len) ;
  if (fb->format != PIXFORMAT_JPEG)
  {
    s = "HTTP/1.1 503 Unavailable\n" ;
    write(client->sd, s, strlen(s)) ;
    s = "Connection: close\n\n" ;
    write(client->sd, s, strlen(s)) ;
    snprintf(line, BUF_LEN_LINE, "Invalid frame format %d.\n", fb->format) ;
    write(client->sd, line, strlen(line)) ;
  }
  else
  {
    // ready to send "fb->buf" to the webclient, start with HTTP header

    s = "HTTP/1.0 200 OK\n" ; write(client->sd, s, strlen(s)) ;
    s = "Accept-Ranges: bytes\n" ; write(client->sd, s, strlen(s)) ;
    s = "Cache-Control: no-cache\n" ; write(client->sd, s, strlen(s)) ;
    s = "Content-Type: image/jpeg\n" ; write(client->sd, s, strlen(s)) ;

    snprintf(line, BUF_LEN_LINE, "Content-Length: %d\n\n", fb->len) ;
    write(client->sd, line, strlen(line)) ;
    G_runtime->cam_data->frames_ok++ ;

    int written=0, remainder, amt ;
    ts_start = esp_timer_get_time() ;
    while (written != fb->len)
    {
      remainder = fb->len - written ;
      amt = write(client->sd, fb->buf + written, remainder) ;
      if (amt < 1)
        break ;
      else
        written = written + amt ;
      client->ts_last_activity = esp_timer_get_time() ; // idle timestamp
    }
    if (written != fb->len)
      G_runtime->cam_data->bad_xmits++ ;
    else
    {
      ts_end = esp_timer_get_time() ;
      G_runtime->cam_data->last_xmit_msec = (ts_end - ts_start) / 1000 ;
    }
  }
  esp_camera_fb_return(fb) ;
}

/*
   This function is called from "f_cam_cmd()". Our role is initalize the
   camera and write the appropriate result in "result_msg" and "result_code"
   of worker thread "idx".
*/

void f_cam_init(int idx, char *user_mhz)
{
  if (G_runtime->cam_data != NULL)                      // already initialized
  {
    strncpy(G_runtime->worker[idx].result_msg,
            "Camera already initialized.\r\n", BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }

  if (psramFound() == false)                            // must have PSRAM
  {
    strncpy(G_runtime->worker[idx].result_msg,
            "No PSRAM found..\r\n", BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }

  // malloc() the necessary data structures and initialize it

  S_CamData *data = (S_CamData*) malloc(sizeof(S_CamData)) ;
  if (data == NULL)
  {
    strncpy(G_runtime->worker[idx].result_msg, 
            "malloc() failed for S_CamData.\r\n", BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 500 ;
    return ;
  }
  memset(data, 0, sizeof(S_CamData)) ;

  int xclk_mhz = atoi(user_mhz) ;                       // user specified clock
  if (xclk_mhz < CAM_XCLK_MIN_MHZ) xclk_mhz = CAM_XCLK_MIN_MHZ ;
  if (xclk_mhz > CAM_XCLK_MAX_MHZ) xclk_mhz = CAM_XCLK_MAX_MHZ ;

  data->cam_setup.pin_pwdn = CAM_PIN_PWDN ;
  data->cam_setup.pin_reset = CAM_PIN_RESET ;
  data->cam_setup.pin_xclk = CAM_PIN_XCLK ;
  data->cam_setup.pin_d7 = CAM_PIN_D7 ;
  data->cam_setup.pin_d6 = CAM_PIN_D6 ;
  data->cam_setup.pin_d5 = CAM_PIN_D5 ;
  data->cam_setup.pin_d4 = CAM_PIN_D4 ;
  data->cam_setup.pin_d3 = CAM_PIN_D3 ;
  data->cam_setup.pin_d2 = CAM_PIN_D2 ;
  data->cam_setup.pin_d1 = CAM_PIN_D1 ;
  data->cam_setup.pin_d0 = CAM_PIN_D0 ;
  data->cam_setup.pin_vsync = CAM_PIN_VSYNC ;
  data->cam_setup.pin_href = CAM_PIN_HREF ;
  data->cam_setup.pin_sscb_sda = CAM_PIN_SIOD ;
  data->cam_setup.pin_sscb_scl = CAM_PIN_SIOC ;
  data->cam_setup.pin_pclk = CAM_PIN_PCLK ;
  data->cam_setup.xclk_freq_hz = xclk_mhz * 1000000 ;
  data->cam_setup.ledc_timer = LEDC_TIMER_0 ;
  data->cam_setup.ledc_channel = LEDC_CHANNEL_0 ;
  data->cam_setup.pixel_format = PIXFORMAT_JPEG ;
  data->cam_setup.grab_mode = CAMERA_GRAB_LATEST ;
  data->cam_setup.fb_location = CAMERA_FB_IN_PSRAM ;
  data->cam_setup.frame_size = FRAMESIZE_UXGA ;
  data->cam_setup.jpeg_quality = CAM_DEF_JPEG_QUALITY ;
  data->cam_setup.fb_count = 2 ;                // double buffering in PSRAM

  // at this point, try to initialize camera ... fingers crossed

  esp_err_t err = esp_camera_init(&data->cam_setup) ;
  if (err)
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "esp_camera_init() failed 0x%x.\r\n", err) ;
    G_runtime->worker[idx].result_code = 500 ;
    free(data) ;
    return ;                                            // we failed miserably
  }

  snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
           "Camera initialized at %dmhz, psram %d/%d bytes.\r\n",
           xclk_mhz, ESP.getFreePsram(), ESP.getPsramSize()) ;
  data->xclk_mhz = xclk_mhz ;
  G_runtime->worker[idx].result_code = 200 ;
  G_runtime->cam_data = data ;
}

/*
   This function is called from "f_action()" when the user send a "cam ..."
   command. Recall that we are running under worker thread "idx" at this point.
*/

void f_cam_cmd(int idx)
{
  // parse our "cam..." command, or print help

  char *tokens[5], *cmd=NULL, *action=NULL, *v1=NULL, *v2=NULL, *v3=NULL ;
  int count = f_parse(G_runtime->worker[idx].cmd, tokens, 5) ;
  if (count < 2)
  {
    strncpy(G_runtime->worker[idx].result_msg,
      "init <mhz>                       XCLK frequency (8-20)\r\n"
      "set <key> <value>                set camera parameter\r\n"
      "reg <addr> <mask> <value>        set camera register\r\n",
      BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }
  action = tokens[1] ;                  // eg, "cam init"
  if (count > 2) v1 = tokens[2] ;       // eg, "cam set framesize ..."
  if (count > 3) v2 = tokens[3] ;       // eg, "cam set framesize sxga"
  if (count > 4) v3 = tokens[4] ;       // eg, "cam reg 17 255 1"

  if ((strcmp(action, "init") == 0) && (v1 != NULL))
  {
    f_cam_init(idx, v1) ;
  }
  else
  {
    strncpy(G_runtime->worker[idx].result_msg, "Invalid command.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
  }
}

