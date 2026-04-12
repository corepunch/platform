#include "windows_local.h"
#include "../platform.h"

#define DEFAULT_WIDTH  640
#define DEFAULT_HEIGHT 480

HWND  g_hwnd = NULL;
HDC   g_hdc  = NULL;
HGLRC g_hrc  = NULL;

int g_win_width  = DEFAULT_WIDTH;
int g_win_height = DEFAULT_HEIGHT;

static HINSTANCE g_hinstance = NULL;

void
axInit(void)
{
  g_hinstance = GetModuleHandleA(NULL);

  WNDCLASSEXA wc;
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize        = sizeof(wc);
  wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
  wc.lpfnWndProc   = WndProc;
  wc.hInstance     = g_hinstance;
  wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
  wc.lpszClassName = PLATFORM_WNDCLASS;

  if (!RegisterClassExA(&wc)) {
    fprintf(stderr, "Failed to register window class\n");
  }
}

void
axShutdown(void)
{
  if (g_hrc) {
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(g_hrc);
    g_hrc = NULL;
  }
  if (g_hdc && g_hwnd) {
    ReleaseDC(g_hwnd, g_hdc);
    g_hdc = NULL;
  }
  if (g_hwnd) {
    DestroyWindow(g_hwnd);
    g_hwnd = NULL;
  }
  if (g_hinstance) {
    UnregisterClassA(PLATFORM_WNDCLASS, g_hinstance);
    g_hinstance = NULL;
  }
}

