/* Bounded business RPC through the system curl process, with secrets on stdin. */
#include "../platform.h"

#if defined(AX_PLATFORM_IOS) || defined(__ANDROID__)
int axHttpRpc(const char *method, const char *url, const char *bearer, const char *json,
              int timeout_ms, AXhttpcancel cancelled, void *context,
              char **response, size_t *length, int *status) {
  (void)method; (void)url; (void)bearer; (void)json; (void)timeout_ms; (void)cancelled; (void)context;
  if (response) *response = NULL;
  if (length) *length = 0;
  if (status) *status = 0;
  fprintf(stderr, "[platform_rpc] bounded RPC unavailable on this mobile platform\n"); fflush(stderr);
  return AX_HTTP_RPC_UNAVAILABLE;
}
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define RPC_REQUEST_MAX (1024u * 1024u)
#define RPC_RESPONSE_MAX (4u * 1024u * 1024u)

static int64_t rpc_now(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1;
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static bool_t rpc_url_valid(const char *url) {
  if (!url || strlen(url) > 2048) return FALSE;
  bool_t local = !strncmp(url, "http://", 7);
  const char *p = local ? url + 7 : !strncmp(url, "https://", 8) ? url + 8 : NULL;
  if (!p || !*p) return FALSE;
  const char *end = p;
  while (*end && *end != '/' && *end != '?' && *end != '#') end++;
  if (end == p) return FALSE;
  for (const char *q = p; *q; q++)
    if ((unsigned char)*q < 33 || (unsigned char)*q == 127 || *q == '\\' || *q == '#' || (q < end && *q == '@')) return FALSE;
  if (local) {
    size_t n = (size_t)(end - p);
    const char *port = NULL;
    if (n >= 5 && !strncmp(p, "[::1]", 5)) port = p + 5;
    else if (n >= 9 && !strncmp(p, "localhost", 9)) port = p + 9;
    else if (n >= 9 && !strncmp(p, "127.0.0.1", 9)) port = p + 9;
    else return FALSE;
    if (port != end) {
      if (*port++ != ':' || port == end) return FALSE;
      while (port < end) if (*port < '0' || *port++ > '9') return FALSE;
    }
  }
  return TRUE;
}
static bool_t rpc_append(char *buffer, size_t capacity, size_t *used, const char *text) {
  size_t n = strlen(text);
  if (*used + n >= capacity) return FALSE;
  memcpy(buffer + *used, text, n); *used += n; buffer[*used] = 0; return TRUE;
}
static bool_t rpc_quote(char *buffer, size_t capacity, size_t *used, const char *text) {
  if (!rpc_append(buffer, capacity, used, "\"")) return FALSE;
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
    const char *escape = *p == '\\' ? "\\\\" : *p == '"' ? "\\\"" :
                         *p == '\n' ? "\\n" : *p == '\r' ? "\\r" : *p == '\t' ? "\\t" : NULL;
    if (escape) { if (!rpc_append(buffer, capacity, used, escape)) return FALSE; }
    else {
      if (*p < 32 || *p == 127 || *used + 1 >= capacity) return FALSE;
      buffer[(*used)++] = (char)*p; buffer[*used] = 0;
    }
  }
  return rpc_append(buffer, capacity, used, "\"\n");
}
static bool_t rpc_safe_fd(int *fd) {
  if (*fd <= STDERR_FILENO) {
    int duplicate = fcntl(*fd, F_DUPFD, STDERR_FILENO + 1);
    if (duplicate < 0) return FALSE;
    close(*fd); *fd = duplicate;
  }
  int flags = fcntl(*fd, F_GETFD, 0);
  return flags >= 0 && fcntl(*fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}
static bool_t rpc_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}
int axHttpRpc(const char *method, const char *url, const char *bearer, const char *json,
              int timeout_ms, AXhttpcancel cancelled, void *context,
              char **response, size_t *response_length, int *http_status) {
  if (response) *response = NULL;
  if (response_length) *response_length = 0;
  if (http_status) *http_status = 0;
  if (!response || !response_length || !http_status || !method || !rpc_url_valid(url) ||
      !bearer || !*bearer || strlen(bearer) > 512 || timeout_ms <= 0 || timeout_ms > 300000 ||
      (strcmp(method, "GET") && strcmp(method, "POST")) ||
      (!strcmp(method, "GET") && json) || (!strcmp(method, "POST") && (!json || strlen(json) > RPC_REQUEST_MAX))) {
    fprintf(stderr, "[platform_rpc] invalid RPC arguments\n"); fflush(stderr); return AX_HTTP_RPC_ERROR;
  }
  for (const unsigned char *p = (const unsigned char *)bearer; *p; p++)
    if (*p < 33 || *p > 126) { fprintf(stderr, "[platform_rpc] invalid bearer token\n"); fflush(stderr); return AX_HTTP_RPC_ERROR; }
  if (cancelled && cancelled(context)) return AX_HTTP_RPC_CANCELLED;
  int64_t started = rpc_now();
  if (started < 0) return AX_HTTP_RPC_ERROR;
  size_t capacity = 2 * (json ? strlen(json) : 0) + 8192, used = 0;
  char *config = calloc(1, capacity), *output = malloc(RPC_RESPONSE_MAX + 5);
  if (!config || !output) { free(config); free(output); return AX_HTTP_RPC_ERROR; }
  char authorization[544];
  snprintf(authorization, sizeof(authorization), "Authorization: Bearer %s", bearer);
  bool_t built = rpc_append(config, capacity, &used, "silent\nurl = ") && rpc_quote(config, capacity, &used, url) &&
                 rpc_append(config, capacity, &used, "header = ") && rpc_quote(config, capacity, &used, authorization) &&
                 rpc_append(config, capacity, &used, "header = \"Accept: application/json\"\n");
  memset(authorization, 0, sizeof(authorization));
  if (json) built = built && rpc_append(config, capacity, &used, "header = \"Content-Type: application/json\"\ndata-raw = ") &&
                    rpc_quote(config, capacity, &used, json);
  if (!built) { free(config); free(output); return AX_HTTP_RPC_ERROR; }
  int input[2] = {-1, -1}, out[2] = {-1, -1}, result = AX_HTTP_RPC_ERROR;
  pid_t child = -1; bool_t reaped = FALSE; int child_status = 0;
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, input) != 0 || pipe(out) != 0) goto done;
  if (!rpc_safe_fd(&input[0]) || !rpc_safe_fd(&input[1]) || !rpc_safe_fd(&out[0]) || !rpc_safe_fd(&out[1])) goto done;
