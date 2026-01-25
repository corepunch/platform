#include "../platform.h"
#include <unistd.h>

#define APPNAME "highperf"

static PATHSTR g_share = { 0 };
static PATHSTR g_local = { 0 };
static PATHSTR g_lib = { 0 };

char const*
SYS_GetPlatform(void)
{
  return "linux (wayland)";
}

char const*
SYS_SettingsDirectory()
{
  if (g_local[0] == 0) {
    snprintf(g_local, sizeof(g_local), "%s/." APPNAME, getenv("HOME"));
  }
  return g_local;
}

static void
SYS_BundleDirectory(char* buf, int sz, char const* dir)
{
  char path[512];
  snprintf(path, sizeof(path), "/proc/%d/exe", getpid());
  int len = readlink(path, buf, sz - 1);
  buf[len] = '\0';
  char* c = buf + len - 1;
  for (int i = 1;; c--) {
    if (*c == '/') {
      if (i > 0) {
        i--;
      } else {
        break;
      }
    }
  }
  strcpy(c, dir);
}

char const*
SYS_ShareDirectory()
{
  if (g_share[0] == 0) {
    SYS_BundleDirectory(g_share, sizeof(g_share), "/share/" APPNAME);
  }
  return g_share;
}

char const*
SYS_LibDirectory()
{
  if (g_lib[0] == 0) {
    SYS_BundleDirectory(g_lib, sizeof(g_lib), "/lib/" APPNAME);
  }
  return g_lib;
}

char const*
KEY_GetKeyName(int keycode)
{
  // for (keymap_t const *km = darwin_scancode_table; km->keycode != -1;
  // km++) {
  //     if (keycode == km->keycode) {
  //         return km->keyname;
  //     }
  // }
  return "";
}