bool_t
axCreateWindow(char const *title, uint32_t width, uint32_t height, uint32_t flags)
{
  if (width == 0)  width  = DEFAULT_WIDTH;
  if (height == 0) height = DEFAULT_HEIGHT;

  if (g_hwnd) {
    /* Window already exists – update title and size only. */
    if (title) {
      SetWindowTextA(g_hwnd, title);
    }
    if ((int)width != g_win_width || (int)height != g_win_height) {
      RECT rect = { 0, 0, (LONG)width, (LONG)height };
      AdjustWindowRectEx(&rect, (DWORD)GetWindowLongA(g_hwnd, GWL_STYLE),
                         FALSE, (DWORD)GetWindowLongA(g_hwnd, GWL_EXSTYLE));
      SetWindowPos(g_hwnd, NULL, 0, 0,
                   rect.right - rect.left, rect.bottom - rect.top,
                   SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
      g_win_width  = (int)width;
      g_win_height = (int)height;
    }
    return TRUE;
  }

  /* Build window style from flags.
   * Default (flags == 0) preserves previous behaviour: decorated + resizable. */
  DWORD style   = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  DWORD exstyle = WS_EX_APPWINDOW;
  int   x = CW_USEDEFAULT, y = CW_USEDEFAULT;

  if (flags & AX_WINDOW_BORDERLESS) {
    style = WS_POPUP;
  } else {
    /* Resizable by default (backward compat); RESIZABLE flag makes it explicit. */
    if ((flags == 0) || (flags & AX_WINDOW_RESIZABLE)) {
      style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
    }
  }

  if (flags & AX_WINDOW_FULLSCREEN) {
    style   = WS_POPUP;
    exstyle = WS_EX_APPWINDOW;
    width   = (uint32_t)GetSystemMetrics(SM_CXSCREEN);
    height  = (uint32_t)GetSystemMetrics(SM_CYSCREEN);
    x = 0;
    y = 0;
  }

  /* Compute window rect that gives the desired client area. */
  RECT rect = { 0, 0, (LONG)width, (LONG)height };
  AdjustWindowRectEx(&rect, style, FALSE, exstyle);
  int w = rect.right  - rect.left;
  int h = rect.bottom - rect.top;

  g_hwnd = CreateWindowExA(
    exstyle, PLATFORM_WNDCLASS, title ? title : "",
    style,
    x, y, w, h,
    NULL, NULL, g_hinstance, NULL);

  if (!g_hwnd) {
    fprintf(stderr, "Failed to create window\n");
    return FALSE;
  }

  g_hdc = GetDC(g_hwnd);

  /* Choose and set a pixel format that supports OpenGL. */
  PIXELFORMATDESCRIPTOR pfd;
  ZeroMemory(&pfd, sizeof(pfd));
  pfd.nSize        = sizeof(pfd);
  pfd.nVersion     = 1;
  pfd.dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
  /* Double buffer is the default; include unless user has not requested it.
   * Since no single-buffer flag exists yet, always include it. */
  pfd.dwFlags     |= PFD_DOUBLEBUFFER;
  pfd.iPixelType   = PFD_TYPE_RGBA;
  pfd.cColorBits   = 32;
  pfd.cDepthBits   = 24;
  pfd.cStencilBits = 8;
  pfd.iLayerType   = PFD_MAIN_PLANE;

  int fmt = ChoosePixelFormat(g_hdc, &pfd);
  if (!fmt || !SetPixelFormat(g_hdc, fmt, &pfd)) {
    fprintf(stderr, "Failed to set pixel format\n");
    ReleaseDC(g_hwnd, g_hdc);
    g_hdc = NULL;
    DestroyWindow(g_hwnd);
    g_hwnd = NULL;
    return FALSE;
  }

  g_hrc = wglCreateContext(g_hdc);
  if (!g_hrc) {
    fprintf(stderr, "Failed to create WGL context\n");
    ReleaseDC(g_hwnd, g_hdc);
    g_hdc = NULL;
    DestroyWindow(g_hwnd);
    g_hwnd = NULL;
    return FALSE;
  }

  wglMakeCurrent(g_hdc, g_hrc);

  g_win_width  = (int)width;
  g_win_height = (int)height;

  if (!(flags & AX_WINDOW_HIDDEN)) {
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
  }

  printf("Windows window created (%dx%d)\n", g_win_width, g_win_height);
  return TRUE;
}

void
axMakeCurrentContext(void)
{
  if (g_hdc && g_hrc) {
    wglMakeCurrent(g_hdc, g_hrc);
  }
}

void
axBeginPaint(void)
{
  axMakeCurrentContext();
}

void
axEndPaint(void)
{
  /* Ensure alpha is fully opaque before presenting */
  glColorMask(0, 0, 0, 1);
  glClearColor(1, 1, 1, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glColorMask(1, 1, 1, 1);

  if (g_hdc) {
    SwapBuffers(g_hdc);
  }
}

void
axBindFramebuffer(void)
{
  /* WGL uses the default framebuffer; nothing to bind */
}

bool_t
axCreateSurface(uint32_t width, uint32_t height)
{
  /* Off-screen surfaces are not implemented */
  (void)width;
  (void)height;
  return FALSE;
}

float
axGetScaling(void)
{
  if (!g_hdc) {
    return 1.0f;
  }
  int dpi = GetDeviceCaps(g_hdc, LOGPIXELSX);
  return (dpi > 0) ? ((float)dpi / 96.0f) : 1.0f;
}

uint32_t
axGetSize(struct AXsize *size)
{
  if (size) {
    size->width  = (uint32_t)g_win_width;
    size->height = (uint32_t)g_win_height;
  }
  return MAKEDWORD(g_win_width, g_win_height);
}

bool_t
axSetSize(uint32_t width, uint32_t height, bool_t centered)
{
  if (!g_hwnd) {
    return FALSE;
  }
  UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
  int x = 0, y = 0;
  if (centered) {
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    x = (sw - (int)width)  / 2;
    y = (sh - (int)height) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
  } else {
    flags |= SWP_NOMOVE;
  }
  RECT rect = { 0, 0, (LONG)width, (LONG)height };
  AdjustWindowRectEx(&rect, (DWORD)GetWindowLongA(g_hwnd, GWL_STYLE),
                     FALSE, (DWORD)GetWindowLongA(g_hwnd, GWL_EXSTYLE));
  SetWindowPos(g_hwnd, NULL, x, y,
               rect.right - rect.left, rect.bottom - rect.top, flags);
  g_win_width  = (int)width;
  g_win_height = (int)height;
  return TRUE;
}

/* IOSurface stubs (macOS concept, not applicable on Windows) */
void
R_ReleaseIOSurface(unsigned iosurface)
{
  (void)iosurface;
}

struct _IMAGE *
R_CreatetextureFromIOSurface(unsigned surfaceID)
{
  (void)surfaceID;
  return NULL;
}

unsigned
R_CreateIOSurface(unsigned w, unsigned h, unsigned texnum)
{
  (void)w;
  (void)h;
  (void)texnum;
  return (unsigned)-1;
}
