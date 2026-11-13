#include "../platform.h"

int axHttpRpc(const char *method, const char *url, const char *bearer, const char *json,
              int timeout_ms, AXhttpcancel cancelled, void *context,
              char **response, size_t *length, int *status) {
  (void)method; (void)url; (void)bearer; (void)json; (void)timeout_ms; (void)cancelled; (void)context;
  if (response) *response = NULL;
  if (length) *length = 0;
  if (status) *status = 0;
  fprintf(stderr, "[platform_rpc] bounded RPC unavailable on Windows\n"); fflush(stderr);
  return AX_HTTP_RPC_UNAVAILABLE;
}
