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

int main(void)
{
  test_cwd_nonempty();
  test_mkdir_exists_listdir();
  printf("All filesystem tests passed.\n");
  return 0;
}
