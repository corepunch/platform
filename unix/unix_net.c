/*
 * unix_net.c - POSIX networking implementation.
 *
 * Provides the axNet* and axTls* API for macOS and Linux (and any other
 * POSIX platform that includes unix/ sources in its build).
 *
 * TLS backends:
 *   macOS  - Secure Transport (Security.framework, no extra dependencies)
 *   Linux  - OpenSSL when compiled with -DHAVE_OPENSSL; TLS unavailable
 *             otherwise (axTlsConnect returns NULL)
 *
 * The file is compiled on both Linux and macOS because the macOS Makefile
 * includes all unix/ sources.  Platform-specific code is guarded by
 * #ifdef __APPLE__ / #else.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "../platform.h"

/* -------------------------------------------------------------------------
 * TLS includes – chosen at compile time per platform.
 * ---------------------------------------------------------------------- */
#ifdef __APPLE__
#  include <Security/SecureTransport.h>
#  include <Security/Security.h>
#elif defined(HAVE_OPENSSL)
#  include <openssl/ssl.h>
#  include <openssl/err.h>
#  include <openssl/x509v3.h>
#endif

/* =========================================================================
 * Subsystem lifecycle
 * ====================================================================== */

bool_t
axNetInit(void)
{
  /* POSIX sockets need no explicit initialisation. */
  return TRUE;
}

void
axNetShutdown(void)
{
  /* Nothing to clean up on POSIX. */
}

/* =========================================================================
 * Socket primitives
 * ====================================================================== */

int
axNetSocket(int af, int type)
{
  int domain = (af == AX_NET_AF_IPV6) ? AF_INET6 : AF_INET;
  int kind   = (type == AX_NET_SOCK_UDP) ? SOCK_DGRAM : SOCK_STREAM;
  int fd = socket(domain, kind, 0);
  if (fd == -1) {
    perror("axNetSocket");
    return -1;
  }
  return fd;
}

void
axNetClose(int sock)
{
  if (sock >= 0)
    close(sock);
}

bool_t
axNetSetNonBlocking(int sock, bool_t nonblocking)
{
  int flags = fcntl(sock, F_GETFL, 0);
  if (flags == -1) {
    perror("axNetSetNonBlocking: fcntl F_GETFL");
    return FALSE;
  }
  if (nonblocking)
    flags |= O_NONBLOCK;
  else
    flags &= ~O_NONBLOCK;
  if (fcntl(sock, F_SETFL, flags) == -1) {
    perror("axNetSetNonBlocking: fcntl F_SETFL");
    return FALSE;
  }
  return TRUE;
}

bool_t
axNetSetReuseAddr(int sock, bool_t reuse)
{
  int val = reuse ? 1 : 0;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) == -1) {
    perror("axNetSetReuseAddr");
    return FALSE;
  }
  return TRUE;
}

bool_t
axNetBind(int sock, uint16_t port)
{
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port        = htons(port);
  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    perror("axNetBind");
    return FALSE;
  }
  return TRUE;
}

bool_t
axNetListen(int sock, int backlog)
{
  if (listen(sock, backlog) == -1) {
    perror("axNetListen");
    return FALSE;
  }
  return TRUE;
}

int
axNetAccept(int sock)
{
  int fd = (int)accept(sock, NULL, NULL);
  if (fd == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
    perror("axNetAccept");
  return fd;
}

bool_t
axNetConnect(int sock, char const *host, uint16_t port)
{
  char port_str[8];
  snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);

  struct addrinfo hints, *res;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  int rc = getaddrinfo(host, port_str, &hints, &res);
  if (rc != 0) {
    fprintf(stderr, "axNetConnect: getaddrinfo(%s): %s\n",
            host, gai_strerror(rc));
    return FALSE;
  }

  bool_t ok = FALSE;
  for (struct addrinfo *p = res; p; p = p->ai_next) {
    if (connect(sock, p->ai_addr, p->ai_addrlen) == 0 ||
        errno == EINPROGRESS) {
      ok = TRUE;
      break;
    }
  }
  freeaddrinfo(res);

  if (!ok)
    perror("axNetConnect: connect");
  return ok;
}

int
axNetSend(int sock, void const *buf, int len)
{
  ssize_t n = send(sock, buf, (size_t)len, 0);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return 0;
    perror("axNetSend");
    return -1;
  }
  return (int)n;
}

