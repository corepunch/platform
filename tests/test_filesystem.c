/*
 * test_filesystem.c - behavioral tests for platform filesystem helpers.
 *
 * Tests cover:
 *   - axGetCwd
 *   - axMkDir
 *   - axPathExists
 *   - axListDir callback enumeration
 */

#include "platform.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if defined(_WIN32) || defined(__MINGW32__)
#  include <direct.h>
#  define RMDIR _rmdir
#else
#  include <unistd.h>
#  define RMDIR rmdir
#endif

typedef struct {
  int saw_file;
  int saw_dir;
} list_ctx_t;

/* Callback that counts entries and stops after the first one. */
typedef struct {
  int count;
} early_stop_ctx_t;

static bool_t fs_stop_cb(AXdirent const *entry, void *userdata)
{
  early_stop_ctx_t *ctx = (early_stop_ctx_t *)userdata;
  (void)entry;
  if (!ctx)
    return FALSE;
  ctx->count++;
  return FALSE; /* request early stop */
}

static bool_t fs_list_cb(AXdirent const *entry, void *userdata)
{
  list_ctx_t *ctx = (list_ctx_t *)userdata;
  if (!entry || !ctx)
    return FALSE;

  if (strcmp(entry->name, "sample.txt") == 0) {
    ctx->saw_file = 1;
    assert(entry->is_directory == FALSE);
    assert(entry->size >= 4);
  }
  if (strcmp(entry->name, "sub") == 0) {
    ctx->saw_dir = 1;
    assert(entry->is_directory == TRUE);
  }

  return TRUE;
}

static void test_cwd_nonempty(void)
{
  char cwd[1024] = {0};
  assert(axGetCwd(cwd, sizeof(cwd)) == TRUE);
  assert(cwd[0] != '\0');
}

static void test_mkdir_exists_listdir(void)
{
  char cwd[1024] = {0};
  char base[128] = {0};
  char root[1200] = {0};
  char sub[1200] = {0};
  char file[1200] = {0};

  assert(axGetCwd(cwd, sizeof(cwd)) == TRUE);

  snprintf(base, sizeof(base), "ax_fs_test_%lu", (unsigned long)axGetMilliseconds());
  snprintf(root, sizeof(root), "%s/%s", cwd, base);
  snprintf(sub, sizeof(sub), "%s/sub", root);
  snprintf(file, sizeof(file), "%s/sample.txt", root);

  assert(axPathExists(root) == FALSE);

  assert(axMkDir(root) == TRUE);
  assert(axPathExists(root) == TRUE);

  /* Existing directory should be treated as success. */
  assert(axMkDir(root) == TRUE);

  assert(axMkDir(sub) == TRUE);
  assert(axPathExists(sub) == TRUE);

  {
    FILE *fp = fopen(file, "wb");
    assert(fp != NULL);
    fputs("test", fp);
    fclose(fp);
  }
  assert(axPathExists(file) == TRUE);

  {
    list_ctx_t ctx = {0, 0};
    assert(axListDir(root, fs_list_cb, &ctx) == TRUE);
    assert(ctx.saw_file == 1);
    assert(ctx.saw_dir == 1);
  }

  /* Cleanup */
  remove(file);
  RMDIR(sub);
  RMDIR(root);
}

static void test_listdir_nonexistent(void)
{
  char cwd[1024] = {0};
  char missing[1200] = {0};
  list_ctx_t ctx = {0, 0};

  assert(axGetCwd(cwd, sizeof(cwd)) == TRUE);
  int n = snprintf(missing, sizeof(missing), "%s/nonexistent_ax_path_%lu",
                   cwd, (unsigned long)axGetMilliseconds());
  assert(n > 0 && (size_t)n < sizeof(missing));
  assert(axPathExists(missing) == FALSE);
  assert(axListDir(missing, fs_list_cb, &ctx) == FALSE);
}

static void test_listdir_early_stop(void)
{
  char cwd[1024] = {0};
  char root[1200] = {0};
  char file_path[1200] = {0};
  char subdir_path[1200] = {0};
  FILE *fp = NULL;
  early_stop_ctx_t ctx = {0};

  assert(axGetCwd(cwd, sizeof(cwd)) == TRUE);

  snprintf(root, sizeof(root), "%s/ax_test_early_stop_%lu",
           cwd, (unsigned long)axGetMilliseconds());
  snprintf(file_path, sizeof(file_path), "%s/first.txt", root);
  snprintf(subdir_path, sizeof(subdir_path), "%s/second", root);

  assert(axMkDir(root) == TRUE);

  fp = fopen(file_path, "wb");
  assert(fp != NULL);
  fputs("data", fp);
  fclose(fp);

  assert(axMkDir(subdir_path) == TRUE);

  /* axListDir must return TRUE even when the callback stops early. */
  assert(axListDir(root, fs_stop_cb, &ctx) == TRUE);

  /* The callback must have been invoked exactly once and then stopped enumeration. */
  assert(ctx.count == 1);

  /* Cleanup */
  remove(file_path);
  RMDIR(subdir_path);
  RMDIR(root);
}

static void test_path_exists_null(void)
{
  /* NULL path must return FALSE without crashing. */
  assert(axPathExists(NULL) == FALSE);
}

static void test_settings_roundtrip(void)
{
  char const *dir = axSettingsDirectory();
  if (!dir || !dir[0]) {
    printf("  (axSettingsDirectory unavailable – settings round-trip skipped)\n");
    return;
  }

  /* Use a unique filename to avoid cross-run interference. */
  char name[64];
  snprintf(name, sizeof(name), "ax_test_settings_%lu.bin",
           (unsigned long)axGetMilliseconds());

  char const payload[] = "roundtrip_data_12345";
  size_t payload_len = sizeof(payload) - 1; /* exclude NUL */

  assert(axSettingsSave(name, payload, payload_len) == TRUE);

  char buf[64];
  size_t loaded = 0;
  assert(axSettingsLoad(name, buf, sizeof(buf), &loaded) == TRUE);
  assert(loaded == payload_len);
  assert(memcmp(buf, payload, loaded) == 0);

  /* Overwrite with different content and verify it replaces the old file. */
  char const payload2[] = "new_data";
  size_t payload2_len = sizeof(payload2) - 1;
  assert(axSettingsSave(name, payload2, payload2_len) == TRUE);

  size_t loaded2 = 0;
  assert(axSettingsLoad(name, buf, sizeof(buf), &loaded2) == TRUE);
  assert(loaded2 == payload2_len);
  assert(memcmp(buf, payload2, loaded2) == 0);

  /* Clean up: build the full path and remove it. */
  {
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s/%s", dir, name);
    remove(full_path);
  }
}

int main(void)
{
  test_cwd_nonempty();
  test_mkdir_exists_listdir();
  test_listdir_nonexistent();
  test_listdir_early_stop();
  test_path_exists_null();
  test_settings_roundtrip();
  printf("All filesystem tests passed.\n");
  return 0;
}
