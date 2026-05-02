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

   NOTES

   - To "reboot" the camera, toggle pin 32 hi and the lo, eg
       % curl http://esp32-cam/v1?cmd=hi+32
       % curl http://esp32-cam/v1?cmd=lo+32

   - To setup for long exposures, switch to register bank 1, and then set
     the pixel clock divider value, eg
       % curl http://esp32-cam/v1?cmd=cam+reg_set+255+255+1
       % curl http://esp32-cam/v1?cmd=cam+reg_set+17+255+8

   - To query camera model, select sensor bank registers then query for the
     PID and VER registers, eg
       % curl http://esp32-cam/v1?cmd=cam+reg_set+255+255+1
       % curl http://esp32-cam/v1?cmd=cam+reg_get+10+255        # PID
       % curl http://esp32-cam/v1?cmd=cam+reg_get+11+255        # VER
     thus,
       OV2640 - PID=0x26, VER=0x42

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
   This is a convenience function called from "f_cam_set()". Our job is to
   return the framesize ID number for "value" (eg, "sxga").
*/

framesize_t f_framesize_id(char *value)
{
  if (strcmp(value, "qqvga") == 0) return (FRAMESIZE_QQVGA) ;
  if (strcmp(value, "qcif") == 0)  return (FRAMESIZE_QCIF) ;
  if (strcmp(value, "hqvga") == 0) return (FRAMESIZE_HQVGA) ;
  if (strcmp(value, "qvga") == 0)  return (FRAMESIZE_QVGA) ;
  if (strcmp(value, "cif") == 0)   return (FRAMESIZE_CIF) ;
  if (strcmp(value, "hvga") == 0)  return (FRAMESIZE_HVGA) ;
  if (strcmp(value, "vga") == 0)   return (FRAMESIZE_VGA) ;
  if (strcmp(value, "svga") == 0)  return (FRAMESIZE_SVGA) ;
  if (strcmp(value, "xga") == 0)   return (FRAMESIZE_XGA) ;
  if (strcmp(value, "hd") == 0)    return (FRAMESIZE_HD) ;
  if (strcmp(value, "sxga") == 0)  return (FRAMESIZE_SXGA) ;
  if (strcmp(value, "uxga") == 0)  return (FRAMESIZE_UXGA) ;
  return(FRAMESIZE_SXGA) ; // default framesize
}

/*
   This function is called from "f_cam_cmd()". Our job is to set a camera
   parameter "key" to "value". Most of these are numeric, but some may be
   symbolic (eg, "framesize" with a value "sxga").
*/

