# Networking

The platform library provides a portable socket API (`axNet*`) and an opaque
TLS layer (`axTls*`) that work on Windows, macOS, Linux, and QNX without any
`#ifdef` guards in your code.

On **Windows** the backend uses Winsock2 and Schannel.  On **macOS** it uses
BSD sockets and Secure Transport.  On **Linux** it uses POSIX sockets and
optionally OpenSSL (pass `-DHAVE_OPENSSL -lssl -lcrypto` when building).
On **WebGL** plain sockets are not available; `axTlsConnect` always returns
`NULL`.

---

## Initialisation and shutdown

```c
axNetInit();      /* must be called before any axNet* or axTls* function */

/* … do all networking … */

axNetShutdown();  /* releases Winsock on Windows; no-op elsewhere */
```

Call `axNetInit()` once from the main thread before spawning any worker
threads.  `axNetShutdown()` should be the last networking call in the
program.

---

## TCP client — plain socket

```c
#include "platform.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    axNetInit();

    int sock = axNetSocket(AX_NET_AF_IPV4, AX_NET_SOCK_TCP);
    if (sock < 0) { fputs("socket failed\n", stderr); return 1; }

    if (!axNetConnect(sock, "example.com", 80)) {
        fputs("connect failed\n", stderr);
        axNetClose(sock);
        axNetShutdown();
        return 1;
    }

    const char *req = "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n";
    axNetSend(sock, req, (int)strlen(req));

    char buf[4096];
    int n;
    while ((n = axNetRecv(sock, buf, (int)sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        fputs(buf, stdout);
    }

    axNetClose(sock);
    axNetShutdown();
    return 0;
}
```

---

## HTTPS client — TLS over TCP

```c
#include "platform.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    axNetInit();

    int sock = axNetSocket(AX_NET_AF_IPV4, AX_NET_SOCK_TCP);
    if (sock < 0) { fputs("socket failed\n", stderr); return 1; }

    if (!axNetConnect(sock, "example.com", 443)) {
        fputs("connect failed\n", stderr);
        axNetClose(sock);
        axNetShutdown();
        return 1;
    }

    /* Upgrade to TLS — hostname is used for SNI and certificate verification */
    AXtlsctx *tls = axTlsConnect(sock, "example.com");
    if (!tls) {
        fputs("TLS handshake failed\n", stderr);
        axNetClose(sock);
        axNetShutdown();
        return 1;
    }

    const char *req = "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n";
    axTlsSend(tls, req, (int)strlen(req));

    char buf[4096];
    int n;
    while ((n = axTlsRecv(tls, buf, (int)sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        fputs(buf, stdout);
    }

    axTlsClose(tls);    /* sends TLS close_notify */
    axNetClose(sock);   /* then close the underlying socket */
    axNetShutdown();
    return 0;
}
```

> **Note:** On Linux, TLS requires OpenSSL.  Build with
> `-DHAVE_OPENSSL -lssl -lcrypto` or `axTlsConnect` will return `NULL`.

---

## TCP server — accept loop

```c
#include "platform.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    axNetInit();

    int server = axNetSocket(AX_NET_AF_IPV4, AX_NET_SOCK_TCP);
    if (server < 0) { fputs("socket failed\n", stderr); return 1; }
    axNetSetReuseAddr(server, TRUE);   /* allow immediate rebind after restart */
    if (!axNetBind(server, NULL, 8080)) { fputs("bind failed\n", stderr); return 1; }
    axNetListen(server, 8);

    printf("listening on :8080\n");

    for (;;) {
        int client = axNetAccept(server);
        if (client < 0) continue;

        char req[1024];
        int n = axNetRecv(client, req, (int)sizeof(req) - 1);
        if (n > 0) {
            req[n] = '\0';
            /* Echo the first line back */
            const char *resp =
                "HTTP/1.0 200 OK\r\n"
                "Content-Type: text/plain\r\n\r\n"
                "Hello from axNet!\n";
            axNetSend(client, resp, (int)strlen(resp));
        }
        axNetClose(client);
    }

    axNetClose(server);
    axNetShutdown();
    return 0;
}
```

---

## Non-blocking I/O and polling

Switch a socket to non-blocking mode and use `axNetPoll` to wait for
readiness before reading or writing:

```c
axNetSetNonBlocking(sock, TRUE);

/* Wait up to 5 seconds for data to arrive */
int ready = axNetPoll(sock, AX_NET_POLL_READ, 5000);
if (ready < 0) {
    fputs("poll error\n", stderr);
} else if (ready == 0) {
    fputs("timeout — no data in 5 s\n", stderr);
} else {
    char buf[1024];
    int n = axNetRecv(sock, buf, (int)sizeof(buf));
    if (n < 0 && axNetWouldBlock()) {
        /* spurious wakeup — try again later */
    } else if (n <= 0) {
        /* connection closed or hard error */
    } else {
        /* process buf[0..n-1] */
    }
}
```

