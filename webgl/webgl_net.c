/*
 * webgl_net.c – Networking stubs for the WebGL / Emscripten platform.
 *
 * Raw TCP/UDP sockets are not available to Emscripten side-modules; network
 * access must go through the browser's XMLHttpRequest or Fetch API instead.
 * All socket functions return error values, and the TLS functions return NULL.
 *
 * Higher-level code that needs HTTP(S) from a WebGL context should use
 * Emscripten's emscripten_fetch() facility directly.
 */

#include "../platform.h"

bool_t
axNetInit(void)
{
  return FALSE;
}

void
axNetShutdown(void)
{
}

int
axNetSocket(int af, int type)
{
  (void)af;
  (void)type;
  return -1;
}

void
axNetClose(int sock)
{
  (void)sock;
}

bool_t
axNetSetNonBlocking(int sock, bool_t nonblocking)
{
  (void)sock;
  (void)nonblocking;
  return FALSE;
}

bool_t
axNetSetReuseAddr(int sock, bool_t reuse)
{
  (void)sock;
  (void)reuse;
  return FALSE;
}

bool_t
axNetBind(int sock, char const *host, uint16_t port)
{
  (void)host;
  (void)sock;
  (void)port;
  return FALSE;
}

bool_t
axNetListen(int sock, int backlog)
{
  (void)sock;
  (void)backlog;
  return FALSE;
}

int
axNetAccept(int sock)
{
  (void)sock;
  return -1;
}

bool_t
axNetConnect(int sock, char const *host, uint16_t port)
{
  (void)sock;
  (void)host;
  (void)port;
  return FALSE;
}

int
axNetSend(int sock, void const *buf, int len)
{
  (void)sock;
  (void)buf;
  (void)len;
  return -1;
}

int
axNetRecv(int sock, void *buf, int len)
{
  (void)sock;
  (void)buf;
  (void)len;
  return -1;
}

bool_t
axNetWouldBlock(void)
{
  return FALSE;
}

bool_t
axNetResolve(char const *host, char *out, int outlen)
{
  (void)host;
  (void)out;
  (void)outlen;
  return FALSE;
}

int
axNetPoll(int sock, int events, int timeout_ms)
{
  (void)sock;
  (void)events;
  (void)timeout_ms;
  return -1;
}

AXtlsctx *
axTlsConnect(int sock, char const *hostname)
{
  (void)sock;
  (void)hostname;
  return NULL;
}

void
axTlsClose(AXtlsctx *ctx)
{
  (void)ctx;
}

int
axTlsSend(AXtlsctx *ctx, void const *buf, int len)
{
  (void)ctx;
  (void)buf;
  (void)len;
  return -1;
}

int
axTlsRecv(AXtlsctx *ctx, void *buf, int len)
{
  (void)ctx;
  (void)buf;
  (void)len;
  return -1;
}