int
axNetRecv(int sock, void *buf, int len)
{
  ssize_t n = recv(sock, buf, (size_t)len, 0);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return 0;
    perror("axNetRecv");
    return -1;
  }
  return (int)n;
}

bool_t
axNetWouldBlock(void)
{
  return errno == EAGAIN || errno == EWOULDBLOCK;
}

bool_t
axNetResolve(char const *host, char *out, int outlen)
{
  struct addrinfo hints, *res;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  int rc = getaddrinfo(host, NULL, &hints, &res);
  if (rc != 0) {
    fprintf(stderr, "axNetResolve: getaddrinfo(%s): %s\n",
            host, gai_strerror(rc));
    return FALSE;
  }

  bool_t ok = FALSE;
  for (struct addrinfo *p = res; p; p = p->ai_next) {
    void *addr_ptr;
    if (p->ai_family == AF_INET) {
      addr_ptr = &((struct sockaddr_in *)p->ai_addr)->sin_addr;
    } else if (p->ai_family == AF_INET6) {
      addr_ptr = &((struct sockaddr_in6 *)p->ai_addr)->sin6_addr;
    } else {
      continue;
    }
    if (inet_ntop(p->ai_family, addr_ptr, out, (socklen_t)outlen)) {
      ok = TRUE;
      break;
    }
  }
  freeaddrinfo(res);
  return ok;
}

int
axNetPoll(int sock, int events, int timeout_ms)
{
  struct pollfd pfd;
  pfd.fd      = sock;
  pfd.events  = 0;
  pfd.revents = 0;

  if (events & AX_NET_POLL_READ)  pfd.events |= POLLIN;
  if (events & AX_NET_POLL_WRITE) pfd.events |= POLLOUT;
  if (events & AX_NET_POLL_ERR)   pfd.events |= POLLERR;

  int rc = poll(&pfd, 1, timeout_ms);
  if (rc < 0) {
    perror("axNetPoll");
    return -1;
  }
  if (rc == 0)
    return 0; /* timeout */

  int ready = 0;
  if (pfd.revents & POLLIN)  ready |= AX_NET_POLL_READ;
  if (pfd.revents & POLLOUT) ready |= AX_NET_POLL_WRITE;
  if (pfd.revents & POLLERR) ready |= AX_NET_POLL_ERR;
  return ready;
}

/* =========================================================================
 * TLS – macOS: Secure Transport
 * ====================================================================== */
#ifdef __APPLE__

struct AXtlsctx
{
  SSLContextRef ssl;
  int           fd;
};

static OSStatus
st_read(SSLConnectionRef conn, void *data, size_t *len)
{
  int     fd = *(int const *)conn;
  ssize_t n  = read(fd, data, *len);
  if (n > 0)  { *len = (size_t)n; return noErr; }
  if (n == 0) { *len = 0; return errSSLClosedGraceful; }
  *len = 0;
  if (errno == EAGAIN || errno == EWOULDBLOCK) return errSSLWouldBlock;
  return errSecIO;
}

static OSStatus
st_write(SSLConnectionRef conn, void const *data, size_t *len)
{
  int     fd = *(int const *)conn;
  ssize_t n  = write(fd, data, *len);
  if (n >= 0) { *len = (size_t)n; return noErr; }
  *len = 0;
  if (errno == EAGAIN || errno == EWOULDBLOCK) return errSSLWouldBlock;
  return errSecIO;
}

AXtlsctx *
axTlsConnect(int sock, char const *hostname)
{
  AXtlsctx *ctx = (AXtlsctx *)malloc(sizeof(AXtlsctx));
  if (!ctx) return NULL;
  ctx->fd = sock;

  ctx->ssl = SSLCreateContext(kCFAllocatorDefault,
                              kSSLClientSide, kSSLStreamType);
  if (!ctx->ssl) {
    free(ctx);
    return NULL;
  }

  SSLSetIOFuncs(ctx->ssl, st_read, st_write);
  SSLSetConnection(ctx->ssl, (SSLConnectionRef)&ctx->fd);

  if (hostname && hostname[0])
    SSLSetPeerDomainName(ctx->ssl, hostname, strlen(hostname));

  OSStatus status;
  do {
    status = SSLHandshake(ctx->ssl);
  } while (status == errSSLWouldBlock);

  if (status != noErr) {
    fprintf(stderr, "axTlsConnect: SSLHandshake failed (%d)\n", (int)status);
    CFRelease(ctx->ssl);
    free(ctx);
    return NULL;
  }
  return ctx;
}

