/*
 * windows_net.c – Windows networking implementation.
 *
 * Provides the axNet* and axTls* API using:
 *   - Winsock2 for socket and DNS operations
 *   - Schannel (Security Support Provider Interface) for TLS
 *
 * Link requirements (already in the Makefile):
 *   -lws2_32    Winsock2
 *   -lsecur32   Schannel / SSPI
 */

/* Winsock2 must precede windows.h to avoid redefinition conflicts. */
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

/* Schannel / SSPI */
#define SECURITY_WIN32
#include <security.h>
#include <schannel.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../platform.h"

/* =========================================================================
 * Internal helpers
 * ====================================================================== */

/* Maximum TLS record payload size (RFC 5246 §6.2.1). */
#define TLS_MAX_RECORD 16384
/* Buffer large enough for several encrypted records. */
#define TLS_RECV_BUF   (TLS_MAX_RECORD * 4)

static bool_t winsock_initialised = FALSE;

/* =========================================================================
 * Subsystem lifecycle
 * ====================================================================== */

bool_t
axNetInit(void)
{
  if (winsock_initialised)
    return TRUE;
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    fprintf(stderr, "axNetInit: WSAStartup failed: %d\n", WSAGetLastError());
    return FALSE;
  }
  winsock_initialised = TRUE;
  return TRUE;
}

void
axNetShutdown(void)
{
  if (winsock_initialised) {
    WSACleanup();
    winsock_initialised = FALSE;
  }
}

/* =========================================================================
 * Socket primitives
 * ====================================================================== */

int
axNetSocket(int af, int type)
{
  int domain = (af == AX_NET_AF_IPV6) ? AF_INET6 : AF_INET;
  int kind   = (type == AX_NET_SOCK_UDP) ? SOCK_DGRAM : SOCK_STREAM;
  SOCKET s = socket(domain, kind, 0);
  if (s == INVALID_SOCKET) {
    fprintf(stderr, "axNetSocket: %d\n", WSAGetLastError());
    return -1;
  }
  return (int)s;
}

void
axNetClose(int sock)
{
  if (sock >= 0)
    closesocket((SOCKET)sock);
}

bool_t
axNetSetNonBlocking(int sock, bool_t nonblocking)
{
  u_long mode = nonblocking ? 1 : 0;
  if (ioctlsocket((SOCKET)sock, FIONBIO, &mode) == SOCKET_ERROR) {
    fprintf(stderr, "axNetSetNonBlocking: %d\n", WSAGetLastError());
    return FALSE;
  }
  return TRUE;
}

