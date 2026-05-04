/*
   This is a convenience function which reads from "filename", writing up to
   "max_size" bytes into "buf". On success the number of bytes is returned,
   otherwise -1 to indicate something went wrong (probably no such file).
*/

int f_read_single_line(char *filename, char *buf, int max_size)
{
  File f = SPIFFS.open(filename, "r") ;
  if (f.size() < 1)
    return(-1) ;                                // file is probably absent

  int amt = f.readBytesUntil('\n', buf, max_size-1) ;
  if (amt > 0)
    buf[amt] = 0 ;

  f.close() ;
  return(amt) ;
}

/*
   This is a convenience function which reads from "filename", writing the
   entire contents of the file into "buf", but at most "max_size" bytes. The
   total number of bytes placed into "buf" is returned, or "-1" if something
   went wrong (probably no such file).
*/

int f_read_whole(char *filename, char *buf, int max_size)
{
  int amt=0 ;
  File f = SPIFFS.open(filename, "r") ;
  if (f.size() < 1)
    return(-1) ;                                // file is probably absent

  amt = f.readBytes(buf, max_size-1) ;
  if (amt > 0)
    buf[amt] = 0 ;

  f.close() ;
  return(amt) ;
}

/*
   This function is called from "f_fs_cmd()". Our job is to print out as much
   info as we can on our flash storage.
*/

void f_fs_info(int idx)
{
  unsigned long flash_size=0 ;
  esp_flash_get_physical_size(NULL, &flash_size) ;
  snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
           "partitionBytes: %d\r\nusedBytes: %d\r\nphysicalBytes: %ld\r\n",
           SPIFFS.totalBytes(), SPIFFS.usedBytes(), flash_size) ;
  G_runtime->worker[idx].result_code = 200 ;
}

/*
   This function is called from "f_fs_cmd()". We list the files in the SPIFFS.
   Recall that this filesystem does not support directories.
*/

void f_fs_ls(int idx)
{
  char line[BUF_LEN_LINE] ;

  File root = SPIFFS.open ("/", "r") ;
  File f = root.openNextFile () ;
  while (f)
  {
    snprintf (line, BUF_LEN_LINE, "%-8d /%s\r\n", f.size(), f.name()) ;
    strncat(G_runtime->worker[idx].result_msg, line,
            BUF_LEN_WORKER_RESULT -
            strlen(G_runtime->worker[idx].result_msg) - 1) ;
    f = root.openNextFile () ;
  }
  root.close () ;
  G_runtime->worker[idx].result_code = 200 ;
}

/*
   This function is called from "f_fs_cmd()". Our job is to try renaming "src"
   to "dst".
*/

void f_fs_mv(int idx, char *src, char *dst)
{
  if (SPIFFS.rename(src, dst))
  {
    snprintf (G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
              "Moved %s->%s.\r\n", src, dst) ;
    G_runtime->worker[idx].result_code = 200 ;
  }
  else
  {
    snprintf (G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
              "Cannot move %s->%s.\r\n", src, dst) ;
    G_runtime->worker[idx].result_code = 500 ;
  }
}

/*
   This function is called from "f_fs_cmd()" when our job is to print out the
   partition table. We're supplied the "idx" of our worker thread, thus our
   responsibility is to write into its "result_msg" and "result_code".
*/

void f_fs_partinfo(int idx)
{
  char line[BUF_LEN_LINE] ;

  esp_partition_iterator_t p_iter = esp_partition_find(
                                      ESP_PARTITION_TYPE_ANY,
                                      ESP_PARTITION_SUBTYPE_ANY,
                                      NULL) ;
  while (p_iter != NULL)
  {
    const esp_partition_t *p = esp_partition_get(p_iter) ;
    snprintf(line, BUF_LEN_LINE,
             "label:%-9s type:%d subtype:%-3d addr:0x%06x size:%lu KB\r\n",
             p->label, p->type, p->subtype,
             (unsigned long) p->address,
             (unsigned long) p->size / 1024) ;
    strncat(G_runtime->worker[idx].result_msg, line,
            BUF_LEN_WORKER_RESULT -
            strlen(G_runtime->worker[idx].result_msg) - 1) ;
    p_iter = esp_partition_next(p_iter) ;
  }
  esp_partition_iterator_release(p_iter) ;
  G_runtime->worker[idx].result_code = 200 ;
}

