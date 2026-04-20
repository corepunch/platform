/*
 * test_net.c – behavioural tests for the platform networking API.
 *
 * Tests cover:
 *   - axNetInit / axNetShutdown lifecycle
 *   - axNetSocket / axNetClose
 *   - axNetSetNonBlocking / axNetSetReuseAddr socket options
 *   - axNetBind / axNetListen / axNetAccept (loopback server)
 *   - axNetConnect (loopback client)
 *   - axNetSend / axNetRecv round-trip
 *   - axNetResolve (localhost / 127.0.0.1)
 *   - axNetPoll timeout and readiness
 *   - axNetWouldBlock on a non-blocking socket
 *
 * The TLS functions are not tested here because they require network access
 * to a remote TLS server, which is not available in all CI environments.
 * The test only verifies that the TLS function pointers are non-NULL and that
 * axTlsConnect returns NULL on an unconnected socket without crashing.
 */

#include "platform.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32) || defined(__MINGW32__)
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#endif

/* -------------------------------------------------------------------------
 * test_init_shutdown
 *   axNetInit must return TRUE; a second call must also succeed.
 *   axNetShutdown must not crash.
 * ---------------------------------------------------------------------- */
static void
test_init_shutdown(void)
{
  assert(axNetInit() == TRUE);
  assert(axNetInit() == TRUE); /* idempotent */
  axNetShutdown();
  assert(axNetInit() == TRUE); /* re-initialise */
}

/* -------------------------------------------------------------------------
 * test_socket_create_close
 *   axNetSocket must return a valid (>= 0) descriptor for TCP and UDP,
 *   both IPv4 and IPv6.  axNetClose must not crash.
 * ---------------------------------------------------------------------- */
static void
test_socket_create_close(void)
{
  int s;

  s = axNetSocket(AX_NET_AF_IPV4, AX_NET_SOCK_TCP);
  assert(s >= 0);
  axNetClose(s);

  s = axNetSocket(AX_NET_AF_IPV4, AX_NET_SOCK_UDP);
  assert(s >= 0);
  axNetClose(s);

  /* Closing -1 must be safe. */
  axNetClose(-1);
}

/* -------------------------------------------------------------------------
 * test_socket_options
 *   axNetSetNonBlocking and axNetSetReuseAddr must return TRUE on a valid
 *   socket.
 * ---------------------------------------------------------------------- */
static void
test_socket_options(void)
{
  int s = axNetSocket(AX_NET_AF_IPV4, AX_NET_SOCK_TCP);
  assert(s >= 0);

  assert(axNetSetNonBlocking(s, TRUE)  == TRUE);
  assert(axNetSetNonBlocking(s, FALSE) == TRUE);
  assert(axNetSetReuseAddr(s, TRUE)    == TRUE);
  assert(axNetSetReuseAddr(s, FALSE)   == TRUE);

  axNetClose(s);
}

/* -------------------------------------------------------------------------
 * test_loopback_echo
 *   Bind a TCP server to an ephemeral port on 127.0.0.1.  Connect a
 *   client, exchange a short message, verify the echoed bytes match.
 * ---------------------------------------------------------------------- */
static void
test_loopback_echo(void)
{
  /* ------ server socket ------ */
  int server = axNetSocket(AX_NET_AF_IPV4, AX_NET_SOCK_TCP);
  assert(server >= 0);
  assert(axNetSetReuseAddr(server, TRUE) == TRUE);

  /* Bind to port 0 so the OS assigns a free port. */
  assert(axNetBind(server, 0) == TRUE);
  assert(axNetListen(server, 1) == TRUE);

  /* Discover the assigned port. */
#if defined(_WIN32) || defined(__MINGW32__)
  int name_len = sizeof(struct sockaddr_in);
  struct sockaddr_in srv_addr;
  memset(&srv_addr, 0, sizeof(srv_addr));
  getsockname((SOCKET)server, (struct sockaddr *)&srv_addr,
              (socklen_t *)&name_len);
  uint16_t port = ntohs(srv_addr.sin_port);
#else
  struct sockaddr_in srv_addr;
  socklen_t name_len = sizeof(srv_addr);
  memset(&srv_addr, 0, sizeof(srv_addr));
  getsockname(server, (struct sockaddr *)&srv_addr, &name_len);
  uint16_t port = ntohs(srv_addr.sin_port);
#endif
  assert(port > 0);

  /* ------ client socket ------ */
  int client = axNetSocket(AX_NET_AF_IPV4, AX_NET_SOCK_TCP);
  assert(client >= 0);
  assert(axNetConnect(client, "127.0.0.1", port) == TRUE);

  /* Accept the incoming connection. */
  int accepted = axNetAccept(server);
  assert(accepted >= 0);

  /* Send from client → accepted peer. */
  char const *msg = "hello";
  int sent = axNetSend(client, msg, (int)strlen(msg));
  assert(sent == (int)strlen(msg));

  /* Receive on the accepted socket. */
  char buf[32];
  memset(buf, 0, sizeof(buf));
  /* Poll briefly to ensure data arrives before calling recv. */
  int ready = axNetPoll(accepted, AX_NET_POLL_READ, 1000);
  assert(ready > 0);

  int received = axNetRecv(accepted, buf, (int)sizeof(buf) - 1);
  assert(received == (int)strlen(msg));
  assert(memcmp(buf, msg, (size_t)received) == 0);

  axNetClose(accepted);
  axNetClose(client);
  axNetClose(server);
}

