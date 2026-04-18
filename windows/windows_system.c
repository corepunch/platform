#include "windows_local.h"
#include "../platform.h"
#include <commdlg.h>
#include <shlobj.h>

static char g_documents[MAX_PATH] = { 0 };
static PATHSTR g_share = { 0 };
static PATHSTR g_local = { 0 };
static PATHSTR g_lib   = { 0 };

char const *
axGetPlatform(void)
{
  return "windows";
}

char const *
axSettingsDirectory(void)
{
  if (g_local[0] == 0) {
    char appdata[MAX_PATH] = { 0 };
    SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appdata);
    snprintf(g_local, sizeof(g_local), "%s\\" APPNAME, appdata);
    CreateDirectoryA(g_local, NULL);
  }
  return g_local;
}

static void
get_exe_dir(char *buf, int sz)
{
  GetModuleFileNameA(NULL, buf, sz);
  char *sep = strrchr(buf, '\\');
  if (sep) {
    *sep = '\0';
  }
}

char const *
axShareDirectory(void)
{
  if (g_share[0] == 0) {
    get_exe_dir(g_share, sizeof(g_share));
  }
  return g_share;
}

char const *
axLibDirectory(void)
{
  if (g_lib[0] == 0) {
    get_exe_dir(g_lib, sizeof(g_lib));
  }
  return g_lib;
}

char const *
axDocumentsDirectory(void)
{
  if (g_documents[0] == 0) {
    SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, g_documents);
  }
  return g_documents;
}

bool_t
axIsDarkTheme(void)
{
  HKEY hkey;
  DWORD value = 1; /* default: light theme */
  DWORD size = sizeof(DWORD);
  if (RegOpenKeyExA(HKEY_CURRENT_USER,
                    "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                    0, KEY_READ, &hkey) == ERROR_SUCCESS) {
    RegQueryValueExA(hkey, "AppsUseLightTheme", NULL, NULL,
                     (LPBYTE)&value, &size);
    RegCloseKey(hkey);
  }
  /* AppsUseLightTheme == 0 means dark mode is active */
  return (value == 0) ? TRUE : FALSE;
}

bool_t
axGetOpenFileName(struct _AXopenfilename const *ofn)
{
  OPENFILENAMEA fn;
  ZeroMemory(&fn, sizeof(fn));
  fn.lStructSize = sizeof(fn);
  fn.hwndOwner   = g_hwnd;
  fn.lpstrFile   = ofn->lpstrFile;
  fn.nMaxFile    = ofn->nMaxFile;
  fn.lpstrFilter = ofn->lpstrFilter;
  fn.lpstrTitle  = ofn->lpstrTitle;
  fn.Flags       = OFN_EXPLORER;
  if (ofn->Flags & OFN_FILEMUSTEXIST) fn.Flags |= OFN_FILEMUSTEXIST;
  if (ofn->Flags & OFN_PATHMUSTEXIST) fn.Flags |= OFN_PATHMUSTEXIST;
  fn.lpstrFile[0] = '\0';
  return GetOpenFileNameA(&fn) ? TRUE : FALSE;
}

bool_t
axGetSaveFileName(struct _AXopenfilename const *ofn)
{
  OPENFILENAMEA fn;
  ZeroMemory(&fn, sizeof(fn));
  fn.lStructSize = sizeof(fn);
  fn.hwndOwner   = g_hwnd;
  fn.lpstrFile   = ofn->lpstrFile;
  fn.nMaxFile    = ofn->nMaxFile;
  fn.lpstrFilter = ofn->lpstrFilter;
  fn.lpstrTitle  = ofn->lpstrTitle;
  fn.Flags       = OFN_EXPLORER | OFN_OVERWRITEPROMPT;
  fn.lpstrFile[0] = '\0';
  return GetSaveFileNameA(&fn) ? TRUE : FALSE;
}

bool_t
axGetFolderName(struct _AXopenfilename const *ofn)
{
  BROWSEINFOA bi;
  ZeroMemory(&bi, sizeof(bi));
  bi.hwndOwner = g_hwnd;
  bi.lpszTitle = ofn->lpstrTitle ? ofn->lpstrTitle : "Select Folder";
  bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
  LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
  if (pidl) {
    BOOL ok = SHGetPathFromIDListA(pidl, ofn->lpstrFile);
    CoTaskMemFree(pidl);
    return ok ? TRUE : FALSE;
  }
  return FALSE;
}

char const *
KEY_GetKeyName(uint32_t keycode)
{
  (void)keycode;
  return "";
}

bool_t
axMkDir(char const *path)
{
  return (CreateDirectoryA(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS)
         ? TRUE : FALSE;
}

bool_t
axListDir(char const *path, AXDirCallback cb, void *userdata)
{
  char pattern[MAX_PATH];
  snprintf(pattern, sizeof(pattern), "%s\\*", path);

  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pattern, &fd);
  if (h == INVALID_HANDLE_VALUE)
    return FALSE;

  do {
    if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
      continue;

    AXdirent entry;
    memset(&entry, 0, sizeof(entry));
    strncpy(entry.name, fd.cFileName, sizeof(entry.name) - 1);
    entry.is_directory = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? TRUE : FALSE;
    entry.is_hidden    = (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)    ? TRUE : FALSE;
    entry.size         = entry.is_directory ? 0 : (size_t)fd.nFileSizeLow;

    /* Convert Windows FILETIME (100-ns ticks since 1601-01-01) to Unix time */
    ULARGE_INTEGER uli;
    uli.LowPart  = fd.ftLastWriteTime.dwLowDateTime;
    uli.HighPart = fd.ftLastWriteTime.dwHighDateTime;
    entry.modified = (time_t)((uli.QuadPart - 116444736000000000ULL) / 10000000ULL);

    if (!cb(&entry, userdata))
      break;
  } while (FindNextFileA(h, &fd));

  FindClose(h);
  return TRUE;
}

bool_t
axGetCwd(char *buf, size_t sz)
{
  return GetCurrentDirectoryA((DWORD)sz, buf) != 0 ? TRUE : FALSE;
}
