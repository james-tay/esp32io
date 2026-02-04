#include "esp_partition.h"

/*
   We're called when this worker thread's "cmd" is a "fs ...", thus our job
   is to perform various filesystem management functions. Note that it's our
   responsibility to set the worker thread's "result_msg" and "result_code".
*/

void f_fs_cmd(int idx)
{
  char line[BUF_LEN_LINE] ;

  // parse the "fs ..." command, or print help

  char *tokens[4], *cmd=NULL, *key=NULL ;
  if (f_parse(G_runtime->worker[idx].cmd, tokens,4) == 1)
  {
    strncpy(G_runtime->worker[idx].result_msg,
      "fs info                  show filesystem info\r\n"
      "fs ls                    list files\r\n"
      "fs partinfo              show partition layout\r\n"
      "fs read <file>           show contents of a file\r\n"
      "fs write <file> <line>   write one line to a file\r\n",
      BUF_LEN_WORKER_RESULT) ;
    G_runtime->worker[idx].result_code = 400 ;
    return ;
  }
  cmd = tokens[0] ;
  key = tokens[1] ;

  if (strcmp(key, "partinfo") == 0)
  {
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
              strlen(G_runtime->worker[idx].result_msg)) ;
      p_iter = esp_partition_next(p_iter) ;
    }
    esp_partition_iterator_release(p_iter) ;
    G_runtime->worker[idx].result_code = 200 ;
  }

}
