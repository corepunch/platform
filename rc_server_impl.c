/*
 * rc_server_impl.c — Remote-control TCP server shared implementation.
 *
 * Not compiled directly.  Included by unix/unix_rc.c and
 * windows/windows_rc.c, each of which pulls in the right platform headers
 * before the include.
 *
 * Protocol: newline-terminated text commands, one reply per command.
 * See platform.h @defgroup rc for the full command reference.
 */

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#include "platform.h"

/* -------------------------------------------------------------------------
 * State
 * ---------------------------------------------------------------------- */

static volatile int s_active = 0;
static int          s_server = -1;
static axthread_t   s_thread = NULL;
static axmutex_t    s_mutex  = NULL;

/* Protected by s_mutex. */
static char s_screenshot_path[512] = {0};

/* -------------------------------------------------------------------------
 * Command dispatch
 * ---------------------------------------------------------------------- */

static void
rc_reply(int conn, const char *msg)
{
  axNetSend(conn, msg, (int)strlen(msg));
}

static bool_t
rc_dispatch(int conn, const char *line)
{
  int x, y, code, dx, dy, mods;
  char path[512];

  if (sscanf(line, "click %d %d", &x, &y) == 2) {
    axPostMessageW(NULL, kEventLeftButtonDown,  MAKEDWORD(x, y), NULL);
    axPostMessageW(NULL, kEventLeftButtonUp,    MAKEDWORD(x, y), NULL);
    rc_reply(conn, "ok\n");

  } else if (sscanf(line, "rclick %d %d", &x, &y) == 2) {
    axPostMessageW(NULL, kEventRightButtonDown, MAKEDWORD(x, y), NULL);
    axPostMessageW(NULL, kEventRightButtonUp,   MAKEDWORD(x, y), NULL);
    rc_reply(conn, "ok\n");

  } else if (sscanf(line, "move %d %d", &x, &y) == 2) {
    axPostMessageW(NULL, kEventMouseMoved, MAKEDWORD(x, y), NULL);
    rc_reply(conn, "ok\n");

  } else if (sscanf(line, "scroll %d %d %d %d", &x, &y, &dx, &dy) == 4) {
    axPostMessageW(NULL, kEventScrollWheel, MAKEDWORD(x, y),
                   (void *)(intptr_t)MAKEDWORD(dx, dy));
    rc_reply(conn, "ok\n");

  } else if (sscanf(line, "keydown %d %d", &code, &mods) == 2) {
    axPostMessageW(NULL, kEventKeyDown, MAKEDWORD(code, mods), NULL);
    rc_reply(conn, "ok\n");

  } else if (sscanf(line, "keydown %d", &code) == 1) {
    axPostMessageW(NULL, kEventKeyDown, (uint32_t)code, NULL);
    rc_reply(conn, "ok\n");

  } else if (sscanf(line, "keyup %d %d", &code, &mods) == 2) {
    axPostMessageW(NULL, kEventKeyUp, MAKEDWORD(code, mods), NULL);
    rc_reply(conn, "ok\n");

  } else if (sscanf(line, "keyup %d", &code) == 1) {
    axPostMessageW(NULL, kEventKeyUp, (uint32_t)code, NULL);
    rc_reply(conn, "ok\n");

  } else if (sscanf(line, "key %d %d", &code, &mods) == 2) {
    axPostMessageW(NULL, kEventKeyDown, MAKEDWORD(code, mods), NULL);
    axPostMessageW(NULL, kEventKeyUp,   MAKEDWORD(code, mods), NULL);
    rc_reply(conn, "ok\n");

  } else if (sscanf(line, "key %d", &code) == 1) {
    axPostMessageW(NULL, kEventKeyDown, (uint32_t)code, NULL);
    axPostMessageW(NULL, kEventKeyUp,   (uint32_t)code, NULL);
    rc_reply(conn, "ok\n");

  } else if (strncmp(line, "type ", 5) == 0) {
    /* Each character round-trips through kEventKeyDown/Up the same way a
     * real keypress does: keyCode = uppercase ASCII, lParam's low byte
     * carries the literal character for text-input handlers. */
    for (const char *p = line + 5; *p; p++) {
      unsigned char ch = (unsigned char)*p;
      uint32_t key_code = (uint32_t)toupper(ch);
      axPostMessageW(NULL, kEventKeyDown, key_code, (void *)(intptr_t)ch);
      axPostMessageW(NULL, kEventKeyUp,   key_code, NULL);
    }
    rc_reply(conn, "ok\n");

  } else if (sscanf(line, "screenshot %511s", path) == 1) {
    axMutexLock(s_mutex);
    strncpy(s_screenshot_path, path, sizeof(s_screenshot_path) - 1);
    axMutexUnlock(s_mutex);
    /* The main loop only polls for a pending screenshot between blocking
     * axGetMessage() calls, so wake it with a message the dispatcher
     * already treats as a no-op. */
    axPostMessageW(NULL, kEventModifiersChanged, 0, NULL);
    rc_reply(conn, "ok\n");

  } else if (strcmp(line, "stop") == 0) {
    axPostMessageW(NULL, kEventWindowClosed, 0, NULL);
    rc_reply(conn, "ok\n");

  } else if (strcmp(line, "quit") == 0) {
    rc_reply(conn, "ok\n");
    return FALSE;

  } else {
    rc_reply(conn, "err unknown command\n");
  }
  return TRUE;
}

