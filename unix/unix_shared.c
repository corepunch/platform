#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <dlfcn.h>

#include "../platform.h"

longTime_t
axGetMilliseconds(void)
{
  struct timeval tp;
  struct timezone tzp;
  static long secbase;

  gettimeofday(&tp, &tzp);

  if (!secbase) {
    secbase = tp.tv_sec;
    return tp.tv_usec / 1000;
  }

  return (tp.tv_sec - secbase) * 1000 + tp.tv_usec / 1000;
}

void
axSleep(longTime_t msec)
{
  struct timespec ts;

  ts.tv_sec = msec / 1000;
  ts.tv_nsec = (msec % 1000) * 1000000;

  nanosleep(&ts, &ts);
}

typedef struct
{
  char const *name;
  uint32_t keynum;
} keyname_t;

keyname_t keynames[] = { { "tab", AX_KEY_TAB },
  { "enter", AX_KEY_ENTER },
  { "escape", AX_KEY_ESCAPE },
  { "space", AX_KEY_SPACE },
  { "backspace", AX_KEY_BACKSPACE },
  { "up", AX_KEY_UPARROW },
  { "down", AX_KEY_DOWNARROW },
  { "left", AX_KEY_LEFTARROW },
  { "right", AX_KEY_RIGHTARROW },
  
  { "alt", AX_KEY_ALT },
  { "ctrl", AX_KEY_CTRL },
  { "shift", AX_KEY_SHIFT },
  
  { "f1", AX_KEY_F1 },
  { "f2", AX_KEY_F2 },
  { "f3", AX_KEY_F3 },
  { "f4", AX_KEY_F4 },
  { "f5", AX_KEY_F5 },
  { "f6", AX_KEY_F6 },
  { "f7", AX_KEY_F7 },
  { "f8", AX_KEY_F8 },
  { "f9", AX_KEY_F9 },
  { "f10", AX_KEY_F10 },
  { "f11", AX_KEY_F11 },
  { "f12", AX_KEY_F12 },
  
  { "ins", AX_KEY_INS },
  { "del", AX_KEY_DEL },
  { "pgdn", AX_KEY_PGDN },
  { "pgup", AX_KEY_PGUP },
  { "home", AX_KEY_HOME },
  { "end", AX_KEY_END },
  
  { "mouse1", AX_KEY_MOUSE1 },
  { "mouse2", AX_KEY_MOUSE2 },
  { "mouse3", AX_KEY_MOUSE3 },
  
  { "joy1", AX_KEY_JOY1 },
  { "joy2", AX_KEY_JOY2 },
  { "joy3", AX_KEY_JOY3 },
  { "joy4", AX_KEY_JOY4 },
  
  { "aux1", AX_KEY_AUX1 },
  { "aux2", AX_KEY_AUX2 },
  { "aux3", AX_KEY_AUX3 },
  { "aux4", AX_KEY_AUX4 },
  { "aux5", AX_KEY_AUX5 },
  { "aux6", AX_KEY_AUX6 },
  { "aux7", AX_KEY_AUX7 },
  { "aux8", AX_KEY_AUX8 },
  { "aux9", AX_KEY_AUX9 },
  { "aux10", AX_KEY_AUX10 },
  { "aux11", AX_KEY_AUX11 },
  { "aux12", AX_KEY_AUX12 },
  { "aux13", AX_KEY_AUX13 },
  { "aux14", AX_KEY_AUX14 },
  { "aux15", AX_KEY_AUX15 },
  { "aux16", AX_KEY_AUX16 },
  { "aux17", AX_KEY_AUX17 },
  { "aux18", AX_KEY_AUX18 },
  { "aux19", AX_KEY_AUX19 },
  { "aux20", AX_KEY_AUX20 },
  { "aux21", AX_KEY_AUX21 },
  { "aux22", AX_KEY_AUX22 },
  { "aux23", AX_KEY_AUX23 },
  { "aux24", AX_KEY_AUX24 },
  { "aux25", AX_KEY_AUX25 },
  { "aux26", AX_KEY_AUX26 },
  { "aux27", AX_KEY_AUX27 },
  { "aux28", AX_KEY_AUX28 },
  { "aux29", AX_KEY_AUX29 },
  { "aux30", AX_KEY_AUX30 },
  { "aux31", AX_KEY_AUX31 },
  { "aux32", AX_KEY_AUX32 },
  
  { "kp_home", AX_KEY_KP_HOME },
  { "kp_uparrow", AX_KEY_KP_UPARROW },
  { "kp_pgup", AX_KEY_KP_PGUP },
  { "kp_leftarrow", AX_KEY_KP_LEFTARROW },
  { "kp_5", AX_KEY_KP_5 },
  { "kp_rightarrow", AX_KEY_KP_RIGHTARROW },
  { "kp_end", AX_KEY_KP_END },
  { "kp_downarrow", AX_KEY_KP_DOWNARROW },
  { "kp_pgdn", AX_KEY_KP_PGDN },
  { "kp_enter", AX_KEY_KP_ENTER },
  { "kp_ins", AX_KEY_KP_INS },
  { "kp_del", AX_KEY_KP_DEL },
  { "kp_slash", AX_KEY_KP_SLASH },
  { "kp_minus", AX_KEY_KP_MINUS },
  { "kp_plus", AX_KEY_KP_PLUS },
  
  { "mwheelup", AX_KEY_MWHEELUP },
  { "mwheeldown", AX_KEY_MWHEELDOWN },
  
  { "pause", AX_KEY_PAUSE },
  
  { "semicolon",
    ';' }, // because a raw semicolon seperates commands
  
  { NULL, 0 } };

char keyshift[256];

char const *
axKeynumToString(uint32_t keynum)
{
  keyname_t* kn;
  static char tinystr[2];
  keynum = keynum&0xff;
  if (keynum == -1)
    return "<KEY NOT FOUND>";
  if (keynum > 32 && keynum < 127) { // printable ascii
    tinystr[0] = keynum;
    tinystr[1] = 0;
    return tinystr;
  }
  for (kn = keynames; kn->name; kn++)
    if (keynum == kn->keynum)
      return kn->name;
  return "<UNKNOWN KEYNUM>";
}

void *
axDynlibOpen(char const *path)
{
  return dlopen(path, RTLD_LAZY);
}

void *
axDynlibSym(void *handle, char const *sym)
{
  return dlsym(handle, sym);
}

void
axDynlibClose(void *handle)
{
  if (handle)
    dlclose(handle);
}

char const *
axDynlibError(void)
{
  return dlerror();
}
