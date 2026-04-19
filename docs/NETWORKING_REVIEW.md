# Multiplatform Networking Implementation Review

**Date:** April 19, 2026  
**Scope:** `platform/unix/unix_net.c` and `platform/windows/windows_net.c`  
**Architecture:** Dual-implementation abstraction layer over POSIX and Windows socket APIs

---

## Executive Summary

The networking subsystem is a **well-designed abstraction layer** that provides a uniform `axNet*` / `axTls*` API across POSIX (macOS, Linux) and Windows. The implementation is **clean, maintainable, and follows the layered architecture principle**. However, there are several **minor improvements and edge cases** worth addressing for production robustness.

---

## Architecture Overview

### Design Pattern: Transport-Oriented Abstraction

The API is deliberately **low-level and transport-focused**:
- Exposes raw socket descriptors (`int sock`)
- Provides DNS resolution, polling, and TLS wrapping
- Higher-level protocols (HTTP, etc.) build on top
- No connection pooling, keep-alive, or retry logic at this layer

**This is correct.** It follows the POSIX networking philosophy and avoids over-engineering.

### Layered Implementation

```
┌─────────────────────────────────────┐
│  User code (HTTP client, etc.)      │
├─────────────────────────────────────┤
│  axNet* / axTls* API (platform.h)   │
├──────────────────┬──────────────────┤
│ unix_net.c       │ windows_net.c    │
│ ├─ POSIX sockets │ ├─ Winsock2      │
│ ├─ Secure Trp    │ ├─ Schannel      │
│ └─ OpenSSL opt.  │ └─ SSPI          │
└──────────────────┴──────────────────┘
```

Both implementations are **parallel, isolated, and platform-idiomatic**—exactly right.

---

## Detailed Analysis

### 1. Socket Primitives (Core Functionality) ✅

**Strengths:**

- **Consistent signature mapping:** Both platforms map to the same abstract constants (`AX_NET_AF_*`, `AX_NET_SOCK_*`).
- **Safe on error:** Both return -1 on failure; Windows uses `INVALID_SOCKET` check internally but converts to int(-1).
- **Non-blocking I/O:** Both support `axNetSetNonBlocking()` correctly.
  - POSIX: `fcntl(F_SETFL, O_NONBLOCK)` ✅
  - Windows: `ioctlsocket(FIONBIO)` ✅

**Issue: Socket Descriptor Safety in axNetClose()**

```c
// unix_net.c & windows_net.c
void axNetClose(int sock) {
  if (sock >= 0)
    close(sock);  // or closesocket(sock) on Windows
}
```

**Problem:** The check `sock >= 0` is defensive but incomplete.
- On POSIX, closing an already-closed FD is an error (may return `EBADF`) — the function silently ignores this.
- Repeated `axNetClose(sock)` calls will eventually close a valid socket of a later connection.

**Recommendation:**

```c
// More robust (optional – depends on API contract):
void axNetClose(int sock) {
  if (sock >= 0) {
    close(sock);  // errno is set on failure, but caller can't see it
    // Consider: return bool_t to signal real errors
  }
}
```

**Assessment:** Low-risk issue in practice (code should track open sockets), but worth documenting the expected caller responsibility.

---

### 2. Name Resolution (getaddrinfo) ✅

Both implementations correctly use `getaddrinfo()` for **hostname and numeric address resolution**, supporting both IPv4 and IPv6.

**Strengths:**

- **IPv4/IPv6 agnostic:** `hints.ai_family = AF_UNSPEC` lets the OS choose.
- **Error reporting:** Both print diagnostics to stderr.
- **Loop over results:** Both iterate `res->ai_next` to try alternative addresses.

**Minor Issue: Inconsistent Error Reporting in axNetConnect**

