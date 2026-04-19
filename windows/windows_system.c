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

/* =========================================================================
 * Helper: UTF-8 to UTF-16 conversion
 * ====================================================================== */

static wchar_t *
utf8_to_utf16(char const *utf8_str)
{
  if (!utf8_str)
    return NULL;
  
  int len = MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, NULL, 0);
  if (len <= 0)
    return NULL;
  
  wchar_t *wide = (wchar_t *)malloc((size_t)len * sizeof(wchar_t));
  if (!wide)
    return NULL;
  
  MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, wide, len);
  return wide;
}

/* =========================================================================
 * Directory operations
 * ====================================================================== */

bool_t
axMkDir(char const *path)
{
  wchar_t *wpath = utf8_to_utf16(path);
  if (!wpath)
    return FALSE;
  
  BOOL created = CreateDirectoryW(wpath, NULL);
  if (!created) {
    DWORD err = GetLastError();
    if (err == ERROR_ALREADY_EXISTS) {
      /* Verify it's actually a directory, not a file */
      DWORD attrs = GetFileAttributesW(wpath);
      if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        free(wpath);
        return TRUE;
      }
    }
    free(wpath);
    return FALSE;
  }
  
  free(wpath);
  return TRUE;
}

bool_t
axListDir(char const *path, AXDirCallback cb, void *userdata)
{
  wchar_t *wpath = utf8_to_utf16(path);
  if (!wpath)
    return FALSE;
  
  /* Build search pattern: path\* */
  size_t wpath_len = wcslen(wpath);
  wchar_t *pattern = (wchar_t *)malloc((wpath_len + 3) * sizeof(wchar_t));
  if (!pattern) {
    free(wpath);
    return FALSE;
  }
  wcscpy(pattern, wpath);
  if (wpath_len > 0 && pattern[wpath_len - 1] != L'\\') {
    pattern[wpath_len] = L'\\';
    pattern[wpath_len + 1] = L'*';
    pattern[wpath_len + 2] = L'\0';
  } else {
    pattern[wpath_len] = L'*';
    pattern[wpath_len + 1] = L'\0';
  }
  
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW(pattern, &fd);
  free(pattern);
  free(wpath);
  
  if (h == INVALID_HANDLE_VALUE)
    return FALSE;
  
  bool_t success = TRUE;
  bool_t early_exit = FALSE;
  
  do {
    /* Skip . and .. */
    if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
      continue;
    
    /* Convert UTF-16 filename to UTF-8 */
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, NULL, 0, NULL, NULL);
    if (utf8_len <= 0 || utf8_len > 256) /* entry.name is 256 bytes */
      continue;
    
    AXdirent entry;
    memset(&entry, 0, sizeof(entry));
    WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, entry.name, sizeof(entry.name), NULL, NULL);
    
    entry.is_directory = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? TRUE : FALSE;
    entry.is_hidden    = (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)    ? TRUE : FALSE;
    
    /* Use both nFileSizeHigh and nFileSizeLow for proper 64-bit size */
    if (!entry.is_directory) {
      ULARGE_INTEGER file_size;
      file_size.LowPart = fd.nFileSizeLow;
      file_size.HighPart = fd.nFileSizeHigh;
      entry.size = (size_t)file_size.QuadPart;
    } else {
      entry.size = 0;
    }
    
    /* Convert Windows FILETIME (100-ns ticks since 1601-01-01) to Unix time */
    ULARGE_INTEGER uli;
    uli.LowPart  = fd.ftLastWriteTime.dwLowDateTime;
    uli.HighPart = fd.ftLastWriteTime.dwHighDateTime;
    entry.modified = (time_t)((uli.QuadPart - 116444736000000000ULL) / 10000000ULL);
    
    if (!cb(&entry, userdata)) {
      early_exit = TRUE;
      break;
    }
  } while (FindNextFileW(h, &fd));
  
  /* Check if we exited due to error (not early callback abort or normal end) */
  if (!early_exit) {
    DWORD err = GetLastError();
    if (err != ERROR_NO_MORE_FILES) {
      success = FALSE;
    }
  }
  
  FindClose(h);
  return success;
}

bool_t
axGetCwd(char *buf, size_t sz)
{
  /* GetCurrentDirectoryW returns the required length when buffer is too small,
   * so we need to check that returned length is > 0 and <= sz. */
  wchar_t wbuf[MAX_PATH];
  DWORD wlen = GetCurrentDirectoryW(sizeof(wbuf) / sizeof(wbuf[0]), wbuf);
  
  if (wlen == 0 || wlen >= sizeof(wbuf) / sizeof(wbuf[0]))
    return FALSE; /* Error or buffer would overflow */
  
  /* Convert UTF-16 to UTF-8 */
  int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, NULL, 0, NULL, NULL);
  if (utf8_len <= 0 || (size_t)utf8_len > sz)
    return FALSE; /* Conversion failed or doesn't fit */
  
  WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, buf, (int)sz, NULL, NULL);
  return TRUE;
}
