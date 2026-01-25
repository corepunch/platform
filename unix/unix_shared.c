#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

#include "../platform.h"

longTime_t
SYS_GetMilliseconds(void)
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
SYS_Sleep(longTime_t msec)
{
  struct timespec ts;

  ts.tv_sec = msec / 1000;
  ts.tv_nsec = (msec % 1000) * 1000000;

  nanosleep(&ts, &ts);
}

typedef struct
{
  lpcString_t name;
  uint32_t keynum;
} keyname_t;

keyname_t keynames[] = { { "tab", K_TAB },
  { "enter", K_ENTER },
  { "escape", K_ESCAPE },
  { "space", K_SPACE },
  { "backspace", K_BACKSPACE },
  { "up", K_UPARROW },
  { "down", K_DOWNARROW },
  { "left", K_LEFTARROW },
  { "right", K_RIGHTARROW },
  
  { "alt", K_ALT },
  { "ctrl", K_CTRL },
  { "shift", K_SHIFT },
  
  { "f1", K_F1 },
  { "f2", K_F2 },
  { "f3", K_F3 },
  { "f4", K_F4 },
  { "f5", K_F5 },
  { "f6", K_F6 },
  { "f7", K_F7 },
  { "f8", K_F8 },
  { "f9", K_F9 },
  { "f10", K_F10 },
  { "f11", K_F11 },
  { "f12", K_F12 },
  
  { "ins", K_INS },
  { "del", K_DEL },
  { "pgdn", K_PGDN },
  { "pgup", K_PGUP },
  { "home", K_HOME },
  { "end", K_END },
  
  { "mouse1", K_MOUSE1 },
  { "mouse2", K_MOUSE2 },
  { "mouse3", K_MOUSE3 },
  
  { "joy1", K_JOY1 },
  { "joy2", K_JOY2 },
  { "joy3", K_JOY3 },
  { "joy4", K_JOY4 },
  
  { "aux1", K_AUX1 },
  { "aux2", K_AUX2 },
  { "aux3", K_AUX3 },
  { "aux4", K_AUX4 },
  { "aux5", K_AUX5 },
  { "aux6", K_AUX6 },
  { "aux7", K_AUX7 },
  { "aux8", K_AUX8 },
  { "aux9", K_AUX9 },
  { "aux10", K_AUX10 },
  { "aux11", K_AUX11 },
  { "aux12", K_AUX12 },
  { "aux13", K_AUX13 },
  { "aux14", K_AUX14 },
  { "aux15", K_AUX15 },
  { "aux16", K_AUX16 },
  { "aux17", K_AUX17 },
  { "aux18", K_AUX18 },
  { "aux19", K_AUX19 },
  { "aux20", K_AUX20 },
  { "aux21", K_AUX21 },
  { "aux22", K_AUX22 },
  { "aux23", K_AUX23 },
  { "aux24", K_AUX24 },
  { "aux25", K_AUX25 },
  { "aux26", K_AUX26 },
  { "aux27", K_AUX27 },
  { "aux28", K_AUX28 },
  { "aux29", K_AUX29 },
  { "aux30", K_AUX30 },
  { "aux31", K_AUX31 },
  { "aux32", K_AUX32 },
  
  { "kp_home", K_KP_HOME },
  { "kp_uparrow", K_KP_UPARROW },
  { "kp_pgup", K_KP_PGUP },
  { "kp_leftarrow", K_KP_LEFTARROW },
  { "kp_5", K_KP_5 },
  { "kp_rightarrow", K_KP_RIGHTARROW },
  { "kp_end", K_KP_END },
  { "kp_downarrow", K_KP_DOWNARROW },
  { "kp_pgdn", K_KP_PGDN },
  { "kp_enter", K_KP_ENTER },
  { "kp_ins", K_KP_INS },
  { "kp_del", K_KP_DEL },
  { "kp_slash", K_KP_SLASH },
  { "kp_minus", K_KP_MINUS },
  { "kp_plus", K_KP_PLUS },
  
  { "mwheelup", K_MWHEELUP },
  { "mwheeldown", K_MWHEELDOWN },
  
  { "pause", K_PAUSE },
  
  { "semicolon",
    ';' }, // because a raw semicolon seperates commands
  
  { NULL, 0 } };

char keyshift[256];

ORCA_API lpcString_t
Key_KeynumToString(uint32_t keynum)
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