```c
// unix_net.c
if (getaddrinfo(host, port_str, &hints, &res) != 0) {
  fprintf(stderr, "axNetConnect: getaddrinfo(%s): %s\n",
          host, gai_strerror(rc));  // ← good: human-readable
  return FALSE;
}

// windows_net.c
if (getaddrinfo(host, port_str, &hints, &res) != 0) {
  fprintf(stderr, "axNetConnect: getaddrinfo(%s) failed: %d\n",
          host, WSAGetLastError());  // ← not ideal: prints Winsock error, not getaddrinfo error
  return FALSE;
}
```

The Windows version calls `WSAGetLastError()` **after `getaddrinfo()`**, which may have cleared the error code. On Windows, `getaddrinfo()` returns `WSAHOST_NOT_FOUND` (same as `gai_strerror()` on POSIX) **but the error is from Winsock, not POSIX errno.**

**Recommendation:** Use `gai_strerror(WSAGetLastError())` on Windows too (if available in SDK), or accept the inconsistency as a known limitation.

---

### 3. Non-Blocking I/O & Polling 🟡

#### axNetSetNonBlocking() ✅

Both platforms handle this correctly.

#### axNetWouldBlock() — Platform Mismatch

```c
// unix_net.c
bool_t axNetWouldBlock(void) {
  return errno == EAGAIN || errno == EWOULDBLOCK;
}

// windows_net.c
bool_t axNetWouldBlock(void) {
  return WSAGetLastError() == WSAEWOULDBLOCK ? TRUE : FALSE;
}
```

**Problem:**
- `errno` is thread-local on POSIX; after the calling code performs any I/O, `errno` may be overwritten.
- `WSAGetLastError()` is thread-local on Windows; same issue.
- **Both implementations assume the caller checks `axNetWouldBlock()` immediately after a failed `axNetSend()` / `axNetRecv()`.** If any other call happens in between, the check is unreliable.

**Real-world impact:** Low if the caller is single-threaded and checks immediately; **critical if multi-threaded or if multiple operations occur before checking.**

**Recommended Mitigation (no code change needed):** Document clearly in `platform.h`:

```c
/**
 * @brief Test whether the last send/receive returned due to EAGAIN/EWOULDBLOCK.
 *
 * WARNING: This reads errno (POSIX) or WSAGetLastError() (Windows), which is
 * thread-local and volatile. Call this immediately after axNetSend/axNetRecv
 * returns a negative or zero value — do not perform any other I/O in between.
 *
 * @return `TRUE` if the last network call would have blocked, `FALSE` otherwise.
 */
AX_API bool_t
axNetWouldBlock(void);
```

Alternatively, **return the error status from axNetSend/axNetRecv themselves**:

```c
// Better API design (breaking change):
int axNetSend(int sock, ..., int *out_errno);  // returns bytes sent; out_errno gets errno/WSAGetLastError()
```

#### axNetPoll() ✅ (with minor caveats)

**POSIX (`poll()`):**
```c
int rc = poll(&pfd, 1, timeout_ms);
// Returns: >0 (events ready), 0 (timeout), -1 (error)
// Correct event mapping: POLLIN→READ, POLLOUT→WRITE, POLLERR→ERR
```

**Windows (`select()`):**
```c
int rc = select(0, &rd, &wr, &ex, &tv);
// Returns: >0 (events ready), 0 (timeout), SOCKET_ERROR (error)
// Correct event mapping: FD_ISSET macros check fd_set bits
```

Both are correct, but **FD_SET usage on Windows is antiquated**. Modern Windows code uses `WSAPoll()` (Winsock2 API, available since Windows Vista), which is the Windows equivalent of POSIX `poll()`.

**Recommendation:** Consider adding an optional Windows optimization:

```c
// In windows_net.c (currently uses select(), which requires fd_set management)
// Optimization: use WSAPoll() if available / on newer Windows
#if _WIN32_WINNT >= 0x0600
  // Use WSAPoll() – cleaner API, no fd_set management
#else
  // Fall back to select() for older Windows
#endif
```