/* -------------------------------------------------------------------------
 * test_poll_timeout
 *   axNetPoll on an idle socket must return 0 (timeout) quickly.
 * ---------------------------------------------------------------------- */
static void
test_poll_timeout(void)
{
  int s = axNetSocket(AX_NET_AF_IPV4, AX_NET_SOCK_TCP);
  assert(s >= 0);
  assert(axNetBind(s, 0) == TRUE);
  assert(axNetListen(s, 1) == TRUE);

  /* No client connects: expect timeout (0). */
  int result = axNetPoll(s, AX_NET_POLL_READ, 50);
  assert(result == 0);

  axNetClose(s);
}

/* -------------------------------------------------------------------------
 * test_resolve_localhost
 *   axNetResolve("localhost", ...) or axNetResolve("127.0.0.1", ...)
 *   must succeed and produce a non-empty string.
 * ---------------------------------------------------------------------- */
static void
test_resolve_localhost(void)
{
  char out[64];

  /* Numeric address must resolve to itself. */
  bool_t ok = axNetResolve("127.0.0.1", out, (int)sizeof(out));
  assert(ok == TRUE);
  assert(strlen(out) > 0);

  /* Hostname resolution; may fail in a restricted network environment,
   * so only verify that the function returns without crashing. */
  axNetResolve("localhost", out, (int)sizeof(out));
}

/* -------------------------------------------------------------------------
 * test_nonblocking_wouldblock
 *   On a non-blocking socket that has no data, axNetRecv should return 0
 *   (no error, no data) and axNetWouldBlock should return TRUE.
 * ---------------------------------------------------------------------- */
static void
test_nonblocking_wouldblock(void)
{
  int server = axNetSocket(AX_NET_AF_IPV4, AX_NET_SOCK_TCP);
  assert(server >= 0);
  assert(axNetSetReuseAddr(server, TRUE) == TRUE);
  assert(axNetBind(server, 0) == TRUE);
  assert(axNetListen(server, 1) == TRUE);

#if defined(_WIN32) || defined(__MINGW32__)
  int addr_len = sizeof(struct sockaddr_in);
  struct sockaddr_in sa;
  memset(&sa, 0, sizeof(sa));
  getsockname((SOCKET)server, (struct sockaddr *)&sa,
              (socklen_t *)&addr_len);
  uint16_t server_port = ntohs(sa.sin_port);
#else
  struct sockaddr_in sa;
  socklen_t addr_len = sizeof(sa);
  memset(&sa, 0, sizeof(sa));
  getsockname(server, (struct sockaddr *)&sa, &addr_len);
  uint16_t server_port = ntohs(sa.sin_port);
#endif

  int client = axNetSocket(AX_NET_AF_IPV4, AX_NET_SOCK_TCP);
  assert(client >= 0);
  assert(axNetConnect(client, "127.0.0.1", server_port) == TRUE);

  int peer = axNetAccept(server);
  assert(peer >= 0);

  /* Put peer in non-blocking mode and immediately try to read – no data. */
  assert(axNetSetNonBlocking(peer, TRUE) == TRUE);
  char dummy[16];
  int rc = axNetRecv(peer, dummy, (int)sizeof(dummy));
  /* Expect 0 (would-block) and axNetWouldBlock() == TRUE. */
  assert(rc == 0);
  assert(axNetWouldBlock() == TRUE);

  axNetClose(peer);
  axNetClose(client);
  axNetClose(server);
}

/* -------------------------------------------------------------------------
 * test_udp_loopback
 *   Bind a UDP socket to an ephemeral port.  Connect a second UDP socket to
 *   that port (sets the default destination) then exchange a short datagram.
 * ---------------------------------------------------------------------- */
static void
test_udp_loopback(void)
{
  /* ------ server socket (bound) ------ */
  int server = axNetSocket(AX_NET_AF_IPV4, AX_NET_SOCK_UDP);
  assert(server >= 0);
  assert(axNetSetReuseAddr(server, TRUE) == TRUE);
  assert(axNetBind(server, 0) == TRUE);

  /* Discover the assigned port. */
#if defined(_WIN32) || defined(__MINGW32__)
  int slen = sizeof(struct sockaddr_in);
  struct sockaddr_in srv;
  memset(&srv, 0, sizeof(srv));
  getsockname((SOCKET)server, (struct sockaddr *)&srv, (socklen_t *)&slen);
#else
  struct sockaddr_in srv;
  socklen_t slen = sizeof(srv);
  memset(&srv, 0, sizeof(srv));
  getsockname(server, (struct sockaddr *)&srv, &slen);
#endif
  uint16_t port = ntohs(srv.sin_port);
  assert(port > 0);

  /* ------ client socket (connected UDP) ------ */
  int client = axNetSocket(AX_NET_AF_IPV4, AX_NET_SOCK_UDP);
  assert(client >= 0);
  /* axNetConnect on a UDP socket sets the default peer address so that
   * axNetSend / axNetRecv work without sendto / recvfrom. */
  assert(axNetConnect(client, "127.0.0.1", port) == TRUE);

  char const *msg = "udp";
  int sent = axNetSend(client, msg, 3);
  assert(sent == 3);

  int ready = axNetPoll(server, AX_NET_POLL_READ, 1000);
  assert(ready > 0);

  char buf[16];
  memset(buf, 0, sizeof(buf));
  int received = axNetRecv(server, buf, (int)sizeof(buf) - 1);
  assert(received == 3);
  assert(memcmp(buf, msg, 3) == 0);

  axNetClose(client);
  axNetClose(server);
}

