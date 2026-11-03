#include "android_local.h"
#include <android/configuration.h>

static char g_settings[512], g_share[512], g_documents[512];

char const *axGetPlatform(void) { return "android"; }

static char const *
android_dir(char *buffer, size_t size, char const *base, char const *leaf)
{
  if (!buffer[0] && base) snprintf(buffer, size, "%s%s", base, leaf);
  return buffer;
}

/* Private app storage; the application unpacks its bundled files here. */
char const *
axSettingsDirectory(void)
{
  return android_dir(g_settings, sizeof(g_settings),
                     android.app ? android.app->activity->internalDataPath : NULL, "");
}

char const *
axShareDirectory(void)
{
  return android_dir(g_share, sizeof(g_share),
                     android.app ? android.app->activity->internalDataPath : NULL, "/share");
}

char const *axLibDirectory(void) { return axSettingsDirectory(); }

char const *
axDocumentsDirectory(void)
{
  char const *external = android.app ? android.app->activity->externalDataPath : NULL;
  return android_dir(g_documents, sizeof(g_documents), external ? external : axSettingsDirectory(), "");
}

bool_t
axIsDarkTheme(void)
{
  return android.app && AConfiguration_getUiModeNight(android.app->config) == ACONFIGURATION_UI_MODE_NIGHT_YES;
}

bool_t axGetOpenFileName(struct _AXopenfilename const *ofn) { (void)ofn; return FALSE; }
bool_t axGetSaveFileName(struct _AXopenfilename const *ofn) { (void)ofn; return FALSE; }
bool_t axGetFolderName(struct _AXopenfilename const *ofn) { (void)ofn; return FALSE; }
void axSetCursor(int cursor_id) { (void)cursor_id; }
char const *KEY_GetKeyName(uint32_t keycode) { (void)keycode; return ""; }