> **Note:** Call `axNetWouldBlock()` *immediately* after a failed
> `axNetSend` or `axNetRecv` — it reads thread-local state (`errno` on
> POSIX, `WSAGetLastError()` on Windows) that any subsequent network call
> may overwrite.

### Non-blocking connect

For non-blocking sockets `axNetConnect` may return `FALSE` while the
OS-level connect is still in progress.  Poll for writability to detect
completion:

```c
axNetSetNonBlocking(sock, TRUE);
axNetConnect(sock, "example.com", 80);   /* may return FALSE — that's OK */

/* Wait up to 10 s for the connection to complete */
int ready = axNetPoll(sock, AX_NET_POLL_WRITE, 10000);
if (ready <= 0) {
    fputs("connect timed out\n", stderr);
    axNetClose(sock);
}
/* socket is now connected */
```

---

## DNS resolution

Resolve a hostname to a printable IP address string without opening a
socket:

```c
char ip[64];
if (axNetResolve("example.com", ip, (int)sizeof(ip))) {
    printf("example.com → %s\n", ip);
} else {
    fputs("resolution failed\n", stderr);
}
```

---

## API quick reference

### Lifecycle

| Function | Description |
|----------|-------------|
| `axNetInit()` | Initialise the networking subsystem (call once). |
| `axNetShutdown()` | Release networking resources at exit. |

### Sockets

| Function | Description |
|----------|-------------|
| `axNetSocket(af, type)` | Create a socket. `af`: `AX_NET_AF_IPV4` / `AX_NET_AF_IPV6`. `type`: `AX_NET_SOCK_TCP` / `AX_NET_SOCK_UDP`. |
| `axNetClose(sock)` | Close the socket. Passing -1 is safe. |
| `axNetSetNonBlocking(sock, nb)` | Toggle non-blocking mode. |
| `axNetSetReuseAddr(sock, reuse)` | Enable `SO_REUSEADDR` for servers. |

### Client

| Function | Description |
|----------|-------------|
| `axNetConnect(sock, host, port)` | Resolve `host` and connect. |
| `axNetSend(sock, buf, len)` | Send bytes; returns bytes sent or -1. |
| `axNetRecv(sock, buf, len)` | Receive bytes; returns bytes received, 0 on close, -1 on error. |
| `axNetWouldBlock()` | `TRUE` if the last send/recv would have blocked (call immediately after). |

### Server

| Function | Description |
|----------|-------------|
| `axNetBind(sock, host, port)` | Bind to a numeric local address, or all interfaces when `host` is `NULL`. |
| `axNetListen(sock, backlog)` | Mark socket as passive. |
| `axNetAccept(sock)` | Accept an incoming connection; returns new socket or -1. |

### Utilities

| Function | Description |
|----------|-------------|
| `axNetResolve(host, out, outlen)` | Resolve hostname to IP string. |
| `axNetPoll(sock, events, timeout_ms)` | Wait for I/O readiness. `events`: bitmask of `AX_NET_POLL_READ`, `AX_NET_POLL_WRITE`, `AX_NET_POLL_ERR`. Returns ready flags, 0 on timeout, -1 on error. |

### TLS

| Function | Description |
|----------|-------------|
| `axTlsConnect(sock, hostname)` | TLS handshake on an already-connected socket. Returns opaque context or `NULL`. |
| `axTlsClose(ctx)` | Send close_notify and free the TLS context. |
| `axTlsSend(ctx, buf, len)` | Send bytes through TLS. |
| `axTlsRecv(ctx, buf, len)` | Receive bytes through TLS. |

---

## Platform support

| Feature | Windows | macOS | Linux | QNX | WebGL |
|---------|:-------:|:-----:|:-----:|:---:|:-----:|
| TCP / UDP sockets | ✓ | ✓ | ✓ | ✓ | — |
| IPv4 | ✓ | ✓ | ✓ | ✓ | — |
| IPv6 | ✓ | ✓ | ✓ | ✓ | — |
| Non-blocking I/O | ✓ | ✓ | ✓ | ✓ | — |
| TLS | ✓ Schannel | ✓ Secure Transport | ✓ OpenSSL¹ | — | — |

¹ Requires `-DHAVE_OPENSSL -lssl -lcrypto` at build time.

On **WebGL** the browser sandbox does not expose raw sockets.  Use the
browser's `fetch` / `WebSocket` APIs from JavaScript instead, or integrate
a higher-level HTTP library that is compiled with Emscripten.