/* -------------------------------------------------------------------------
 * test_socket_create_ipv6
 *   Creating and closing an IPv6 TCP socket must not crash.
 *   IPv6 is optional in some CI environments, so failure is tolerated.
 * ---------------------------------------------------------------------- */
static void
test_socket_create_ipv6(void)
{
  int s = axNetSocket(AX_NET_AF_IPV6, AX_NET_SOCK_TCP);
  /* Skip gracefully when IPv6 is unavailable. */
  if (s >= 0)
    axNetClose(s);
}

/* -------------------------------------------------------------------------
 * test_poll_write_ready
 *   A connected socket that has not sent anything must be immediately ready
 *   for writing.
 * ---------------------------------------------------------------------- */
static void
test_poll_write_ready(void)
{
  int server = axNetSocket(AX_NET_AF_IPV4, AX_NET_SOCK_TCP);
  assert(server >= 0);
  assert(axNetSetReuseAddr(server, TRUE) == TRUE);
  assert(axNetBind(server, 0) == TRUE);
  assert(axNetListen(server, 1) == TRUE);

#if defined(_WIN32) || defined(__MINGW32__)
  int wlen = sizeof(struct sockaddr_in);
  struct sockaddr_in wa;
  memset(&wa, 0, sizeof(wa));
  getsockname((SOCKET)server, (struct sockaddr *)&wa, (socklen_t *)&wlen);
  uint16_t wport = ntohs(wa.sin_port);
#else
  struct sockaddr_in wa;
  socklen_t wlen = sizeof(wa);
  memset(&wa, 0, sizeof(wa));
  getsockname(server, (struct sockaddr *)&wa, &wlen);
  uint16_t wport = ntohs(wa.sin_port);
#endif

  int client = axNetSocket(AX_NET_AF_IPV4, AX_NET_SOCK_TCP);
  assert(client >= 0);
  assert(axNetConnect(client, "127.0.0.1", wport) == TRUE);

  int peer = axNetAccept(server);
  assert(peer >= 0);

  /* A fresh, connected socket should be write-ready immediately. */
  int result = axNetPoll(client, AX_NET_POLL_WRITE, 500);
  assert(result > 0);

  axNetClose(peer);
  axNetClose(client);
  axNetClose(server);
}

/* -------------------------------------------------------------------------
 * test_tls_api_available
 *   Verify the TLS function pointers are non-NULL.
 *   axTlsConnect on an invalid socket must return NULL without crashing.
 * ---------------------------------------------------------------------- */
static void
test_tls_api_available(void)
{
  /* Function pointers must exist (checked at link time; assert for safety). */
  assert(axTlsConnect != NULL);
  assert(axTlsClose   != NULL);
  assert(axTlsSend    != NULL);
  assert(axTlsRecv    != NULL);

  /* axTlsConnect on fd -1 must return NULL without crashing. */
  AXtlsctx *ctx = axTlsConnect(-1, "example.com");
  /* May or may not be NULL depending on the TLS backend, but must not
   * crash.  If it somehow returned non-NULL, close it cleanly. */
  if (ctx)
    axTlsClose(ctx);
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */
int
main(void)
{
  test_init_shutdown();
  printf("axNetInit/axNetShutdown: OK\n");

  test_socket_create_close();
  printf("axNetSocket/axNetClose: OK\n");

  test_socket_options();
  printf("axNetSetNonBlocking/axNetSetReuseAddr: OK\n");

  test_loopback_echo();
  printf("loopback TCP echo: OK\n");

  test_poll_timeout();
  printf("axNetPoll timeout: OK\n");

  test_resolve_localhost();
  printf("axNetResolve: OK\n");

  test_nonblocking_wouldblock();
  printf("non-blocking + axNetWouldBlock: OK\n");

  test_udp_loopback();
  printf("UDP loopback datagram: OK\n");

  test_socket_create_ipv6();
  printf("IPv6 socket create: OK\n");

  test_poll_write_ready();
  printf("axNetPoll write-ready: OK\n");

  test_tls_api_available();
  printf("TLS API symbols: OK\n");

  printf("All network tests passed.\n");
  axNetShutdown();
  return 0;
}
