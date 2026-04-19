# Platform Deprecation Notes

This project currently suppresses a small number of Apple deprecation warnings
at the exact call sites where the deprecated APIs are still intentionally used.
The goal is to keep builds readable without hiding unrelated warnings.

## Current suppressed APIs

### macOS IOSurface global sharing

File: `macos/macos_window.m`

- `kIOSurfaceIsGlobal` is deprecated because globally visible surfaces are
  considered insecure.
- This is only safe to keep while Orion still depends on cross-process
  IOSurface sharing in the current implementation.
- Follow-up: replace global-surface usage with a safer sharing mechanism, or
  remove the flag entirely if cross-process sharing is no longer required.

### macOS SecureTransport TLS backend

File: `unix/unix_net.c`

- `SSLCreateContext`, `SSLHandshake`, `SSLRead`, `SSLWrite`, and related
  SecureTransport APIs are deprecated on macOS.
- They still work on current macOS releases, but Apple recommends migrating to
  `Network.framework`.
- Follow-up options:
  1. implement a `Network.framework` TLS backend for macOS
  2. unify macOS and Linux on OpenSSL in the `axTls*` abstraction

## What we are deliberately not doing

- We are not disabling `-Wdeprecated-declarations` globally.
- We are not changing runtime behavior just to silence warnings.
- We are not switching the HTTP stack to libcurl as a warning workaround.

Using libcurl could make sense for an HTTP-only client layer, but the platform
library currently exposes lower-level socket and TLS primitives, so libcurl is
not a drop-in replacement for the deprecated TLS APIs.