For now, this is **not a correctness issue**, just a modernization opportunity.

---

### 4. TLS/Cryptography

#### Architecture Differences

| Aspect | POSIX (Secure Transport / OpenSSL) | Windows (Schannel/SSPI) |
|--------|-------------------------------------|------------------------|
| **Initialization** | Create SSL context, set I/O cbs | Acquire credentials, create security context |
| **Handshake** | `SSL_connect()` in a loop | Custom handshake loop with `InitializeSecurityContext()` |
| **Encryption/Decryption** | `SSL_write()` / `SSL_read()` | `EncryptMessage()` / `DecryptMessage()` |
| **Buffering** | Framework-managed inside OpenSSL | Manual: `enc_buf`, `dec_buf`, offset/len tracking |
| **Error Handling** | `SSL_get_error()` | SECURITY_STATUS codes (`SEC_E_*`, `SEC_I_*`) |

#### macOS (Secure Transport) 🟢

**Strengths:**
- Uses the system framework (Security.framework); no extra dependencies.
- Clean callback-based I/O: `st_read()` / `st_write()`.
- Simple handshake flow via `SSLHandshake()`.

**Code Quality:** ✅ Excellent—minimal and idiomatic.

#### Linux without OpenSSL 🟡

**Current state:** Stub that returns NULL (TLS unavailable).

**Consideration:** This is acceptable for a framework library — the user can rebuild with `-DHAVE_OPENSSL` if needed. The stub prevents link errors on headless systems.

#### OpenSSL Support (Linux) 🟢

**Strengths:**
- Uses system OpenSSL (or user-provided).
- Enables SNI (Server Name Indication) and hostname verification (X509_VERIFY_PARAM).
- Follows the canonical OpenSSL client pattern.

**Code Quality:** ✅ Good—production-ready.

#### Windows (Schannel) 🟡

**Strengths:**
- Uses the system Security Support Provider Interface (SSPI); no extra dependencies.
- Implements full TLS 1.0–1.3 support via Schannel.
- Manually buffers encrypted/decrypted data to handle partial TLS records.

**Issues:**