/* -------------------------------------------------------------------------
 * Per-connection handler: reads lines, dispatches until closed or error.
 * ---------------------------------------------------------------------- */

static void
rc_handle_connection(int conn)
{
  char buf[1024];
  int  fill = 0;

  while (s_active) {
    /* Poll so we can notice s_active going false. */
    int ready = axNetPoll(conn, AX_NET_POLL_READ, 200);
    if (ready < 0)
      break;
    if (!ready)
      continue;

    int n = axNetRecv(conn, buf + fill, (int)(sizeof(buf) - 1 - fill));
    if (n <= 0)
      break;
    fill += n;
    buf[fill] = '\0';

    /* Dispatch every complete line. */
    char *p = buf;
    char *nl;
    while ((nl = strchr(p, '\n')) != NULL) {
      *nl = '\0';
      /* Strip trailing \r for Windows-style CRLF. */
      int len = (int)(nl - p);
      if (len > 0 && p[len - 1] == '\r')
        p[len - 1] = '\0';

      if (*p && !rc_dispatch(conn, p))
        return;

      p = nl + 1;
    }

    /* Shift any partial line to the front of the buffer. */
    fill = (int)(buf + fill - p);
    if (fill > 0)
      memmove(buf, p, fill);
    else
      fill = 0;
  }

  axNetClose(conn);
}

/* -------------------------------------------------------------------------
 * Accept thread
 * ---------------------------------------------------------------------- */

static void
rc_thread(void *arg)
{
  (void)arg;
  while (s_active) {
    /* Poll the server socket so we can stop cleanly. */
    int ready = axNetPoll(s_server, AX_NET_POLL_READ, 200);
    if (ready <= 0)
      continue;

    int conn = axNetAccept(s_server);
    if (conn < 0)
      continue;

    rc_handle_connection(conn);
  }
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

bool_t
axRCStart(uint16_t port)
{
  if (s_active)
    return TRUE;

  axNetInit();

  s_server = axNetSocket(AX_NET_AF_IPV4, AX_NET_SOCK_TCP);
  if (s_server < 0)
    return FALSE;

  axNetSetReuseAddr(s_server, TRUE);

  if (!axNetBind(s_server, "127.0.0.1", port) || !axNetListen(s_server, 4)) {
    axNetClose(s_server);
    s_server = -1;
    return FALSE;
  }

  s_mutex  = axMutexCreate();
  s_active = 1;
  s_thread = axThreadCreate(rc_thread, NULL);
  if (!s_thread) {
    s_active = 0;
    axMutexDestroy(s_mutex);
    axNetClose(s_server);
    s_server = -1;
    return FALSE;
  }

  return TRUE;
}

void
axRCStop(void)
{
  if (!s_active)
    return;
  s_active = 0;
  axThreadJoin(s_thread);
  s_thread = NULL;
  axNetClose(s_server);
  s_server = -1;
  axMutexDestroy(s_mutex);
  s_mutex = NULL;
}

bool_t
axRCPopScreenshot(char *path, int pathlen)
{
  if (!path || pathlen <= 0 || !s_mutex)
    return FALSE;
  axMutexLock(s_mutex);
  bool_t found = FALSE;
  if (s_screenshot_path[0] && (int)strlen(s_screenshot_path) < pathlen) {
    strcpy(path, s_screenshot_path);
    s_screenshot_path[0] = '\0';
    found = TRUE;
  }
  axMutexUnlock(s_mutex);
  return found;
}