void
axTlsClose(AXtlsctx *ctx)
{
  if (!ctx) return;
  SSLClose(ctx->ssl);
  CFRelease(ctx->ssl);
  free(ctx);
}

int
axTlsSend(AXtlsctx *ctx, void const *buf, int len)
{
  size_t   processed = 0;
  OSStatus status    = SSLWrite(ctx->ssl, buf, (size_t)len, &processed);
  if (status == noErr || status == errSSLWouldBlock)
    return (int)processed;
  fprintf(stderr, "axTlsSend: SSLWrite failed (%d)\n", (int)status);
  return -1;
}

int
axTlsRecv(AXtlsctx *ctx, void *buf, int len)
{
  size_t   processed = 0;
  OSStatus status    = SSLRead(ctx->ssl, buf, (size_t)len, &processed);
  if (status == noErr || status == errSSLWouldBlock)
    return (int)processed;
  if (status == errSSLClosedGraceful || status == errSSLClosedNoNotify)
    return 0;
  fprintf(stderr, "axTlsRecv: SSLRead failed (%d)\n", (int)status);
  return -1;
}

/* =========================================================================
 * TLS – Linux: OpenSSL
 * ====================================================================== */
#elif defined(HAVE_OPENSSL)

struct AXtlsctx
{
  SSL_CTX *ssl_ctx;
  SSL     *ssl;
};

AXtlsctx *
axTlsConnect(int sock, char const *hostname)
{
  AXtlsctx *ctx = (AXtlsctx *)calloc(1, sizeof(AXtlsctx));
  if (!ctx) return NULL;

  ctx->ssl_ctx = SSL_CTX_new(TLS_client_method());
  if (!ctx->ssl_ctx) {
    free(ctx);
    return NULL;
  }

  /* Enable peer certificate verification against the default CA bundle. */
  SSL_CTX_set_verify(ctx->ssl_ctx, SSL_VERIFY_PEER, NULL);
  SSL_CTX_set_default_verify_paths(ctx->ssl_ctx);

  ctx->ssl = SSL_new(ctx->ssl_ctx);
  if (!ctx->ssl) {
    SSL_CTX_free(ctx->ssl_ctx);
    free(ctx);
    return NULL;
  }

  SSL_set_fd(ctx->ssl, sock);

  /* Server Name Indication (SNI). */
  if (hostname && hostname[0])
    SSL_set_tlsext_host_name(ctx->ssl, hostname);

  /* Hostname verification (OpenSSL >= 1.0.2). */
  if (hostname && hostname[0]) {
    X509_VERIFY_PARAM *param = SSL_get0_param(ctx->ssl);
    X509_VERIFY_PARAM_set_hostflags(param,
                                    X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
    X509_VERIFY_PARAM_set1_host(param, hostname, 0);
  }

  if (SSL_connect(ctx->ssl) != 1) {
    ERR_print_errors_fp(stderr);
    SSL_free(ctx->ssl);
    SSL_CTX_free(ctx->ssl_ctx);
    free(ctx);
    return NULL;
  }
  return ctx;
}

void
axTlsClose(AXtlsctx *ctx)
{
  if (!ctx) return;
  SSL_shutdown(ctx->ssl);
  SSL_free(ctx->ssl);
  SSL_CTX_free(ctx->ssl_ctx);
  free(ctx);
}

int
axTlsSend(AXtlsctx *ctx, void const *buf, int len)
{
  int n = SSL_write(ctx->ssl, buf, len);
  if (n <= 0) {
    ERR_print_errors_fp(stderr);
    return -1;
  }
  return n;
}

int
axTlsRecv(AXtlsctx *ctx, void *buf, int len)
{
  int n = SSL_read(ctx->ssl, buf, len);
  if (n < 0) {
    int err = SSL_get_error(ctx->ssl, n);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
      return 0;
    ERR_print_errors_fp(stderr);
    return -1;
  }
  return n;
}

/* =========================================================================
 * TLS – stub (Linux without OpenSSL)
 * ====================================================================== */
#else /* !__APPLE__ && !HAVE_OPENSSL */

struct AXtlsctx { int _unused; };

AXtlsctx *
axTlsConnect(int sock, char const *hostname)
{
  (void)sock;
  (void)hostname;
  fprintf(stderr, "axTlsConnect: TLS not available; "
          "rebuild with -DHAVE_OPENSSL\n");
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

#endif /* TLS backend */
