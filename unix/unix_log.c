#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../platform.h"

static FILE *g_ax_log_file = NULL;
static char  g_ax_log_path[1024] = {0};

bool_t
axSetLogFile(char const *path)
{
  FILE *file;

  if (g_ax_log_file) {
    fclose(g_ax_log_file);
    g_ax_log_file = NULL;
  }
  g_ax_log_path[0] = '\0';

  if (!path || !path[0])
    return TRUE;

  file = fopen(path, "a");
  if (!file)
    return FALSE;

  setvbuf(file, NULL, _IOLBF, 0);
  g_ax_log_file = file;
  strncpy(g_ax_log_path, path, sizeof(g_ax_log_path) - 1);
  g_ax_log_path[sizeof(g_ax_log_path) - 1] = '\0';
  return TRUE;
}

char const *
axGetLogFile(void)
{
  return g_ax_log_path;
}

void
axLog(char const *fmt, ...)
{
  va_list args;

  if (!g_ax_log_file || !fmt)
    return;

  fprintf(g_ax_log_file, "[%10lu] ", (unsigned long)axGetMilliseconds());
  va_start(args, fmt);
  vfprintf(g_ax_log_file, fmt, args);
  va_end(args);
  fputc('\n', g_ax_log_file);
}

void
axLogFlush(void)
{
  if (g_ax_log_file)
    fflush(g_ax_log_file);
}