/*
   This function is called from "f_fs_cmd()". It reads a (potentially)
   multi-line file, placing its contents in our worker thread's "result_msg".
*/

void f_fs_read(int idx, char *filename)
{
  if ((filename == NULL) || (strlen(filename) < 1))
  {
    strncpy(G_runtime->worker[idx].result_msg, "No filename specified.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 500 ;
    return ;
  }

  File f = SPIFFS.open(filename, "r") ;
  if (f.size() < 1)
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "Cannot read file '%s'.\r\n", filename) ;
    G_runtime->worker[idx].result_code = 500 ;
    return ;
  }

  int total = 0 ;
  int remainder = BUF_LEN_WORKER_RESULT - 3 ; // leave space for '\r\n'
  while ((total < f.size()) && (remainder > 0))
  {
    int amt = f.readBytes(G_runtime->worker[idx].result_msg + total,
                          remainder) ;
    if (amt > 0)
    {
      total = total + amt ;
      remainder = remainder - amt ;
      G_runtime->worker[idx].result_msg[total] = 0 ;
    }
    else
      break ;
  }
  f.close() ;
  if (G_runtime->worker[idx].result_msg[total-1] != '\n')
    strcat(G_runtime->worker[idx].result_msg, "\r\n") ;

  G_runtime->worker[idx].result_code = 200 ;
}

/*
   This function is called from "f_fs_recv()" or "f_fs_send()". Our job is to
   setup a listening TCP socket on "port". On success, the socket descriptor
   is returned. If something went wrong, we'll write the error into the
   "idx" worker thread's "result_msg" and "result_code", and return -1.
*/