void f_cam_set(int idx, char *key, char *value)
{
  sensor_t *s = esp_camera_sensor_get() ;
  if (s == NULL)
  {
    strncpy(G_runtime->worker[idx].result_msg,
            "Failed to get camera sensor\r\n", BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 500 ;
    return ;
  }

  if (strcmp(key, "framesize") == 0)                    // Image and Frame
    s->set_framesize(s, f_framesize_id(value)) ;
  else
  if (strcmp(key, "quality") == 0)
    s->set_quality(s, atoi(value)) ;
  else
  if (strcmp(key, "brightness") == 0)                   // Visual Adjustments
    s->set_brightness(s, atoi(value)) ;
  else
  if (strcmp(key, "contrast") == 0)
    s->set_contrast(s, atoi(value)) ;
  else
  if (strcmp(key, "saturation") == 0)
    s->set_saturation(s, atoi(value)) ;
  else
  if (strcmp(key, "denoise") == 0)
    s->set_denoise(s, atoi(value)) ;
  else
  if (strcmp(key, "special_effect") == 0)
    s->set_special_effect(s, atoi(value)) ;
  else
  if (strcmp(key, "hmirror") == 0)                      // Orientation and Misc
    s->set_hmirror(s, atoi(value)) ;
  else
  if (strcmp(key, "vflip") == 0)
    s->set_vflip(s, atoi(value)) ;
  else
  if (strcmp(key, "colorbar") == 0)
    s->set_colorbar(s, atoi(value)) ;
  else
  if (strcmp(key, "dcw") == 0)
    s->set_dcw(s, atoi(value)) ;
  else
  if (strcmp(key, "lenc") == 0)
    s->set_lenc(s, atoi(value)) ;
  else
  if (strcmp(key, "raw_gma") == 0)
    s->set_raw_gma(s, atoi(value)) ;
  else
  if (strcmp(key, "wb_mode") == 0)
    s->set_wb_mode(s, atoi(value)) ;
  else
  if (strcmp(key, "awb_gain") == 0)                     // Automatic Controls
    s->set_awb_gain(s, atoi(value)) ;
  else
  if (strcmp(key, "aec2") == 0)
    s->set_aec2(s, atoi(value)) ;
  else
  if (strcmp(key, "ae_level") == 0)
    s->set_ae_level(s, atoi(value)) ;
  else
  if (strcmp(key, "agc_gain") == 0)
    s->set_agc_gain(s, atoi(value)) ;
  else
  if (strcmp(key, "bpc") == 0)
    s->set_bpc(s, atoi(value)) ;
  else
  if (strcmp(key, "wpc") == 0)
    s->set_wpc(s, atoi(value)) ;
  else
  {
    strncpy(G_runtime->worker[idx].result_msg, "Unsupported parameter.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }

  // print some feedback to the user

  if (strcmp(key, "framesize") == 0)
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "Setting %s->%d.\r\n", key, f_framesize_id(value)) ;
  else
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "Setting %s->%d.\r\n", key, atoi(value)) ;
  G_runtime->worker[idx].result_code = 200 ;
}

/*
   This is a convenience function called from "f_cam_show()". Our job is to
   convert "size" into a framesize string (eg, "1280x1024 sxga"), and return
   this as a static string.
*/

char *f_framesize_str(framesize_t size)
{
  switch (size)
  {
    case FRAMESIZE_QQVGA: return "160x120-qqvga" ;
    case FRAMESIZE_QCIF:  return "176x144-qcif" ;
    case FRAMESIZE_HQVGA: return "240x176-hqvga" ;
    case FRAMESIZE_QVGA:  return "320x240-qvga" ;
    case FRAMESIZE_CIF:   return "400x296-cif" ;
    case FRAMESIZE_HVGA:  return "480x320-hvga" ;
    case FRAMESIZE_VGA:   return "640x480-vga" ;
    case FRAMESIZE_SVGA:  return "800x600-svga" ;
    case FRAMESIZE_XGA:   return "1024x768-xga" ;
    case FRAMESIZE_HD:    return "1280x720-hd" ;
    case FRAMESIZE_SXGA:  return "1280x1024-sxga" ;
    case FRAMESIZE_UXGA:  return "1600x1200-uxga" ;
    default:              return "unknown" ;
  }
}

/*
   This function is called from "f_cam_cmd()". Our role is to obtain the
   "sensor_t" object, from which we'll be able to examine all the various
   camera parameters.
*/

void f_cam_show(int idx)
{
  sensor_t *s = esp_camera_sensor_get() ;
  if (s == NULL)
  {
    strncpy(G_runtime->worker[idx].result_msg,
            "Failed to get camera sensor\r\n", BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 500 ;
    return ;
  }

  snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
           "Image and Frame\r\n"
           " framesize            %s\r\n"
           " scale(0|1)           %d(ro)\r\n"
           " binning(0|1)         %d(ro)\r\n"
           " quality(0->63)       %d\r\n"
           "Visual Adjustments\r\n"
           " brightness(-2->2)    %d\r\n"
           " contrast(-2->2)      %d\r\n"
           " saturation(-2->2)    %d\r\n"
           " denoise(0|1->255)    %d\r\n"
           " special_effect(0->6) %d\r\n"
           "Orientation and Misc\r\n"
           " hmirror(0|1)         %d\r\n"
           " vflip(0|1)           %d\r\n"
           " colorbar(0|1)        %d\r\n"
           " dcw(0|1)             %d\r\n"
           " lenc(0|1)            %d\r\n"
           " raw_gma(0|1)         %d\r\n"
           " wb_mode(0->4)        %d\r\n"
           "Automatic Controls\r\n"
           " awb(0|1)             %d(ro)\r\n"
           " awb_gain(0|1)        %d\r\n"
           " aec(0|1)             %d(ro)\r\n"
           " aec2(0|1)            %d\r\n"
           " ae_level(-2->2)      %d\r\n"
           " agc(0|1)             %d(ro)\r\n"
           " agc_gain(0->30)      %d\r\n"
           " gainceiling(0->6)    %d(ro)\r\n"
           " bpc(0|1)             %d\r\n"
           " wpc(0|1)             %d\r\n",
           f_framesize_str(s->status.framesize),        // Image and Frame
           s->status.scale,
           s->status.binning,
           s->status.quality,
           s->status.brightness,                        // Visual Adjustments
           s->status.contrast,
           s->status.saturation,
           s->status.denoise,
           s->status.special_effect,
           s->status.hmirror,                           // Orientation and Misc
           s->status.vflip,
           s->status.colorbar,
           s->status.dcw,
           s->status.lenc,
           s->status.raw_gma,
           s->status.wb_mode,
           s->status.awb,                               // Automatic Controls
           s->status.awb_gain,
           s->status.aec,
           s->status.aec2,
           s->status.ae_level,
           s->status.agc,
           s->status.agc_gain,
           s->status.gainceiling,
           s->status.bpc,
           s->status.wpc) ;
  G_runtime->worker[idx].result_code = 200 ;
}

/*
   This function is called from "f_cam_cmd()". Our job is to read the contents
   of a register at "addr" but filter the result through "mask".
*/

void f_cam_reg_get(int idx, char *addr_str, char *mask_str)
{
  int addr = atoi(addr_str) ;
  int mask = atoi(mask_str) ;

  sensor_t *s = esp_camera_sensor_get() ;
  if (s == NULL)
  {
    strncpy(G_runtime->worker[idx].result_msg,
            "Failed to get camera sensor\r\n", BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 500 ;
    return ;
  }

  unsigned char result = s->get_reg(s, addr, mask) ;
  snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
           "get_reg(0x%02x,0x%02x) returned 0x%02x.\r\n", addr, mask, result) ;
  G_runtime->worker[idx].result_code = 200 ;
}

/*
   This function is called from "f_cam_cmd()". Our job is to update a register
   at "addr" by appling "mask" on "value". The "mask" allows us to ensure only
   certain bits are manipulated (a mask of 255 means we'll update all bits).
*/

void f_cam_reg_set(int idx, char *addr_str, char *mask_str, char *value_str)
{
  int addr = atoi(addr_str) ;
  int mask = atoi(mask_str) ;
  int value = atoi(value_str) ;

  sensor_t *s = esp_camera_sensor_get() ;
  if (s == NULL)
  {
    strncpy(G_runtime->worker[idx].result_msg,
            "Failed to get camera sensor\r\n", BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 500 ;
    return ;
  }

  int result = s->set_reg(s, addr, mask, value) ;
  snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
           "set_reg(0x%02x,0x%02x,0x%02x) returned %d.\r\n",
           addr, mask, value, result) ;
  if (result == 0)
    G_runtime->worker[idx].result_code = 200 ;
  else
    G_runtime->worker[idx].result_code = 500 ;
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
      "cam init <mhz>                       XCLK frequency (8-20)\r\n"
      "cam set <key> <value>                set camera parameter\r\n"
      "cam show                             show camera parameters\r\n"
      "cam reg_get <addr> <mask>            get a camera register\r\n"
      "cam reg_set <addr> <mask> <value>    set a camera register\r\n",
      BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }
  action = tokens[1] ;                  // eg, "cam init"
  if (count > 2) v1 = tokens[2] ;       // eg, "cam set framesize ..."
  if (count > 3) v2 = tokens[3] ;       // eg, "cam set framesize sxga"
  if (count > 4) v3 = tokens[4] ;       // eg, "cam reg 17 255 1"

  if ((strcmp(action, "init") == 0) && (v1))
    f_cam_init(idx, v1) ;
  else
  if ((strcmp(action, "set") == 0) && (v1) && (v2))
    f_cam_set(idx, v1, v2) ;
  else
  if (strcmp(action, "show") == 0)
    f_cam_show(idx) ;
  else
  if ((strcmp(action, "reg_get") == 0) && (v1) && (v2))
    f_cam_reg_get(idx, v1, v2) ;
  else
  if ((strcmp(action, "reg_set") == 0) && (v1) && (v2) && (v3))
    f_cam_reg_set(idx, v1, v2, v3) ;
  else
  {
    strncpy(G_runtime->worker[idx].result_msg, "Invalid command.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
  }
}