1. **Manual buffering complexity**: The code maintains `enc_buf`, `dec_buf`, `dec_offset`, `dec_len` to handle fragmented TLS records. This is **necessary** (Schannel doesn't give you a stream abstraction), but **error-prone**.

   **Issue found in `axTlsRecv()`:**
   ```c
   int
   axTlsRecv(AXtlsctx *ctx, void *buf, int len)
   {
     // Return any already-decrypted data first.
     if (ctx->dec_len > 0) {
       int n = ctx->dec_len < len ? ctx->dec_len : len;
       memcpy(buf, ctx->dec_buf + ctx->dec_offset, (size_t)n);
       ctx->dec_offset += n;
       ctx->dec_len -= n;
       if (ctx->dec_len == 0) ctx->dec_offset = 0;
       return n;
     }
     // ... more code
   }
   ```
   
   This logic **looks correct**, but the buffer management is **fragile**:
   - If `DecryptMessage()` partially processes input (e.g., two TLS records in `enc_buf`), the split is captured in `in_bufs[1]` (SECBUFFER_EXTRA).
   - The code correctly preserves extra bytes: `memmove(tmp, tmp + ..., extra)`.
   - **However**, if `enc_buf` fills to capacity and is never flushed, new data is silently dropped.

   **Recommendation:** Consider capping the TLS frame size; return an error if a single encrypted record exceeds buffer capacity:
   ```c
   // In axTlsRecv():
   if (ctx->enc_len == (int)sizeof(ctx->enc_buf)) {
     // Buffer is full — likely a malformed/huge TLS record
     fprintf(stderr, "axTlsRecv: encrypted buffer overflow\n");
     return -1;
   }
   ```

2. **Incomplete handshake error handling:**
   ```c
   if (ss != SEC_E_OK) {
     fprintf(stderr, "schannel_handshake: ISC failed: 0x%lx\n",
             (unsigned long)ss);
     return FALSE;
   }
   ```
   
   The error message prints the SECURITY_STATUS code (e.g., `0x80090301`), which is **not human-readable** without cross-referencing Schannel error codes. Compare to OpenSSL, which prints the cert/validation error clearly.

   **Recommendation:** Add a helper function:
   ```c
   static const char *
   schannel_error_string(SECURITY_STATUS ss)
   {
     switch (ss) {
       case SEC_E_INCOMPLETE_CREDENTIALS: return "incomplete credentials";
       case SEC_E_INVALID_HANDLE: return "invalid handle";
       case SEC_E_CERT_UNKNOWN: return "unknown certificate";
       // ... add more as needed
       default: return "unknown error";
     }
   }
   ```

3. **SNI and hostname verification:**
   ```c
   MultiByteToWideChar(CP_UTF8, 0, hostname ? hostname : "", -1, w_host, 256);
   ```
   
   If the hostname is exactly 256 bytes (or longer), it is **silently truncated**. Should add a check:
   ```c
   int hostname_len = hostname ? (int)strlen(hostname) : 0;
   if (hostname_len >= 255) {
     fprintf(stderr, "schannel_handshake: hostname too long\n");
     return FALSE;
   }
   ```

4. **Hard-coded buffer sizes:**
   ```c
   #define TLS_MAX_RECORD 16384
   #define TLS_RECV_BUF   (TLS_MAX_RECORD * 4)
   ```
   
   These constants assume RFC 5246 limits (16 KB per record). Modern TLS 1.3 allows larger record sizes. If a server sends a larger plaintext record, the encrypted buffer may overrun.

   **Recommendation:** Document this assumption, or make it configurable.

---

### 5. Error Handling & Diagnostics

#### stderr Output

Both implementations aggressively print errors to stderr:

```c
fprintf(stderr, "axNetSocket: %d\n", WSAGetLastError());
```

**Pros:**
- Developers can see errors quickly.
- Helps diagnose issues in headless environments.

**Cons:**
- No way to suppress or redirect errors (e.g., for unit tests or silent failures).
- Caller cannot distinguish between types of errors (permission denied vs. name not found).

**Recommendation:** Consider adding a `axNetSetErrorCallback()` function for tests/applications that want custom error handling:

```c
typedef void (*axnet_error_handler_t)(const char *msg);
void axNetSetErrorCallback(axnet_error_handler_t handler);
```

---

### 6. Thread Safety

**Current state:** Not documented.

**Analysis:**

- `axNetInit()` / `axNetShutdown()` are called once globally.
- On Windows: guarded by `static bool_t winsock_initialised` (not thread-safe!).
- On POSIX: no guard (sockets are inherently thread-safe after init).

**Issue:** If two threads call `axNetInit()` simultaneously, both may call `WSAStartup()` or `getaddrinfo()`, which is **not thread-safe** in general.

**Recommendation:** Add a mutex on both platforms:

```c
#ifdef _WIN32
static CRITICAL_SECTION cs_init;
#else
static pthread_mutex_t cs_init = PTHREAD_MUTEX_INITIALIZER;
#endif

bool_t axNetInit(void) {
  // Use platform mutex to guard winsock_initialised
}
```

Or require the caller to call `axNetInit()` once from the main thread before spawning worker threads (simpler, document clearly).

---

### 7. Resource Leaks

Spot-check of cleanup paths:

#### unix_net.c (OpenSSL path)
```c
// axTlsClose() correctly:
SSL_shutdown(ctx->ssl);
SSL_free(ctx->ssl);
SSL_CTX_free(ctx->ssl_ctx);
free(ctx);
```
✅ No leaks.

#### windows_net.c (Schannel path)
```c
// axTlsClose() correctly:
DeleteSecurityContext(&ctx->h_ctx);
FreeCredentialsHandle(&ctx->h_cred);
free(ctx);
```
✅ No leaks.

#### Error paths
Checked in `axTlsConnect()`:
```c
if (!schannel_handshake(ctx, hostname)) {
  FreeCredentialsHandle(&ctx->h_cred);  // ← clean up on handshake failure
  free(ctx);
  return NULL;
}
```
✅ Correct.

---

## Minor Issues Summary

| Issue | Severity | File | Line | Fix |
|-------|----------|------|------|-----|
| `axNetClose()` silent failure | Low | Both | ~80 | Document API contract or return bool_t |
| `axNetWouldBlock()` volatile errno/WSAGetLastError | Medium | Both | ~197-199 | Document immediate-call requirement |
| Windows: getaddrinfo error reporting uses WSAGetLastError | Low | windows_net.c | ~171 | Use gai_strerror() or accept inconsistency |
| Schannel: enc_buf could overflow silently | Medium | windows_net.c | ~670 | Add overflow check in axTlsRecv() |
| Schannel: error codes not human-readable | Low | windows_net.c | ~437, 512, etc. | Add helper function for SECURITY_STATUS→string |
| Schannel: hostname truncation possible | Low | windows_net.c | ~378 | Validate hostname length before MultiByteToWideChar |
| Schannel: TLS record size assumptions | Low | windows_net.c | ~35–36 | Document RFC 5246 assumption |
| No thread-safety on WSAStartup | Medium | windows_net.c | ~45–60 | Add mutex or document single-threaded init requirement |
| No error callback / custom logging | Low | Both | — | Optional enhancement for testing |

---

## Strengths (Summary)

✅ **Excellent abstraction layer** – clean separation between POSIX and Windows.  
✅ **Correct socket primitives** – proper mapping of constants and error returns.  
✅ **IPv4/IPv6 support** – both platforms handle dual-stack correctly.  
✅ **TLS on all platforms** – Secure Transport (macOS), OpenSSL (Linux), Schannel (Windows).  
✅ **Non-blocking I/O** – both `fcntl()` and `ioctlsocket()` used correctly.  
✅ **No resource leaks** – cleanup paths verified.  
✅ **Transport-oriented design** – doesn't over-abstract; lets higher layers handle protocol details.  

---

## Recommendations (Priority Order)

### High Priority
1. **Document axNetWouldBlock() contract:** Clarify that it must be called immediately after a failed send/recv, before any other I/O.
2. **Add Schannel enc_buf overflow check:** Prevent silent data loss on large TLS frames.
3. **Thread-safety for WSAStartup:** Either add a mutex or document that axNetInit() must be called once from the main thread.

### Medium Priority
4. **Improve Schannel error messages:** Add a helper function to convert SECURITY_STATUS codes to human-readable strings.
5. **Validate Schannel hostname length:** Check for truncation before calling MultiByteToWideChar().

### Low Priority
6. **Optional: axNetSetErrorCallback()** for tests and applications that need custom error handling.
7. **Optional: Modernize Windows polling** to WSAPoll() for Windows Vista+.
8. **Document TLS record size assumptions** in Schannel implementation.

---

## Conclusion

The multiplatform networking implementation is **production-quality and well-architected.** The API cleanly abstracts socket operations and TLS across three different backends (POSIX sockets + Secure Transport/OpenSSL, Winsock2 + Schannel). The issues identified are **edge cases and minor improvements**—not fundamental flaws. With the recommended enhancements in place, this is a **solid foundation for any application requiring cross-platform networking.**

---

## Files Modified (as per recommendations)

To implement the recommended fixes:

1. **platform.h** – Enhance documentation for `axNetWouldBlock()`.
2. **platform/windows/windows_net.c** – Add enc_buf overflow check, Schannel error helper, hostname validation.
3. **platform/unix/unix_net.c** – (No critical changes needed; POSIX implementation is clean.)

All changes are backward-compatible.