bool_t
axNetSetReuseAddr(int sock, bool_t reuse)
{
  int val = reuse ? 1 : 0;
  if (setsockopt((SOCKET)sock, SOL_SOCKET, SO_REUSEADDR,
                 (char const *)&val, sizeof(val)) == SOCKET_ERROR) {
    fprintf(stderr, "axNetSetReuseAddr: %d\n", WSAGetLastError());
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
  if (bind((SOCKET)sock, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
    fprintf(stderr, "axNetBind: %d\n", WSAGetLastError());
    return FALSE;
  }
  return TRUE;
}

bool_t
axNetListen(int sock, int backlog)
{
  if (listen((SOCKET)sock, backlog) == SOCKET_ERROR) {
    fprintf(stderr, "axNetListen: %d\n", WSAGetLastError());
    return FALSE;
  }
  return TRUE;
}

int
axNetAccept(int sock)
{
  SOCKET client = accept((SOCKET)sock, NULL, NULL);
  if (client == INVALID_SOCKET) {
    int err = WSAGetLastError();
    if (err != WSAEWOULDBLOCK)
      fprintf(stderr, "axNetAccept: %d\n", err);
    return -1;
  }
  return (int)client;
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

  if (getaddrinfo(host, port_str, &hints, &res) != 0) {
    fprintf(stderr, "axNetConnect: getaddrinfo(%s) failed: %d\n",
            host, WSAGetLastError());
    return FALSE;
  }

  bool_t ok = FALSE;
  for (struct addrinfo *p = res; p; p = p->ai_next) {
    int rc = connect((SOCKET)sock, p->ai_addr, (int)p->ai_addrlen);
    if (rc == 0 || WSAGetLastError() == WSAEWOULDBLOCK) {
      ok = TRUE;
      break;
    }
  }
  freeaddrinfo(res);

  if (!ok)
    fprintf(stderr, "axNetConnect: connect failed: %d\n", WSAGetLastError());
  return ok;
}

int
axNetSend(int sock, void const *buf, int len)
{
  int n = send((SOCKET)sock, (char const *)buf, len, 0);
  if (n == SOCKET_ERROR) {
    if (WSAGetLastError() == WSAEWOULDBLOCK)
      return 0;
    fprintf(stderr, "axNetSend: %d\n", WSAGetLastError());
    return -1;
  }
  return n;
}

int
axNetRecv(int sock, void *buf, int len)
{
  int n = recv((SOCKET)sock, (char *)buf, len, 0);
  if (n == SOCKET_ERROR) {
    if (WSAGetLastError() == WSAEWOULDBLOCK)
      return 0;
    fprintf(stderr, "axNetRecv: %d\n", WSAGetLastError());
    return -1;
  }
  return n;
}

bool_t
axNetWouldBlock(void)
{
  return WSAGetLastError() == WSAEWOULDBLOCK ? TRUE : FALSE;
}

bool_t
axNetResolve(char const *host, char *out, int outlen)
{
  struct addrinfo hints, *res;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(host, NULL, &hints, &res) != 0) {
    fprintf(stderr, "axNetResolve: getaddrinfo(%s) failed: %d\n",
            host, WSAGetLastError());
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
    if (InetNtopA(p->ai_family, addr_ptr, out, (size_t)outlen)) {
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
  fd_set rd, wr, ex;
  FD_ZERO(&rd); FD_ZERO(&wr); FD_ZERO(&ex);
  if (events & AX_NET_POLL_READ)  FD_SET((SOCKET)sock, &rd);
  if (events & AX_NET_POLL_WRITE) FD_SET((SOCKET)sock, &wr);
  if (events & AX_NET_POLL_ERR)   FD_SET((SOCKET)sock, &ex);

  struct timeval tv;
  tv.tv_sec  = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;

  int rc = select(0, &rd, &wr, &ex, &tv);
  if (rc == SOCKET_ERROR) {
    fprintf(stderr, "axNetPoll: select failed: %d\n", WSAGetLastError());
    return -1;
  }
  if (rc == 0) return 0;

  int ready = 0;
  if (FD_ISSET((SOCKET)sock, &rd)) ready |= AX_NET_POLL_READ;
  if (FD_ISSET((SOCKET)sock, &wr)) ready |= AX_NET_POLL_WRITE;
  if (FD_ISSET((SOCKET)sock, &ex)) ready |= AX_NET_POLL_ERR;
  return ready;
}

/* =========================================================================
 * TLS – Schannel
 * ====================================================================== */

struct AXtlsctx
{
  CredHandle h_cred;
  CtxtHandle h_ctx;
  int        fd;
  SecPkgContext_StreamSizes stream_sizes;

  /* Encrypted bytes received from the peer, not yet decrypted. */
  unsigned char enc_buf[TLS_RECV_BUF];
  int enc_len;

  /* Decrypted bytes not yet returned to the caller. */
  unsigned char dec_buf[TLS_MAX_RECORD];
  int dec_offset;
  int dec_len;
};

/* Perform the Schannel client handshake. Returns TRUE on success. */
static bool_t
schannel_handshake(AXtlsctx *tls, char const *hostname)
{
  SECURITY_STATUS ss;
  ULONG req_flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
                    ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY |
                    ISC_REQ_STREAM;
  ULONG attribs = 0;

  /* Wide-char hostname for Schannel SNI. */
  wchar_t w_host[256] = {0};
  MultiByteToWideChar(CP_UTF8, 0, hostname ? hostname : "",
                      -1, w_host, 256);

  SecBufferDesc out_desc;
  SecBuffer     out_buf;
  out_buf.cbBuffer   = 0;
  out_buf.BufferType = SECBUFFER_TOKEN;
  out_buf.pvBuffer   = NULL;
  out_desc.ulVersion = SECBUFFER_VERSION;
  out_desc.cBuffers  = 1;
  out_desc.pBuffers  = &out_buf;

  /* First call – no input yet, generates ClientHello. */
  ss = InitializeSecurityContextW(
    &tls->h_cred, NULL, w_host, req_flags, 0,
    SECURITY_NETWORK_DREP, NULL, 0,
    &tls->h_ctx, &out_desc, &attribs, NULL);

  if (ss != SEC_I_CONTINUE_NEEDED) {
    fprintf(stderr, "schannel_handshake: ISC (1) failed: 0x%lx\n",
            (unsigned long)ss);
    return FALSE;
  }

  /* Send ClientHello */
  if (out_buf.pvBuffer && out_buf.cbBuffer > 0) {
    send((SOCKET)tls->fd, (char *)out_buf.pvBuffer,
         (int)out_buf.cbBuffer, 0);
    FreeContextBuffer(out_buf.pvBuffer);
    out_buf.pvBuffer = NULL;
  }

  /* Handshake loop */
  unsigned char tmp[TLS_RECV_BUF];
  int tmp_len = 0;

  while (ss == SEC_I_CONTINUE_NEEDED ||
         ss == SEC_E_INCOMPLETE_MESSAGE) {
    int n = recv((SOCKET)tls->fd,
                 (char *)tmp + tmp_len,
                 (int)(sizeof(tmp) - (size_t)tmp_len), 0);
    if (n <= 0) {
      fprintf(stderr, "schannel_handshake: recv failed: %d\n",
              WSAGetLastError());
      return FALSE;
    }
    tmp_len += n;

    SecBuffer     in_bufs[2];
    SecBufferDesc in_desc;
    in_bufs[0].cbBuffer   = (unsigned long)tmp_len;
    in_bufs[0].BufferType = SECBUFFER_TOKEN;
    in_bufs[0].pvBuffer   = tmp;
    in_bufs[1].cbBuffer   = 0;
    in_bufs[1].BufferType = SECBUFFER_EMPTY;
    in_bufs[1].pvBuffer   = NULL;
    in_desc.ulVersion      = SECBUFFER_VERSION;
    in_desc.cBuffers       = 2;
    in_desc.pBuffers       = in_bufs;

    out_buf.cbBuffer   = 0;
    out_buf.BufferType = SECBUFFER_TOKEN;
    out_buf.pvBuffer   = NULL;
    out_desc.ulVersion = SECBUFFER_VERSION;
    out_desc.cBuffers  = 1;
    out_desc.pBuffers  = &out_buf;

    ss = InitializeSecurityContextW(
      &tls->h_cred, &tls->h_ctx, w_host, req_flags, 0,
      SECURITY_NETWORK_DREP, &in_desc, 0,
      NULL, &out_desc, &attribs, NULL);

    if (out_buf.pvBuffer && out_buf.cbBuffer > 0) {
      send((SOCKET)tls->fd, (char *)out_buf.pvBuffer,
           (int)out_buf.cbBuffer, 0);
      FreeContextBuffer(out_buf.pvBuffer);
      out_buf.pvBuffer = NULL;
    }

    /* Retain any extra (unprocessed) bytes for the next iteration. */
    if (in_bufs[1].BufferType == SECBUFFER_EXTRA &&
        in_bufs[1].cbBuffer > 0) {
      int extra = (int)in_bufs[1].cbBuffer;
      memmove(tmp, tmp + (tmp_len - extra), (size_t)extra);
      tmp_len = extra;
    } else if (ss != SEC_E_INCOMPLETE_MESSAGE) {
      tmp_len = 0;
    }
  }

  if (ss != SEC_E_OK) {
    fprintf(stderr, "schannel_handshake: ISC failed: 0x%lx\n",
            (unsigned long)ss);
    return FALSE;
  }

  /* Save any post-handshake application data. */
  if (tmp_len > 0 && tmp_len <= (int)sizeof(tls->enc_buf)) {
    memcpy(tls->enc_buf, tmp, (size_t)tmp_len);
    tls->enc_len = tmp_len;
  }

  QueryContextAttributes(&tls->h_ctx, SECPKG_ATTR_STREAM_SIZES,
                         &tls->stream_sizes);
  return TRUE;
}

AXtlsctx *
axTlsConnect(int sock, char const *hostname)
{
  AXtlsctx *ctx = (AXtlsctx *)calloc(1, sizeof(AXtlsctx));
  if (!ctx) return NULL;
  ctx->fd = sock;

  /* Set up credentials (outbound TLS client, default settings). */
  SCHANNEL_CRED cred;
  memset(&cred, 0, sizeof(cred));
  cred.dwVersion = SCHANNEL_CRED_VERSION;
  cred.grbitEnabledProtocols = 0; /* let Schannel pick the best protocol */

  SECURITY_STATUS ss = AcquireCredentialsHandleW(
    NULL, UNISP_NAME_W, SECPKG_CRED_OUTBOUND,
    NULL, &cred, NULL, NULL, &ctx->h_cred, NULL);

  if (ss != SEC_E_OK) {
    fprintf(stderr, "axTlsConnect: AcquireCredentialsHandle failed: "
            "0x%lx\n", (unsigned long)ss);
    free(ctx);
    return NULL;
  }

  if (!schannel_handshake(ctx, hostname)) {
    FreeCredentialsHandle(&ctx->h_cred);
    free(ctx);
    return NULL;
  }
  return ctx;
}

void
axTlsClose(AXtlsctx *ctx)
{
  if (!ctx) return;

  /* Send TLS close_notify. */
  DWORD type = SCHANNEL_SHUTDOWN;
  SecBuffer     sb;
  SecBufferDesc sbd;
  sb.cbBuffer   = sizeof(type);
  sb.BufferType = SECBUFFER_TOKEN;
  sb.pvBuffer   = &type;
  sbd.ulVersion = SECBUFFER_VERSION;
  sbd.cBuffers  = 1;
  sbd.pBuffers  = &sb;
  ApplyControlToken(&ctx->h_ctx, &sbd);

  SecBuffer     out_buf;
  SecBufferDesc out_desc;
  out_buf.cbBuffer   = 0;
  out_buf.BufferType = SECBUFFER_TOKEN;
  out_buf.pvBuffer   = NULL;
  out_desc.ulVersion = SECBUFFER_VERSION;
  out_desc.cBuffers  = 1;
  out_desc.pBuffers  = &out_buf;

  ULONG attribs = 0;
  InitializeSecurityContextW(&ctx->h_cred, &ctx->h_ctx, NULL,
    ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM, 0,
    SECURITY_NETWORK_DREP, NULL, 0,
    NULL, &out_desc, &attribs, NULL);

  if (out_buf.pvBuffer && out_buf.cbBuffer > 0) {
    send((SOCKET)ctx->fd, (char *)out_buf.pvBuffer,
         (int)out_buf.cbBuffer, 0);
    FreeContextBuffer(out_buf.pvBuffer);
  }

  DeleteSecurityContext(&ctx->h_ctx);
  FreeCredentialsHandle(&ctx->h_cred);
  free(ctx);
}

int
axTlsSend(AXtlsctx *ctx, void const *buf, int len)
{
  /* Respect the maximum plaintext size per record. */
  int max_plain = (int)ctx->stream_sizes.cbMaximumMessage;
  if (len > max_plain) len = max_plain;

  int total = (int)(ctx->stream_sizes.cbHeader +
                    (unsigned)len +
                    ctx->stream_sizes.cbTrailer);
  unsigned char *data = (unsigned char *)malloc((size_t)total);
  if (!data) return -1;

  memcpy(data + ctx->stream_sizes.cbHeader, buf, (size_t)len);

  SecBuffer     bufs[4];
  SecBufferDesc msg;
  bufs[0].cbBuffer   = ctx->stream_sizes.cbHeader;
  bufs[0].BufferType = SECBUFFER_STREAM_HEADER;
  bufs[0].pvBuffer   = data;
  bufs[1].cbBuffer   = (unsigned long)len;
  bufs[1].BufferType = SECBUFFER_DATA;
  bufs[1].pvBuffer   = data + ctx->stream_sizes.cbHeader;
  bufs[2].cbBuffer   = ctx->stream_sizes.cbTrailer;
  bufs[2].BufferType = SECBUFFER_STREAM_TRAILER;
  bufs[2].pvBuffer   = data + ctx->stream_sizes.cbHeader + len;
  bufs[3].cbBuffer   = 0;
  bufs[3].BufferType = SECBUFFER_EMPTY;
  bufs[3].pvBuffer   = NULL;
  msg.ulVersion      = SECBUFFER_VERSION;
  msg.cBuffers       = 4;
  msg.pBuffers       = bufs;

  SECURITY_STATUS ss = EncryptMessage(&ctx->h_ctx, 0, &msg, 0);
  if (ss != SEC_E_OK) {
    fprintf(stderr, "axTlsSend: EncryptMessage failed: 0x%lx\n",
            (unsigned long)ss);
    free(data);
    return -1;
  }

  int enc_size = (int)(bufs[0].cbBuffer + bufs[1].cbBuffer +
                       bufs[2].cbBuffer);
  int sent = send((SOCKET)ctx->fd, (char *)data, enc_size, 0);
  free(data);
  return (sent > 0) ? len : -1;
}

int
axTlsRecv(AXtlsctx *ctx, void *buf, int len)
{
  /* Return any already-decrypted data first. */
  if (ctx->dec_len > 0) {
    int n = ctx->dec_len < len ? ctx->dec_len : len;
    memcpy(buf, ctx->dec_buf + ctx->dec_offset, (size_t)n);
    ctx->dec_offset += n;
    ctx->dec_len    -= n;
    if (ctx->dec_len == 0) ctx->dec_offset = 0;
    return n;
  }

  /* Read more encrypted data into the accumulation buffer. */
  int space = (int)sizeof(ctx->enc_buf) - ctx->enc_len;
  if (space > 0) {
    int n = recv((SOCKET)ctx->fd,
                 (char *)ctx->enc_buf + ctx->enc_len, space, 0);
    if (n <= 0) {
      if (n == 0) return 0;
      if (WSAGetLastError() == WSAEWOULDBLOCK) return 0;
      fprintf(stderr, "axTlsRecv: recv failed: %d\n", WSAGetLastError());
      return -1;
    }
    ctx->enc_len += n;
  }

  /* Attempt decryption. */
  SecBuffer     bufs[4];
  SecBufferDesc msg;
  bufs[0].cbBuffer   = (unsigned long)ctx->enc_len;
  bufs[0].BufferType = SECBUFFER_DATA;
  bufs[0].pvBuffer   = ctx->enc_buf;
  bufs[1].cbBuffer   = 0; bufs[1].BufferType = SECBUFFER_EMPTY;
  bufs[1].pvBuffer   = NULL;
  bufs[2].cbBuffer   = 0; bufs[2].BufferType = SECBUFFER_EMPTY;
  bufs[2].pvBuffer   = NULL;
  bufs[3].cbBuffer   = 0; bufs[3].BufferType = SECBUFFER_EMPTY;
  bufs[3].pvBuffer   = NULL;
  msg.ulVersion      = SECBUFFER_VERSION;
  msg.cBuffers       = 4;
  msg.pBuffers       = bufs;

  SECURITY_STATUS ss = DecryptMessage(&ctx->h_ctx, &msg, 0, NULL);

  if (ss == SEC_E_INCOMPLETE_MESSAGE)
    return 0; /* need more data */

  if (ss == SEC_I_CONTEXT_EXPIRED || ss == SEC_I_RENEGOTIATE)
    return 0; /* orderly shutdown or renegotiation */

  if (ss != SEC_E_OK) {
    fprintf(stderr, "axTlsRecv: DecryptMessage failed: 0x%lx\n",
            (unsigned long)ss);
    return -1;
  }

  /* Find decrypted data and any leftover encrypted bytes. */
  unsigned char *dec_ptr  = NULL;
  int            dec_size = 0;
  unsigned char *extra    = NULL;
  int            extra_sz = 0;

  for (int i = 0; i < 4; i++) {
    if (bufs[i].BufferType == SECBUFFER_DATA && bufs[i].pvBuffer) {
      dec_ptr  = (unsigned char *)bufs[i].pvBuffer;
      dec_size = (int)bufs[i].cbBuffer;
    }
    if (bufs[i].BufferType == SECBUFFER_EXTRA && bufs[i].pvBuffer) {
      extra    = (unsigned char *)bufs[i].pvBuffer;
      extra_sz = (int)bufs[i].cbBuffer;
    }
  }

  /* Copy decrypted data, saving any overflow for future calls. */
  int out = dec_size < len ? dec_size : len;
  if (dec_ptr && out > 0) memcpy(buf, dec_ptr, (size_t)out);

  if (dec_size > out && dec_ptr) {
    int leftover = dec_size - out;
    if (leftover <= (int)sizeof(ctx->dec_buf)) {
      memcpy(ctx->dec_buf, dec_ptr + out, (size_t)leftover);
      ctx->dec_offset = 0;
      ctx->dec_len    = leftover;
    }
  }

  /* Preserve leftover encrypted bytes. */
  if (extra && extra_sz > 0 && extra_sz <= (int)sizeof(ctx->enc_buf)) {
    memmove(ctx->enc_buf, extra, (size_t)extra_sz);
    ctx->enc_len = extra_sz;
  } else {
    ctx->enc_len = 0;
  }

  return out;
}
