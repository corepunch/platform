#include "ios_local.h"
#include <limits.h>

void axInit(void) { }
void axSetCursor(int cursor_id) { (void)cursor_id; }
char const *axGetPlatform(void) { return "ios"; }

bool_t
axIsDarkTheme(void)
{
  return axIOSGetWindow().traitCollection.userInterfaceStyle == UIUserInterfaceStyleDark;
}

char const *
axSettingsDirectory(void)
{
  static char path[PATH_MAX];
  if (*path) return path;
  NSURL *url = [[[NSFileManager defaultManager]
                 URLsForDirectory:NSApplicationSupportDirectory
                        inDomains:NSUserDomainMask] firstObject];
  url = [url URLByAppendingPathComponent:@"Orca" isDirectory:YES];
  [[NSFileManager defaultManager] createDirectoryAtURL:url
                         withIntermediateDirectories:YES attributes:nil error:NULL];
  snprintf(path, sizeof(path), "%s", url.fileSystemRepresentation);
  return path;
}

char const *
axShareDirectory(void)
{
  static char path[PATH_MAX];
  if (!*path)
    snprintf(path, sizeof(path), "%s/share", [NSBundle mainBundle].resourcePath.fileSystemRepresentation);
  return path;
}

char const *axLibDirectory(void) { return axShareDirectory(); }

bool_t axGetOpenFileName(AXopenfilename const *ofn) { (void)ofn; return FALSE; }
bool_t axGetSaveFileName(AXopenfilename const *ofn) { (void)ofn; return FALSE; }
bool_t axGetFolderName(AXopenfilename const *ofn) { (void)ofn; return FALSE; }
