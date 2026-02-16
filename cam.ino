/*
   camera pin configuration, see reference,
   https://github.com/raphaelbs/esp32-cam-ai-thinker/blob/master/docs/esp32cam-pin-notes.md
*/

#define CAM_PIN_PWDN 32
#define CAM_PIN_RESET -1 //software reset will be performed
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
   not block (recall that the webserver thread has other work to do). We are
   also fully responsible for handling this webclient's "sd" (ie, we are
   responsible for calling "f_close_webclient()" when it's time.
*/

void f_handle_camera(int idx)
{


  f_close_webclient(idx) ;
}

/*
   This function is called from "f_cam_cmd()". Our role is initalize the
   camera and write the appropriate result in "result_msg" and "result_code"
   of worker thread "idx".
*/

void f_cam_init(int idx, char *user_mhz)
{
  if (G_runtime->cam_setup != NULL)                     // already initialized
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

  camera_config_t *setup = (camera_config_t*) malloc(sizeof(camera_config_t)) ;
  if (setup == NULL)
  {
    strncpy(G_runtime->worker[idx].result_msg, 
            "malloc() failed for camera_config_t.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 500 ;
    return ;
  }

  int xclk_mhz = atoi(user_mhz) ;
  if (xclk_mhz < CAM_XCLK_MIN_MHZ) xclk_mhz = CAM_XCLK_MIN_MHZ ;
  if (xclk_mhz > CAM_XCLK_MAX_MHZ) xclk_mhz = CAM_XCLK_MAX_MHZ ;

  memset(setup, 0, sizeof(camera_config_t)) ;
  setup->pin_pwdn = CAM_PIN_PWDN ;
  setup->pin_reset = CAM_PIN_RESET ;
  setup->pin_xclk = CAM_PIN_XCLK ;
  setup->pin_d7 = CAM_PIN_D7 ;
  setup->pin_d6 = CAM_PIN_D6 ;
  setup->pin_d5 = CAM_PIN_D5 ;
  setup->pin_d4 = CAM_PIN_D4 ;
  setup->pin_d3 = CAM_PIN_D3 ;
  setup->pin_d2 = CAM_PIN_D2 ;
  setup->pin_d1 = CAM_PIN_D1 ;
  setup->pin_d0 = CAM_PIN_D0 ;
  setup->pin_vsync = CAM_PIN_VSYNC ;
  setup->pin_href = CAM_PIN_HREF ;
  setup->pin_sscb_sda = CAM_PIN_SIOD ;
  setup->pin_sscb_scl = CAM_PIN_SIOC ;
  setup->pin_pclk = CAM_PIN_PCLK ;
  setup->xclk_freq_hz = xclk_mhz * 1000000 ;
  setup->ledc_timer = LEDC_TIMER_0 ;
  setup->ledc_channel = LEDC_CHANNEL_0 ;
  setup->pixel_format = PIXFORMAT_JPEG ;
  setup->grab_mode = CAMERA_GRAB_LATEST ;
  setup->fb_location = CAMERA_FB_IN_PSRAM ;
  setup->frame_size = FRAMESIZE_UXGA ;
  setup->jpeg_quality = CAM_DEF_JPEG_QUALITY ;
  setup->fb_count = 2 ;

  // at this point, try to initialize camera ... fingers crossed

  esp_err_t err = esp_camera_init(setup) ;
  if (err)
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "esp_camera_init() failed 0x%x.\r\n", err) ;
    G_runtime->worker[idx].result_code = 500 ;
    free(setup) ;
    return ;                                            // we failed miserably
  }

  snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
           "Camera initialized at %d mhz, psram free:%d size:%d bytes.\r\n",
           xclk_mhz, ESP.getFreePsram(), ESP.getPsramSize()) ;
  G_runtime->worker[idx].result_code = 200 ;
  G_runtime->cam_setup = setup ;
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