int f_fs_listen_socket(int idx, int port)
{
  int listen_sd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP) ;
  if (listen_sd < 0)
  {
    strncpy(G_runtime->worker[idx].result_msg, "socket() failed.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 500 ;
    return(-1) ;
  }

  int reuse = 1 ; // allow quick re-bind()'ing of this port
  setsockopt(listen_sd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) ;

  struct sockaddr_in addr ;
  memset(&addr, 0, sizeof(addr)) ;
  addr.sin_family = AF_INET ;
  addr.sin_addr.s_addr = INADDR_ANY ;
  addr.sin_port = htons(port) ;
  if (bind(listen_sd, (const struct sockaddr*) &addr, sizeof(addr)) < 0)
  {
    close(listen_sd) ;
    strncpy(G_runtime->worker[idx].result_msg, "bind() failed.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 500 ;
    return(-1) ;
  }
  if (listen (listen_sd, 1) != 0)
  {
    close(listen_sd) ;
    strncpy(G_runtime->worker[idx].result_msg, "listen() failed.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 500 ;
    return(-1) ;
  }
  return(listen_sd) ;
}

/*
   This function is called from "f_fs_recv()" or "f_fs_send()". Our job is to
   wait for up to DEF_FS_XFER_TIMEOUT_SECS, for an incoming TCP client on
   "listen_sd". On success, we'll return the client's socket descriptor, or
   -1 if something went wrong (probably a timeout).
*/

int f_fs_wait_for_client(int idx, int listen_sd)
{
  struct timeval tv ;
  fd_set rfds ;
  tv.tv_sec = DEF_FS_XFER_TIMEOUT_SECS ;
  tv.tv_usec = 0 ;
  FD_ZERO(&rfds) ;
  FD_SET(listen_sd, &rfds) ;
  if (select(listen_sd + 1, &rfds, NULL, NULL, &tv) < 1)
  {
    strncpy(G_runtime->worker[idx].result_msg,
            "Timed out waiting for client.\r\n", BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 500 ;
    return(-1) ;
  }

  // if we got here, it means there was activity on "listen_sd"

  return(accept(listen_sd, NULL, NULL)) ;
}

/*
   This function is called from "f_fs_cmd()" with a worker thread with "idx".
   Our job is to listen on the TCP "port" for an incoming client. Any data
   received is written to "filename".
*/

void f_fs_recv(int idx, char *port_str, char *filename)
{
  // sanity check out inputs first

  int fault=0, port ;
  if (port_str == NULL)
    fault = 1 ;
  else
    port = atoi(port_str) ;

  if ((fault) || (port < 1) || (port > 65535) || (filename == NULL) ||
      (filename[0] != '/') || (strlen(filename) > DEF_MAX_FILENAME_LEN))
  {
    strncpy(G_runtime->worker[idx].result_msg, "Invalid usage.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }

  // setup a listening TCP socket and expect a client to connect to it

  int listen_sd = f_fs_listen_socket(idx, port) ;
  if (listen_sd < 0)
    return ;

  // wait for a client to connect, wait up to DEF_FS_XFER_TIMEOUT_SECS

  int client_sd = f_fs_wait_for_client(idx, listen_sd) ;
  close(listen_sd) ;
  if (client_sd < 0)
    return ;

  // open a temporary file for writing, if anything goes wrong, discard it

  char tmp_file[DEF_MAX_FILENAME_LEN] ;
  snprintf(tmp_file, DEF_MAX_FILENAME_LEN, "%s~", filename) ;
  File f = SPIFFS.open(tmp_file, "w") ;
  if (!f)
  {
    strncpy(G_runtime->worker[idx].result_msg,
            "Cannot open file for writing.\r\n", BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 500 ;
    close(client_sd) ;
    return ;
  }

  // read data from "client_sd", but disconnect if it idles too long

  int total_bytes=0, amt ;
  char buf[BUF_LEN_LINE] ;
  fd_set rfds ;
  struct timeval tv ;

  while (1)
  {
    tv.tv_sec = DEF_FS_XFER_TIMEOUT_SECS ;
    tv.tv_usec = 0 ;
    FD_ZERO(&rfds) ;
    FD_SET(client_sd, &rfds) ;
    if (select(client_sd + 1, &rfds, NULL, NULL, &tv) < 1)
    {
      strncpy(G_runtime->worker[idx].result_msg, "Client read timed out.\r\n",
              BUF_LEN_WORKER_RESULT) ;
      G_runtime->worker[idx].result_code = 500 ;
      break ;                                   // client read timed out
    }
    amt = read(client_sd, buf, BUF_LEN_LINE) ;
    if (amt < 1)
      break ;                                   // client closed connection

    if (f.write((const uint8_t*)buf, amt) != amt)
    {
      snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
               "Failed after writing %d bytes.\r\n", total_bytes) ;
      G_runtime->worker[idx].result_code = 500 ;
      close(client_sd) ;
      f.close() ;
      return ;                                  // write() to file failed
    }
    total_bytes = total_bytes + amt ;
  }
  f.close() ;

  // rename "tmp_file" to the intended "filename"



  snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
           "Received %d bytes from client.\r\n", total_bytes) ;
  G_runtime->worker[idx].result_code = 200 ;
  close(client_sd) ;
}

/*
   This function is caled from "f_fs_cmd()" by worker thread "idx". Our job
   is to remove "filename". On completion, we'll set the worker thread's
   "result_msg" and "result_code" accordingly.
*/

void f_fs_rm(int idx, char *filename)
{
  if (filename == NULL)
  {
    strncpy(G_runtime->worker[idx].result_msg, "Invalid usage.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }

  if (SPIFFS.remove(filename))
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "Removed '%s'.\r\n", filename) ;
    G_runtime->worker[idx].result_code = 200 ;
  }
  else
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "Cannot remove '%s'.\r\n", filename) ;
    G_runtime->worker[idx].result_code = 500 ;
  }
}

void f_fs_send(int idx, char *port_str, char *filename)
{



}

/*
   This function is caled from "f_fs_cmd()" by worker thread "idx". Our job
   is to write "content" into "filename". On completion, we'll set the worker
   thread's "result_msg" and "result_code" accordingly.
*/

void f_fs_write(int idx, char *filename, char *content)
{
  if ((filename == NULL) || (content == NULL))
  {
    strncpy(G_runtime->worker[idx].result_msg, "Invalid usage.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }
  if (strlen(filename) > DEF_MAX_FILENAME_LEN)
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
            "Filename exceeds %d bytes.\r\n", DEF_MAX_FILENAME_LEN) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }
  if (filename[0] != '/')
  {
    strncpy(G_runtime->worker[idx].result_msg,
            "Filenames must begin with '/'.\r\n", BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }

  File f = SPIFFS.open(filename, "w") ;
  if (f)
  {
    int amt = f.print(content) ;
    f.close() ;
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "Wrote %d out of %d bytes to '%s'.\r\n",
             amt, strlen(content), filename) ;
    G_runtime->worker[idx].result_code = 200 ;
  }
  else
  {
    snprintf(G_runtime->worker[idx].result_msg, BUF_LEN_WORKER_RESULT,
             "Cannot write to '%s'.\r\n", filename) ;
    G_runtime->worker[idx].result_code = 500 ;
  }
}

/*
   This is a convenience function which checks that the SPIFFS is mounted.
   If so, we return 1, and if it isn't then we write a message to this worker
   thread's "result_msg" and "result_code".
*/

int f_fs_online(int idx)
{
  if (G_runtime->fs_online == 0)
  {
    strncpy(G_runtime->worker[idx].result_msg, "SPIFFS offline.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return(0) ;
  }
  return(1) ;
}

/*
   We're called when this worker thread's "cmd" is a "fs ...", thus our job
   is to perform various filesystem management functions. Note that it's our
   responsibility to set the worker thread's "result_msg" and "result_code".
*/

void f_fs_cmd(int idx)
{
  // parse the "fs ..." command, or print help. Note that we prepare for the
  // longest possible command, which is "fs write /file content", ie 4x tokens.

  char *tokens[4], *cmd=NULL, *key=NULL, *arg1=NULL, *arg2=NULL ;
  memset(tokens, 0, sizeof(char*) * 4) ;
  if (f_parse(G_runtime->worker[idx].cmd, tokens, 4) == 1)
  {
    strncpy(G_runtime->worker[idx].result_msg,
      "fs format                format the SPIFFS\r\n"
      "fs info                  show filesystem info\r\n"
      "fs ls                    list files\r\n"
      "fs mv <src> <dst>        move (rename) a file\r\n"
      "fs partinfo              show partition layout\r\n"
      "fs read <file>           show contents of a file\r\n"
      "fs recv <port> <file>    save incoming data on <port> to file\r\n"
      "fs rm <file>             removes a file\r\n"
      "fs send <port> <file>    write <file> to TCP client on <port>\r\n"
      "fs write <file> <line>   write one line to a file\r\n",
      BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }
  cmd = tokens[0] ;
  key = tokens[1] ;
  arg1 = tokens[2] ;
  arg2 = tokens[3] ;

  if (strcmp(key, "format") == 0)                               // "format"
  {
    if (SPIFFS.format())
    {
      strncpy(G_runtime->worker[idx].result_msg, "Success. Please reload.\r\n",
              BUF_LEN_WORKER_RESULT) ;
      G_runtime->worker[idx].result_code = 200 ;
    }
    else
    {
      strncpy(G_runtime->worker[idx].result_msg, "Failed.\r\n",
              BUF_LEN_WORKER_RESULT) ;
      G_runtime->worker[idx].result_code = 500 ;
    }
  }
  else
  if (strcmp(key, "info") == 0)                                 // "info"
  {
    if (f_fs_online(idx))
      f_fs_info(idx) ;
  }
  else
  if (strcmp(key, "ls") == 0)                                   // "ls"
  {
    if (f_fs_online(idx))
      f_fs_ls(idx) ;
  }
  else
  if (strcmp(key, "mv") == 0)                                   // "mv"
  {
    if (f_fs_online(idx))
      f_fs_mv(idx, arg1, arg2) ;
  }
  else
  if (strcmp(key, "partinfo") == 0)                             // "partinfo"
  {
    if (f_fs_online(idx))
      f_fs_partinfo(idx) ;
  }
  else
  if (strcmp(key, "read") == 0)                                 // "read"
  {
    if (f_fs_online(idx))
      f_fs_read(idx, arg1) ;
  }
  else
  if (strcmp(key, "recv") == 0)                                 // recv
  {
    if (f_fs_online(idx))
      f_fs_recv(idx, arg1, arg2) ;
  }
  else
  if (strcmp(key, "rm") == 0)                                   // "rm"
  {
    if (f_fs_online(idx))
      f_fs_rm(idx, arg1) ;
  }
  else
  if (strcmp(key, "send") == 0)                                 // "send"
  {
    if (f_fs_online(idx))
      f_fs_send(idx, arg1, arg2) ;
  }
  else
  if (strcmp(key, "write") == 0)                                // "write"
  {
    if (f_fs_online(idx))
      f_fs_write(idx, arg1, arg2) ;
  }
  else                                  // user specified an invalid "key"
  {
    strncpy(G_runtime->worker[idx].result_msg, "Invalid command.\r\n",
            BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
  }
}
