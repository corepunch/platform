#include "../platform.h"

bool_t
axSetLogFile(char const *path)
{
  return (!path || !path[0]) ? TRUE : FALSE;
}

char const *
axGetLogFile(void)
{
  return "";
}

void
axLog(char const *fmt, ...)
{
  (void)fmt;
}

void
axLogFlush(void)
{
}