#if !defined(SO_NOSIGPIPE) && !defined(MSG_NOSIGNAL)
  result = AX_HTTP_RPC_UNAVAILABLE; goto done;
#endif
#ifdef SO_NOSIGPIPE
  int one = 1; if (setsockopt(input[0], SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one)) != 0) goto done;
#endif
  if (!rpc_nonblocking(input[0]) || !rpc_nonblocking(out[0])) goto done;
  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0) goto done;
  int setup = posix_spawn_file_actions_adddup2(&actions, input[1], STDIN_FILENO) ||
              posix_spawn_file_actions_adddup2(&actions, out[1], STDOUT_FILENO) ||
              posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0) ||
              posix_spawn_file_actions_addclose(&actions, input[0]) || posix_spawn_file_actions_addclose(&actions, out[0]) ||
              posix_spawn_file_actions_addclose(&actions, input[1]) || posix_spawn_file_actions_addclose(&actions, out[1]);
  char *argv[] = {"curl", "-q", "--config", "-", "--proto", "=http,https", "--noproxy", "*", "--globoff", "--write-out", "\n%{http_code}", NULL};
  char *environment[] = {"PATH=/usr/bin:/bin", "LANG=C", NULL};
  int spawned = setup ? EINVAL : posix_spawn(&child, "/usr/bin/curl", &actions, NULL, argv, environment);
  posix_spawn_file_actions_destroy(&actions);
  if (spawned != 0) { result = AX_HTTP_RPC_UNAVAILABLE; child = -1; goto done; }
  close(input[1]); input[1] = -1; close(out[1]); out[1] = -1;
  size_t sent = 0, received = 0; bool_t input_done = FALSE, eof = FALSE;
  for (;;) {
    if (cancelled && cancelled(context)) { result = AX_HTTP_RPC_CANCELLED; break; }
    int64_t now = rpc_now();
    if (now < 0) break;
    if (now - started >= timeout_ms) { result = AX_HTTP_RPC_TIMEOUT; break; }
    if (!reaped) {
      pid_t waited = waitpid(child, &child_status, WNOHANG);
      if (waited == child) reaped = TRUE;
      else if (waited < 0 && errno != EINTR) break;
    }
    if (reaped && eof) {
      if (!WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0 || received < 4 || output[received - 4] != '\n') break;
      const char *code = output + received - 3;
      if (code[0] < '1' || code[0] > '5' || code[1] < '0' || code[1] > '9' || code[2] < '0' || code[2] > '9') break;
      *http_status = (code[0] - '0') * 100 + (code[1] - '0') * 10 + code[2] - '0';
      *response_length = received - 4; output[*response_length] = 0; *response = output; output = NULL;
      result = AX_HTTP_RPC_OK; break;
    }
    struct pollfd fds[2] = {{input_done ? -1 : input[0], POLLOUT, 0}, {eof ? -1 : out[0], POLLIN, 0}};
    int remaining = (int)(timeout_ms - (now - started));
    int ready = poll(fds, 2, remaining < 25 ? remaining : 25);
    if (ready < 0) { if (errno == EINTR) continue; break; }
    if (!input_done && fds[0].revents) {
#ifdef MSG_NOSIGNAL
      ssize_t n = send(input[0], config + sent, used - sent, MSG_NOSIGNAL);
#else
      ssize_t n = send(input[0], config + sent, used - sent, 0);
#endif
      if (n > 0) sent += (size_t)n;
      else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) break;
      if (sent == used) { shutdown(input[0], SHUT_WR); input_done = TRUE; }
    }
    if (!eof && fds[1].revents) {
      ssize_t n = read(out[0], output + received, RPC_RESPONSE_MAX + 5 - received);
      if (n > 0) { received += (size_t)n; if (received == RPC_RESPONSE_MAX + 5) { result = AX_HTTP_RPC_TOO_LARGE; break; } }
      else if (n == 0) eof = TRUE;
      else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) break;
    }
  }
done:
  if (child > 0 && !reaped) { kill(child, SIGKILL); while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {} }
  if (input[0] >= 0) close(input[0]);
  if (input[1] >= 0) close(input[1]);
  if (out[0] >= 0) close(out[0]);
  if (out[1] >= 0) close(out[1]);
  memset(config, 0, capacity); free(config); free(output);
  if (result == AX_HTTP_RPC_ERROR || result == AX_HTTP_RPC_UNAVAILABLE || result == AX_HTTP_RPC_TOO_LARGE) {
    fprintf(stderr, "[platform_rpc] RPC transport failed status=%d\n", result); fflush(stderr);
  }
  return result;
}
#endif
