#ifndef UNICODE
#define UNICODE
#endif

#ifndef _UNICODE
#define _UNICODE
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#ifndef COBJMACROS
#define COBJMACROS
#endif

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <d2d1.h>
#include <d2derr.h>
#include <dwrite.h>
#include <objbase.h>
#include <wincodec.h>

#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "board.h"

#ifndef BI_ALPHABITFIELDS
#define BI_ALPHABITFIELDS 6
#endif

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

#ifndef USER_DEFAULT_SCREEN_DPI
#define USER_DEFAULT_SCREEN_DPI 96
#endif

#define ARRAY_COUNT(value) (sizeof(value) / sizeof((value)[0]))
#define WM_APP_EXIT (WM_APP + 1)
#define IDC_TEXT_VIEW 1001
#define IMAGE_ZOOM_MIN 0.01
#define IMAGE_ZOOM_MAX 2.0
#define IMAGE_CHECKER_CELL_SIZE 8
#define IMAGE_CHECKER_TILE_SIZE (IMAGE_CHECKER_CELL_SIZE * 2)
#define WINDOW_MIN_WIDTH 200
#define WINDOW_MIN_HEIGHT 140
#define TEXT_FONT_FAMILY_COUNT 3
#define TEXT_LAYOUT_MAX_SIZE 1000000.0f
#define TEXT_AUTOSCROLL_TIMER 1
#define TEXT_AUTOSCROLL_INTERVAL_MS 40
#define TEXT_ZOOM_MIN 0.1
#define TEXT_ZOOM_MAX IMAGE_ZOOM_MAX

static const wchar_t kAppTitle[] = L"Board";
static const wchar_t kWindowClassName[] = L"ClipBoardViewerWindow";
static const wchar_t kTextViewClassName[] = L"ClipBoardTextView";
static const wchar_t kImageViewClassName[] = L"ClipBoardImageView";
static const wchar_t kSingleInstanceMutexName[] = L"Local\\ClipBoardViewerSingleton";
static const wchar_t* const kTextFontFamilies[TEXT_FONT_FAMILY_COUNT] = {L"Cascadia Code", L"Cascadia Mono",
                                                                         L"Consolas"};

typedef enum {
  CLIPBOARD_CONTENT_EMPTY = 0,
  CLIPBOARD_CONTENT_TEXT,
  CLIPBOARD_CONTENT_IMAGE,
  CLIPBOARD_CONTENT_UNSUPPORTED,
  CLIPBOARD_CONTENT_ERROR,
} ClipboardContentType;

typedef struct {
  ClipboardContentType type;
  wchar_t* text;
  size_t textLength;
  HBITMAP bitmap;
  int bitmapWidth;
  int bitmapHeight;
  wchar_t status[256];
  wchar_t message[256];
} ClipboardSnapshot;

static ClipboardSnapshot g_snapshot = {0};
static HWND g_textView = NULL;
static HWND g_textTooltip = NULL;
static HWND g_imageView = NULL;
static HANDLE g_singleInstanceMutex = NULL;
static bool g_clipboardListenerRegistered = false;
static HFONT g_uiFont = NULL;
static IDWriteTextFormat* g_textFormat = NULL;
static bool g_textFontAvailable[TEXT_FONT_FAMILY_COUNT] = {false};
static size_t g_textFontSelected = TEXT_FONT_FAMILY_COUNT - 1;
static wchar_t g_textTooltipText[512] = L"";
static UINT g_currentDpi = USER_DEFAULT_SCREEN_DPI;
static int g_textScrollX = 0;
static int g_textScrollY = 0;
static UINT32 g_textSelectionAnchor = 0;
static UINT32 g_textSelectionCaret = 0;
static bool g_textSelecting = false;
static int g_textWheelRemainder = 0;
static int g_textHWheelRemainder = 0;
static double g_textZoom = 1.0;
static bool g_imageZoomExplicit = false;
static double g_imageZoom = 1.0;
static DWORD g_clipboardSequence = 0;
static bool g_comInitialized = false;
static ID2D1Factory* g_d2dFactory = NULL;
static IDWriteFactory* g_dwriteFactory = NULL;
static IWICImagingFactory* g_wicFactory = NULL;
static ID2D1HwndRenderTarget* g_textRenderTarget = NULL;
static ID2D1SolidColorBrush* g_textBrush = NULL;
static ID2D1SolidColorBrush* g_textSelectionBrush = NULL;
static ID2D1SolidColorBrush* g_textSelectionTextBrush = NULL;
static IDWriteTextLayout* g_textLayout = NULL;
static ID2D1HwndRenderTarget* g_imageRenderTarget = NULL;
static ID2D1Bitmap* g_imageBitmap = NULL;
static ID2D1BitmapBrush* g_imageCheckerBrush = NULL;
static const IID kIID_IDWriteFactory = {0xb859ee5a, 0xd838, 0x4b5b, {0xa2, 0xe8, 0x1a, 0xdc, 0x7d, 0x93, 0xdb, 0x48}};

typedef BOOL(WINAPI* SetProcessDpiAwarenessContextFn)(DPI_AWARENESS_CONTEXT);
typedef HRESULT(WINAPI* SetProcessDpiAwarenessFn)(int);
typedef BOOL(WINAPI* SetProcessDPIAwareFn)(void);
typedef UINT(WINAPI* GetDpiForWindowFn)(HWND);
typedef BOOL(WINAPI* AdjustWindowRectExForDpiFn)(LPRECT, DWORD, BOOL, DWORD, UINT);

static SetProcessDpiAwarenessContextFn load_set_process_dpi_awareness_context(HMODULE module) {
  union {
    FARPROC proc;
    SetProcessDpiAwarenessContextFn fn;
  } symbol = {0};
  symbol.proc = GetProcAddress(module, "SetProcessDpiAwarenessContext");
  return symbol.fn;
}

static SetProcessDpiAwarenessFn load_set_process_dpi_awareness(HMODULE module) {
  union {
    FARPROC proc;
    SetProcessDpiAwarenessFn fn;
  } symbol = {0};
  symbol.proc = GetProcAddress(module, "SetProcessDpiAwareness");
  return symbol.fn;
}

static SetProcessDPIAwareFn load_set_process_dpi_aware(HMODULE module) {
  union {
    FARPROC proc;
    SetProcessDPIAwareFn fn;
  } symbol = {0};
  symbol.proc = GetProcAddress(module, "SetProcessDPIAware");
  return symbol.fn;
}

static GetDpiForWindowFn load_get_dpi_for_window(HMODULE module) {
  union {
    FARPROC proc;
    GetDpiForWindowFn fn;
  } symbol = {0};
  symbol.proc = GetProcAddress(module, "GetDpiForWindow");
  return symbol.fn;
}

static AdjustWindowRectExForDpiFn load_adjust_window_rect_ex_for_dpi(HMODULE module) {
  union {
    FARPROC proc;
    AdjustWindowRectExForDpiFn fn;
  } symbol = {0};
  symbol.proc = GetProcAddress(module, "AdjustWindowRectExForDpi");
  return symbol.fn;
}

static void enable_process_dpi_awareness(void) {
  HMODULE user32 = GetModuleHandleW(L"user32.dll");
  if (user32) {
    SetProcessDpiAwarenessContextFn setContext = load_set_process_dpi_awareness_context(user32);
    if (setContext && setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
      return;
    }
  }

  HMODULE shcore = LoadLibraryW(L"shcore.dll");
  if (shcore) {
    SetProcessDpiAwarenessFn setAwareness = load_set_process_dpi_awareness(shcore);
    if (setAwareness && SUCCEEDED(setAwareness(2))) {
      FreeLibrary(shcore);
      return;
    }
    FreeLibrary(shcore);
  }

  if (user32) {
    SetProcessDPIAwareFn setDpiAware = load_set_process_dpi_aware(user32);
    if (setDpiAware) {
      setDpiAware();
    }
  }
}

static void initialize_common_controls(void) {
  INITCOMMONCONTROLSEX controls = {0};
  controls.dwSize = sizeof(controls);
  controls.dwICC = ICC_WIN95_CLASSES;
  InitCommonControlsEx(&controls);
}

static UINT dpi_from_dc(HWND hwnd) {
  HWND dcOwner = hwnd;
  HDC dc = GetDC(dcOwner);
  if (!dc && hwnd) {
    dcOwner = NULL;
    dc = GetDC(NULL);
  }
  if (!dc) {
    return USER_DEFAULT_SCREEN_DPI;
  }

  int dpi = GetDeviceCaps(dc, LOGPIXELSX);
  ReleaseDC(dcOwner, dc);
  return dpi > 0 ? (UINT) dpi : USER_DEFAULT_SCREEN_DPI;
}

static UINT dpi_for_window(HWND hwnd) {
  static bool loaded = false;
  static GetDpiForWindowFn getDpiForWindow = NULL;

  if (!loaded) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
      getDpiForWindow = load_get_dpi_for_window(user32);
    }
    loaded = true;
  }

  if (getDpiForWindow && hwnd) {
    UINT dpi = getDpiForWindow(hwnd);
    if (dpi > 0) {
      return dpi;
    }
  }

  return dpi_from_dc(hwnd);
}

static int scale_for_dpi(int value, UINT dpi) {
  return MulDiv(value, (int) (dpi ? dpi : USER_DEFAULT_SCREEN_DPI), USER_DEFAULT_SCREEN_DPI);
}

static int scale_for_window(HWND hwnd, int value) {
  UINT dpi = hwnd ? g_currentDpi : dpi_for_window(NULL);
  return scale_for_dpi(value, dpi);
}

static HFONT create_ui_font(UINT dpi) {
  int height = -MulDiv(9, (int) (dpi ? dpi : USER_DEFAULT_SCREEN_DPI), 72);
  return CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

static void update_text_font_tooltip_text(void) {
  const wchar_t* status[TEXT_FONT_FAMILY_COUNT] = {L"unavailable", L"unavailable", L"unavailable"};
  for (size_t i = 0; i < TEXT_FONT_FAMILY_COUNT; ++i) {
    if (i == g_textFontSelected) {
      status[i] = g_textFontAvailable[i] ? L"picked" : L"picked fallback";
    } else if (g_textFontAvailable[i]) {
      status[i] = L"available";
    }
  }

  swprintf(g_textTooltipText, ARRAY_COUNT(g_textTooltipText), L"Text font:\n%ls: %ls\n%ls: %ls\n%ls: %ls",
           kTextFontFamilies[0], status[0], kTextFontFamilies[1], status[1], kTextFontFamilies[2], status[2]);
}

static int CALLBACK font_family_enum_proc(const LOGFONTW* font, const TEXTMETRICW* metrics, DWORD fontType,
                                          LPARAM param) {
  (void) font;
  (void) metrics;
  (void) fontType;
  bool* found = (bool*) param;
  *found = true;
  return 0;
}

static bool font_family_available(const wchar_t* faceName) {
  HDC hdc = GetDC(NULL);
  if (!hdc) {
    return false;
  }

  LOGFONTW font = {0};
  font.lfCharSet = DEFAULT_CHARSET;
  font.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
  wcsncpy(font.lfFaceName, faceName, ARRAY_COUNT(font.lfFaceName) - 1);
  font.lfFaceName[ARRAY_COUNT(font.lfFaceName) - 1] = L'\0';

  bool found = false;
  EnumFontFamiliesExW(hdc, &font, font_family_enum_proc, (LPARAM) &found, 0);
  ReleaseDC(NULL, hdc);
  return found;
}

static void refresh_text_font_choice(void) {
  bool selected = false;
  for (size_t i = 0; i < TEXT_FONT_FAMILY_COUNT; ++i) {
    g_textFontAvailable[i] = font_family_available(kTextFontFamilies[i]);
    if (!selected && g_textFontAvailable[i]) {
      g_textFontSelected = i;
      selected = true;
    }
  }

  if (!selected) {
    g_textFontSelected = TEXT_FONT_FAMILY_COUNT - 1;
  }
  update_text_font_tooltip_text();
}

static const wchar_t* selected_text_font_family(void) {
  return kTextFontFamilies[g_textFontSelected < TEXT_FONT_FAMILY_COUNT ? g_textFontSelected
                                                                       : TEXT_FONT_FAMILY_COUNT - 1];
}

static FLOAT text_font_size_for_dpi(UINT dpi) {
  return (FLOAT) ((double) MulDiv(9, (int) (dpi ? dpi : USER_DEFAULT_SCREEN_DPI), 72) * g_textZoom);
}

static HFONT ui_font(void) {
  return g_uiFont ? g_uiFont : (HFONT) GetStockObject(DEFAULT_GUI_FONT);
}

static void release_text_layout(void) {
  if (g_textLayout) {
    IDWriteTextLayout_Release(g_textLayout);
    g_textLayout = NULL;
  }
}

static void release_text_format(void) {
  release_text_layout();
  if (g_textFormat) {
    IDWriteTextFormat_Release(g_textFormat);
    g_textFormat = NULL;
  }
}

static bool create_text_format(void) {
  release_text_format();
  if (!g_dwriteFactory) {
    return false;
  }

  HRESULT hr = IDWriteFactory_CreateTextFormat(
      g_dwriteFactory, selected_text_font_family(), NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL, text_font_size_for_dpi(g_currentDpi), L"", &g_textFormat);
  if (FAILED(hr)) {
    return false;
  }

  IDWriteTextFormat_SetWordWrapping(g_textFormat, DWRITE_WORD_WRAPPING_NO_WRAP);
  IDWriteTextFormat_SetTextAlignment(g_textFormat, DWRITE_TEXT_ALIGNMENT_LEADING);
  IDWriteTextFormat_SetParagraphAlignment(g_textFormat, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
  IDWriteTextFormat_SetIncrementalTabStop(g_textFormat, (FLOAT) scale_for_dpi(32, g_currentDpi));
  return true;
}

static void refresh_text_tooltip(HWND hwnd) {
  update_text_font_tooltip_text();
  if (!g_textTooltip || !g_textView) {
    return;
  }

  TOOLINFOW tool = {0};
  tool.cbSize = sizeof(tool);
  tool.hwnd = hwnd;
  tool.uId = (UINT_PTR) g_textView;
  tool.lpszText = g_textTooltipText;
  SendMessageW(g_textTooltip, TTM_SETMAXTIPWIDTH, 0, scale_for_window(hwnd, 360));
  SendMessageW(g_textTooltip, TTM_UPDATETIPTEXTW, 0, (LPARAM) &tool);
}

static void create_text_tooltip(HWND hwnd) {
  if (!g_textView) {
    return;
  }

  g_textTooltip =
      CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT,
                      CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd, NULL, GetModuleHandleW(NULL), NULL);
  if (!g_textTooltip) {
    return;
  }

  TOOLINFOW tool = {0};
  tool.cbSize = sizeof(tool);
  tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
  tool.hwnd = hwnd;
  tool.uId = (UINT_PTR) g_textView;
  tool.lpszText = g_textTooltipText;
  SendMessageW(g_textTooltip, TTM_SETMAXTIPWIDTH, 0, scale_for_window(hwnd, 360));
  SendMessageW(g_textTooltip, TTM_ADDTOOLW, 0, (LPARAM) &tool);
}

static void destroy_text_tooltip(void) {
  if (g_textTooltip) {
    DestroyWindow(g_textTooltip);
    g_textTooltip = NULL;
  }
}

static void recreate_fonts_for_dpi(UINT dpi) {
  if (g_uiFont) {
    DeleteObject(g_uiFont);
    g_uiFont = NULL;
  }

  g_currentDpi = dpi ? dpi : USER_DEFAULT_SCREEN_DPI;
  g_uiFont = create_ui_font(g_currentDpi);
  refresh_text_font_choice();
}

static void recreate_fonts(HWND hwnd) {
  recreate_fonts_for_dpi(dpi_for_window(hwnd));
}

static void clamp_text_scroll(HWND hwnd);

static void apply_text_font(void) {
  create_text_format();
  if (g_textView) {
    if (g_snapshot.type == CLIPBOARD_CONTENT_TEXT) {
      clamp_text_scroll(g_textView);
    }
    InvalidateRect(g_textView, NULL, TRUE);
  }
}

static void free_fonts(void) {
  if (g_uiFont) {
    DeleteObject(g_uiFont);
    g_uiFont = NULL;
  }
}

static void set_text(wchar_t* dest, size_t destCount, const wchar_t* value) {
  if (!dest || destCount == 0) {
    return;
  }
  if (!value) {
    value = L"";
  }
  wcsncpy(dest, value, destCount - 1);
  dest[destCount - 1] = L'\0';
}

static void format_text(wchar_t* dest, size_t destCount, const wchar_t* fmt, ...) {
  if (!dest || destCount == 0) {
    return;
  }

  va_list args;
  va_start(args, fmt);
  int written = vswprintf(dest, destCount, fmt, args);
  va_end(args);

  if (written < 0 || (size_t) written >= destCount) {
    dest[destCount - 1] = L'\0';
  }
}

static wchar_t* duplicate_wide_range(const wchar_t* text, size_t length) {
  wchar_t* copy = (wchar_t*) malloc((length + 1) * sizeof(wchar_t));
  if (!copy) {
    return NULL;
  }
  if (length > 0) {
    memcpy(copy, text, length * sizeof(wchar_t));
  }
  copy[length] = L'\0';
  return copy;
}

static void free_clipboard_snapshot(ClipboardSnapshot* snapshot) {
  if (!snapshot) {
    return;
  }
  free(snapshot->text);
  snapshot->text = NULL;
  snapshot->textLength = 0;
  if (snapshot->bitmap) {
    DeleteObject(snapshot->bitmap);
    snapshot->bitmap = NULL;
  }
  snapshot->bitmapWidth = 0;
  snapshot->bitmapHeight = 0;
}

static void replace_clipboard_snapshot(ClipboardSnapshot* snapshot) {
  free_clipboard_snapshot(&g_snapshot);
  g_snapshot = *snapshot;
  memset(snapshot, 0, sizeof(*snapshot));
}

static bool try_open_clipboard(HWND hwnd) {
  const int retries = 5;
  for (int i = 0; i < retries; ++i) {
    if (OpenClipboard(hwnd)) {
      return true;
    }
    Sleep(10);
  }
  return false;
}

static size_t dib_bits_offset(const BITMAPINFOHEADER* header) {
  size_t offset = header->biSize;

  if ((header->biCompression == BI_BITFIELDS || header->biCompression == BI_ALPHABITFIELDS) &&
      header->biSize == sizeof(BITMAPINFOHEADER)) {
    offset += (header->biCompression == BI_ALPHABITFIELDS ? 4u : 3u) * sizeof(DWORD);
  }

  size_t colors = header->biClrUsed;
  if (colors == 0 && header->biBitCount <= 8) {
    colors = (size_t) 1u << header->biBitCount;
  }
  offset += colors * sizeof(RGBQUAD);

  return offset;
}

static bool read_bitmap_size(HBITMAP bitmap, int* width, int* height) {
  if (!bitmap || !width || !height) {
    return false;
  }

  BITMAP info;
  if (GetObjectW(bitmap, sizeof(info), &info) != sizeof(info)) {
    return false;
  }

  *width = info.bmWidth;
  *height = info.bmHeight;
  return *width > 0 && *height > 0;
}

static HBITMAP bitmap_from_dib(UINT format) {
  if (!IsClipboardFormatAvailable(format)) {
    return NULL;
  }

  HANDLE handle = GetClipboardData(format);
  if (!handle) {
    return NULL;
  }

  void* locked = GlobalLock(handle);
  if (!locked) {
    return NULL;
  }

  SIZE_T totalSize = GlobalSize(handle);
  if (totalSize < sizeof(BITMAPINFOHEADER)) {
    GlobalUnlock(handle);
    return NULL;
  }

  BITMAPINFOHEADER* header = (BITMAPINFOHEADER*) locked;
  if (header->biSize < sizeof(BITMAPINFOHEADER) || header->biSize > totalSize) {
    GlobalUnlock(handle);
    return NULL;
  }

  size_t offset = dib_bits_offset(header);
  if (offset >= totalSize) {
    GlobalUnlock(handle);
    return NULL;
  }

  HDC screenDc = GetDC(NULL);
  if (!screenDc) {
    GlobalUnlock(handle);
    return NULL;
  }

  BYTE* bits = (BYTE*) locked + offset;
  HBITMAP bitmap = CreateDIBitmap(screenDc, header, CBM_INIT, bits, (BITMAPINFO*) header, DIB_RGB_COLORS);
  ReleaseDC(NULL, screenDc);
  GlobalUnlock(handle);

  return bitmap;
}

static HBITMAP acquire_clipboard_bitmap(void) {
  if (IsClipboardFormatAvailable(CF_BITMAP)) {
    HBITMAP clipboardBitmap = (HBITMAP) GetClipboardData(CF_BITMAP);
    if (clipboardBitmap) {
      HBITMAP copy = (HBITMAP) CopyImage(clipboardBitmap, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
      if (copy) {
        return copy;
      }
    }
  }

  const UINT dibFormats[] = {CF_DIBV5, CF_DIB};
  for (size_t i = 0; i < ARRAY_COUNT(dibFormats); ++i) {
    HBITMAP bitmap = bitmap_from_dib(dibFormats[i]);
    if (bitmap) {
      return bitmap;
    }
  }

  return NULL;
}

static bool read_unicode_text(ClipboardSnapshot* snapshot) {
  HANDLE handle = GetClipboardData(CF_UNICODETEXT);
  if (!handle) {
    return false;
  }

  const wchar_t* locked = (const wchar_t*) GlobalLock(handle);
  if (!locked) {
    return false;
  }

  const wchar_t* text = locked;
  size_t length = wcslen(text);
  if (length > 0 && text[0] == 0xFEFF) {
    ++text;
    --length;
  }

  wchar_t* copy = duplicate_wide_range(text, length);
  GlobalUnlock(handle);
  if (!copy) {
    return false;
  }

  snapshot->type = CLIPBOARD_CONTENT_TEXT;
  snapshot->text = copy;
  snapshot->textLength = length;
  format_text(snapshot->status, ARRAY_COUNT(snapshot->status), L"  %llu chars (UTF-16)", (unsigned long long) length);
  snapshot->message[0] = L'\0';
  return true;
}

static bool read_ansi_text(ClipboardSnapshot* snapshot) {
  HANDLE handle = GetClipboardData(CF_TEXT);
  if (!handle) {
    return false;
  }

  const char* locked = (const char*) GlobalLock(handle);
  if (!locked) {
    return false;
  }

  int required = MultiByteToWideChar(CP_ACP, 0, locked, -1, NULL, 0);
  if (required <= 0) {
    GlobalUnlock(handle);
    return false;
  }

  wchar_t* copy = (wchar_t*) malloc((size_t) required * sizeof(wchar_t));
  if (!copy) {
    GlobalUnlock(handle);
    return false;
  }

  int converted = MultiByteToWideChar(CP_ACP, 0, locked, -1, copy, required);
  GlobalUnlock(handle);
  if (converted <= 0) {
    free(copy);
    return false;
  }

  size_t length = wcslen(copy);
  snapshot->type = CLIPBOARD_CONTENT_TEXT;
  snapshot->text = copy;
  snapshot->textLength = length;
  format_text(snapshot->status, ARRAY_COUNT(snapshot->status), L"  %llu chars (ANSI)", (unsigned long long) length);
  snapshot->message[0] = L'\0';
  return true;
}

static void set_snapshot_state(ClipboardSnapshot* snapshot, ClipboardContentType type, const wchar_t* status,
                               const wchar_t* message) {
  snapshot->type = type;
  set_text(snapshot->status, ARRAY_COUNT(snapshot->status), status);
  set_text(snapshot->message, ARRAY_COUNT(snapshot->message), message);
}

static void read_clipboard_snapshot(HWND hwnd, ClipboardSnapshot* snapshot) {
  memset(snapshot, 0, sizeof(*snapshot));

  if (!try_open_clipboard(hwnd)) {
    set_snapshot_state(snapshot, CLIPBOARD_CONTENT_ERROR, L"  Clipboard unavailable", L"Unable to open the clipboard.");
    return;
  }

  UINT formatCount = CountClipboardFormats();
  if (formatCount == 0) {
    CloseClipboard();
    set_snapshot_state(snapshot, CLIPBOARD_CONTENT_EMPTY, L"  Clipboard is empty", L"Clipboard is empty.");
    return;
  }

  if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
    if (read_unicode_text(snapshot)) {
      CloseClipboard();
      return;
    }
    CloseClipboard();
    set_snapshot_state(snapshot, CLIPBOARD_CONTENT_ERROR, L"  Text read failed", L"Unable to read clipboard text.");
    return;
  }

  if (IsClipboardFormatAvailable(CF_TEXT)) {
    if (read_ansi_text(snapshot)) {
      CloseClipboard();
      return;
    }
    CloseClipboard();
    set_snapshot_state(snapshot, CLIPBOARD_CONTENT_ERROR, L"  Text read failed", L"Unable to convert clipboard text.");
    return;
  }

  bool imageAvailable = IsClipboardFormatAvailable(CF_BITMAP) || IsClipboardFormatAvailable(CF_DIBV5) ||
                        IsClipboardFormatAvailable(CF_DIB);
  if (imageAvailable) {
    HBITMAP bitmap = acquire_clipboard_bitmap();
    int width = 0;
    int height = 0;
    if (bitmap && read_bitmap_size(bitmap, &width, &height)) {
      CloseClipboard();
      snapshot->type = CLIPBOARD_CONTENT_IMAGE;
      snapshot->bitmap = bitmap;
      snapshot->bitmapWidth = width;
      snapshot->bitmapHeight = height;
      format_text(snapshot->status, ARRAY_COUNT(snapshot->status), L"  %d×%d", width, height);
      snapshot->message[0] = L'\0';
      return;
    }
    if (bitmap) {
      DeleteObject(bitmap);
    }
    CloseClipboard();
    set_snapshot_state(snapshot, CLIPBOARD_CONTENT_ERROR, L"  Image read failed", L"Unable to read clipboard image.");
    return;
  }

  CloseClipboard();
  format_text(snapshot->status, ARRAY_COUNT(snapshot->status), L"  Unsupported clipboard content - %u formats",
              (unsigned) formatCount);
  set_text(snapshot->message, ARRAY_COUNT(snapshot->message), L"Clipboard content is not text or a bitmap image.");
  snapshot->type = CLIPBOARD_CONTENT_UNSUPPORTED;
}

static int scaled_metric(HWND hwnd, int value) {
  return scale_for_window(hwnd, value);
}

static void draw_centered_message(HDC hdc, RECT contentRect, const wchar_t* message);
static RECT fitted_image_rect(int sourceWidth, int sourceHeight, int targetWidth, int targetHeight);
static void adjust_window_rect_for_client(HWND hwnd, int clientWidth, int clientHeight, RECT* rect);
static bool resize_window_for_image_zoom(HWND hwnd);

static int rounded_dimension(double size) {
  if (size < 1.0) {
    return 1;
  }
  if (size > (double) (INT_MAX / 4)) {
    return INT_MAX / 4;
  }

  return (int) (size + 0.5);
}

static int rounded_zoom_dimension(int source, double zoom) {
  if (source <= 0 || zoom <= 0.0) {
    return 1;
  }

  return rounded_dimension((double) source * zoom);
}

static double clamp_image_zoom(double zoom) {
  if (zoom < IMAGE_ZOOM_MIN) {
    return IMAGE_ZOOM_MIN;
  }
  if (zoom > IMAGE_ZOOM_MAX) {
    return IMAGE_ZOOM_MAX;
  }
  return zoom;
}

static double clamp_text_zoom(double zoom) {
  if (zoom < TEXT_ZOOM_MIN) {
    return TEXT_ZOOM_MIN;
  }
  if (zoom > TEXT_ZOOM_MAX) {
    return TEXT_ZOOM_MAX;
  }
  return zoom;
}

static void reset_image_view_state(void) {
  g_imageZoomExplicit = false;
  g_imageZoom = 1.0;
}

static double fit_image_zoom(HWND hwnd) {
  if (g_snapshot.type != CLIPBOARD_CONTENT_IMAGE || g_snapshot.bitmapWidth <= 0 || g_snapshot.bitmapHeight <= 0) {
    return 1.0;
  }

  RECT bounds;
  if (g_imageView && IsWindow(g_imageView)) {
    GetClientRect(g_imageView, &bounds);
  } else {
    GetClientRect(hwnd, &bounds);
  }
  int availableWidth = bounds.right - bounds.left;
  int availableHeight = bounds.bottom - bounds.top;
  if (availableWidth <= 0 || availableHeight <= 0) {
    return 1.0;
  }

  double xZoom = (double) availableWidth / (double) g_snapshot.bitmapWidth;
  double yZoom = (double) availableHeight / (double) g_snapshot.bitmapHeight;
  double zoom = xZoom < yZoom ? xZoom : yZoom;
  return zoom > 0.0 ? zoom : 1.0;
}

static double current_image_zoom(HWND hwnd) {
  if (g_imageZoomExplicit) {
    return g_imageZoom;
  }
  return fit_image_zoom(hwnd);
}

static bool current_image_ratio(double* ratio) {
  if (g_snapshot.type != CLIPBOARD_CONTENT_IMAGE || g_snapshot.bitmapWidth <= 0 || g_snapshot.bitmapHeight <= 0) {
    return false;
  }

  if (ratio) {
    *ratio = (double) g_snapshot.bitmapWidth / (double) g_snapshot.bitmapHeight;
  }
  return true;
}

static double displayed_image_zoom(HWND hwnd) {
  if (g_snapshot.type != CLIPBOARD_CONTENT_IMAGE || g_snapshot.bitmapWidth <= 0 || g_snapshot.bitmapHeight <= 0) {
    return 1.0;
  }

  RECT client;
  HWND imageWindow = g_imageView && IsWindow(g_imageView) ? g_imageView : hwnd;
  GetClientRect(imageWindow, &client);
  int clientWidth = client.right - client.left;
  int clientHeight = client.bottom - client.top;
  if (clientWidth <= 0 || clientHeight <= 0) {
    return current_image_zoom(hwnd);
  }

  RECT imageRect = fitted_image_rect(g_snapshot.bitmapWidth, g_snapshot.bitmapHeight, clientWidth, clientHeight);
  int displayedWidth = imageRect.right - imageRect.left;
  int displayedHeight = imageRect.bottom - imageRect.top;
  if (displayedWidth <= 0 || displayedHeight <= 0) {
    return current_image_zoom(hwnd);
  }

  double xZoom = (double) displayedWidth / (double) g_snapshot.bitmapWidth;
  double yZoom = (double) displayedHeight / (double) g_snapshot.bitmapHeight;
  double zoom = xZoom < yZoom ? xZoom : yZoom;
  return zoom > 0.0 ? zoom : current_image_zoom(hwnd);
}

static void current_status_text(HWND hwnd, wchar_t* status, size_t statusCount) {
  if (g_snapshot.type == CLIPBOARD_CONTENT_TEXT) {
    int percent = (int) (g_textZoom * 100.0 + 0.5);
    format_text(status, statusCount, L"%ls - %d%%", g_snapshot.status, percent);
    return;
  }

  if (g_snapshot.type != CLIPBOARD_CONTENT_IMAGE) {
    set_text(status, statusCount, g_snapshot.status);
    return;
  }

  int percent = (int) (displayed_image_zoom(hwnd) * 100.0 + 0.5);
  format_text(status, statusCount, L"  %d×%d - %d%%", g_snapshot.bitmapWidth, g_snapshot.bitmapHeight, percent);
}

static const wchar_t* title_suffix(const wchar_t* status) {
  if (!status) {
    return L"";
  }
  while (*status == L' ' || *status == L'\t') {
    ++status;
  }
  return status;
}

static void update_window_title(HWND hwnd) {
  wchar_t status[256];
  current_status_text(hwnd, status, ARRAY_COUNT(status));

  const wchar_t* suffix = title_suffix(status);
  wchar_t title[320];
  if (suffix[0]) {
    set_text(title, ARRAY_COUNT(title), suffix);
  } else {
    set_text(title, ARRAY_COUNT(title), kAppTitle);
  }
  SetWindowTextW(hwnd, title);
}

static void layout_children(HWND hwnd) {
  RECT client;
  GetClientRect(hwnd, &client);
  int width = client.right - client.left;
  int height = client.bottom - client.top;

  if (g_textView) {
    MoveWindow(g_textView, 0, 0, width, height, TRUE);
  }
  if (g_imageView) {
    MoveWindow(g_imageView, 0, 0, width, height, TRUE);
  }
}

static int text_padding_x(HWND hwnd) {
  return scale_for_window(hwnd, 4);
}

static int text_padding_y(HWND hwnd) {
  return scale_for_window(hwnd, 3);
}

static UINT32 text_layout_length(void) {
  if (g_snapshot.type != CLIPBOARD_CONTENT_TEXT || !g_snapshot.text || g_snapshot.textLength == 0) {
    return 0;
  }
  return g_snapshot.textLength > UINT32_MAX ? UINT32_MAX : (UINT32) g_snapshot.textLength;
}

static int float_extent_to_int(FLOAT value) {
  if (value <= 0.0f) {
    return 0;
  }
  if (value >= (FLOAT) INT_MAX) {
    return INT_MAX;
  }
  return (int) (value + 0.999f);
}

static bool ensure_text_layout(HWND hwnd) {
  (void) hwnd;
  if (g_textLayout) {
    return true;
  }
  if (!g_dwriteFactory) {
    return false;
  }
  if (!g_textFormat && !create_text_format()) {
    return false;
  }

  const wchar_t* text = g_snapshot.type == CLIPBOARD_CONTENT_TEXT && g_snapshot.text ? g_snapshot.text : L"";
  UINT32 length = text_layout_length();
  HRESULT hr = IDWriteFactory_CreateTextLayout(g_dwriteFactory, text, length, g_textFormat, TEXT_LAYOUT_MAX_SIZE,
                                               TEXT_LAYOUT_MAX_SIZE, &g_textLayout);
  return SUCCEEDED(hr);
}

static bool text_layout_metrics(HWND hwnd, DWRITE_TEXT_METRICS* metrics) {
  if (!metrics) {
    return false;
  }
  memset(metrics, 0, sizeof(*metrics));
  if (!ensure_text_layout(hwnd)) {
    return false;
  }
  return SUCCEEDED(IDWriteTextLayout_GetMetrics(g_textLayout, metrics));
}

static int text_line_height(HWND hwnd) {
  if (ensure_text_layout(hwnd)) {
    DWRITE_LINE_METRICS line = {0};
    UINT32 count = 0;
    if (SUCCEEDED(IDWriteTextLayout_GetLineMetrics(g_textLayout, &line, 1, &count)) && count > 0 &&
        line.height > 0.0f) {
      return max(float_extent_to_int(line.height), 1);
    }
  }
  return max(scale_for_window(hwnd, 16), 1);
}

static int text_char_width(HWND hwnd) {
  return max(rounded_dimension((double) scale_for_window(hwnd, 8) * g_textZoom), 1);
}

static void text_content_size(HWND hwnd, int* width, int* height) {
  DWRITE_TEXT_METRICS metrics;
  bool haveMetrics = text_layout_metrics(hwnd, &metrics);
  FLOAT textWidth = haveMetrics ? max(metrics.width, metrics.widthIncludingTrailingWhitespace) : 0.0f;
  FLOAT textHeight = haveMetrics ? metrics.height : 0.0f;
  if (width) {
    *width = float_extent_to_int(textWidth) + 2 * text_padding_x(hwnd);
  }
  if (height) {
    *height = float_extent_to_int(textHeight) + 2 * text_padding_y(hwnd);
  }
}

static void text_max_scroll(HWND hwnd, int* maxX, int* maxY) {
  RECT client;
  GetClientRect(hwnd, &client);
  int clientWidth = max(client.right - client.left, 0);
  int clientHeight = max(client.bottom - client.top, 0);
  int contentWidth = 0;
  int contentHeight = 0;
  text_content_size(hwnd, &contentWidth, &contentHeight);
  if (maxX) {
    *maxX = max(contentWidth - clientWidth, 0);
  }
  if (maxY) {
    *maxY = max(contentHeight - clientHeight, 0);
  }
}

static void update_text_scrollbars(HWND hwnd) {
  if (!hwnd) {
    return;
  }

  RECT client;
  GetClientRect(hwnd, &client);
  int clientWidth = max(client.right - client.left, 0);
  int clientHeight = max(client.bottom - client.top, 0);
  int contentWidth = 0;
  int contentHeight = 0;
  text_content_size(hwnd, &contentWidth, &contentHeight);

  SCROLLINFO si = {0};
  si.cbSize = sizeof(si);
  si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  si.nMin = 0;
  si.nMax = max(contentHeight - 1, 0);
  si.nPage = (UINT) clientHeight;
  si.nPos = g_textScrollY;
  SetScrollInfo(hwnd, SB_VERT, &si, TRUE);

  si.nMax = max(contentWidth - 1, 0);
  si.nPage = (UINT) clientWidth;
  si.nPos = g_textScrollX;
  SetScrollInfo(hwnd, SB_HORZ, &si, TRUE);
}

static bool set_text_scroll(HWND hwnd, int x, int y) {
  int maxX = 0;
  int maxY = 0;
  text_max_scroll(hwnd, &maxX, &maxY);
  x = min(max(x, 0), maxX);
  y = min(max(y, 0), maxY);
  bool changed = x != g_textScrollX || y != g_textScrollY;
  g_textScrollX = x;
  g_textScrollY = y;
  update_text_scrollbars(hwnd);
  if (changed) {
    InvalidateRect(hwnd, NULL, TRUE);
  }
  return changed;
}

static void clamp_text_scroll(HWND hwnd) {
  set_text_scroll(hwnd, g_textScrollX, g_textScrollY);
}

static void reset_text_view_state(void) {
  g_textScrollX = 0;
  g_textScrollY = 0;
  g_textSelectionAnchor = 0;
  g_textSelectionCaret = 0;
  g_textSelecting = false;
  g_textWheelRemainder = 0;
  g_textHWheelRemainder = 0;
  release_text_layout();
  if (g_textView) {
    if (g_snapshot.type == CLIPBOARD_CONTENT_TEXT) {
      update_text_scrollbars(g_textView);
    } else {
      SCROLLINFO si = {0};
      si.cbSize = sizeof(si);
      si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
      SetScrollInfo(g_textView, SB_VERT, &si, TRUE);
      SetScrollInfo(g_textView, SB_HORZ, &si, TRUE);
    }
    InvalidateRect(g_textView, NULL, TRUE);
  }
}

static D2D1_COLOR_F d2d_color_from_colorref(COLORREF color) {
  D2D1_COLOR_F result = {(FLOAT) GetRValue(color) / 255.0f, (FLOAT) GetGValue(color) / 255.0f,
                         (FLOAT) GetBValue(color) / 255.0f, 1.0f};
  return result;
}

static void release_text_render_target(void) {
  if (g_textSelectionTextBrush) {
    ID2D1SolidColorBrush_Release(g_textSelectionTextBrush);
    g_textSelectionTextBrush = NULL;
  }
  if (g_textSelectionBrush) {
    ID2D1SolidColorBrush_Release(g_textSelectionBrush);
    g_textSelectionBrush = NULL;
  }
  if (g_textBrush) {
    ID2D1SolidColorBrush_Release(g_textBrush);
    g_textBrush = NULL;
  }
  if (g_textRenderTarget) {
    ID2D1HwndRenderTarget_Release(g_textRenderTarget);
    g_textRenderTarget = NULL;
  }
}

static bool create_text_brushes(void) {
  if (!g_textRenderTarget) {
    return false;
  }

  D2D1_COLOR_F textColor = d2d_color_from_colorref(GetSysColor(COLOR_WINDOWTEXT));
  D2D1_COLOR_F selectionColor = d2d_color_from_colorref(GetSysColor(COLOR_HIGHLIGHT));
  D2D1_COLOR_F selectionTextColor = d2d_color_from_colorref(GetSysColor(COLOR_HIGHLIGHTTEXT));
  return SUCCEEDED(ID2D1HwndRenderTarget_CreateSolidColorBrush(g_textRenderTarget, &textColor, NULL, &g_textBrush)) &&
         SUCCEEDED(ID2D1HwndRenderTarget_CreateSolidColorBrush(g_textRenderTarget, &selectionColor, NULL,
                                                               &g_textSelectionBrush)) &&
         SUCCEEDED(ID2D1HwndRenderTarget_CreateSolidColorBrush(g_textRenderTarget, &selectionTextColor, NULL,
                                                               &g_textSelectionTextBrush));
}

static bool ensure_text_render_target(HWND hwnd) {
  if (g_textRenderTarget) {
    return true;
  }
  if (!g_d2dFactory || !hwnd) {
    return false;
  }

  RECT client;
  GetClientRect(hwnd, &client);
  D2D1_SIZE_U pixelSize = {(UINT32) max(client.right - client.left, 1), (UINT32) max(client.bottom - client.top, 1)};
  D2D1_RENDER_TARGET_PROPERTIES renderProperties = {0};
  renderProperties.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
  renderProperties.pixelFormat.format = DXGI_FORMAT_UNKNOWN;
  renderProperties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_UNKNOWN;
  renderProperties.dpiX = (FLOAT) USER_DEFAULT_SCREEN_DPI;
  renderProperties.dpiY = (FLOAT) USER_DEFAULT_SCREEN_DPI;
  renderProperties.usage = D2D1_RENDER_TARGET_USAGE_NONE;
  renderProperties.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;

  D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProperties = {0};
  hwndProperties.hwnd = hwnd;
  hwndProperties.pixelSize = pixelSize;
  hwndProperties.presentOptions = D2D1_PRESENT_OPTIONS_NONE;

  HRESULT hr =
      ID2D1Factory_CreateHwndRenderTarget(g_d2dFactory, &renderProperties, &hwndProperties, &g_textRenderTarget);
  if (FAILED(hr)) {
    return false;
  }

  ID2D1HwndRenderTarget_SetDpi(g_textRenderTarget, (FLOAT) USER_DEFAULT_SCREEN_DPI, (FLOAT) USER_DEFAULT_SCREEN_DPI);
  if (!create_text_brushes()) {
    release_text_render_target();
    return false;
  }
  return true;
}

static bool resize_text_render_target(HWND hwnd) {
  if (!g_textRenderTarget) {
    return true;
  }

  RECT client;
  GetClientRect(hwnd, &client);
  D2D1_SIZE_U pixelSize = {(UINT32) max(client.right - client.left, 1), (UINT32) max(client.bottom - client.top, 1)};
  return SUCCEEDED(ID2D1HwndRenderTarget_Resize(g_textRenderTarget, &pixelSize));
}

static bool text_selection_range(UINT32* start, UINT32* length) {
  UINT32 textLength = text_layout_length();
  UINT32 first = min(g_textSelectionAnchor, g_textSelectionCaret);
  UINT32 last = max(g_textSelectionAnchor, g_textSelectionCaret);
  first = min(first, textLength);
  last = min(last, textLength);
  if (last <= first) {
    if (start) {
      *start = first;
    }
    if (length) {
      *length = 0;
    }
    return false;
  }
  if (start) {
    *start = first;
  }
  if (length) {
    *length = last - first;
  }
  return true;
}

static UINT32 text_position_from_point(HWND hwnd, int x, int y) {
  if (!ensure_text_layout(hwnd)) {
    return 0;
  }

  FLOAT layoutX = (FLOAT) (x + g_textScrollX - text_padding_x(hwnd));
  FLOAT layoutY = (FLOAT) (y + g_textScrollY - text_padding_y(hwnd));
  WINBOOL trailing = FALSE;
  WINBOOL inside = FALSE;
  DWRITE_HIT_TEST_METRICS metrics = {0};
  HRESULT hr = IDWriteTextLayout_HitTestPoint(g_textLayout, layoutX, layoutY, &trailing, &inside, &metrics);
  if (FAILED(hr)) {
    return 0;
  }

  UINT32 position = metrics.textPosition;
  if (trailing && metrics.length > 0) {
    position += metrics.length;
  }
  return min(position, text_layout_length());
}

static void update_text_selection_at_point(HWND hwnd, int x, int y, bool extend) {
  UINT32 position = text_position_from_point(hwnd, x, y);
  if (!extend) {
    g_textSelectionAnchor = position;
  }
  g_textSelectionCaret = position;
  InvalidateRect(hwnd, NULL, TRUE);
}

static void copy_selected_text(HWND hwnd) {
  UINT32 start = 0;
  UINT32 length = 0;
  if (!text_selection_range(&start, &length) || !g_snapshot.text) {
    return;
  }

  SIZE_T bytes = ((SIZE_T) length + 1) * sizeof(wchar_t);
  HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (!handle) {
    return;
  }

  wchar_t* dest = (wchar_t*) GlobalLock(handle);
  if (!dest) {
    GlobalFree(handle);
    return;
  }
  memcpy(dest, g_snapshot.text + start, (size_t) length * sizeof(wchar_t));
  dest[length] = L'\0';
  GlobalUnlock(handle);

  if (!OpenClipboard(hwnd)) {
    GlobalFree(handle);
    return;
  }
  EmptyClipboard();
  if (!SetClipboardData(CF_UNICODETEXT, handle)) {
    GlobalFree(handle);
  }
  CloseClipboard();
}

static void draw_text_selection(HWND hwnd, FLOAT originX, FLOAT originY) {
  UINT32 start = 0;
  UINT32 length = 0;
  if (!text_selection_range(&start, &length) || !g_textLayout || !g_textSelectionBrush) {
    return;
  }

  DWRITE_TEXT_METRICS textMetrics;
  if (!text_layout_metrics(hwnd, &textMetrics) || textMetrics.lineCount == 0) {
    return;
  }

  UINT32 capacity = max(textMetrics.lineCount, 1);
  DWRITE_HIT_TEST_METRICS* metrics = (DWRITE_HIT_TEST_METRICS*) calloc(capacity, sizeof(*metrics));
  if (!metrics) {
    return;
  }

  UINT32 actual = 0;
  HRESULT hr =
      IDWriteTextLayout_HitTestTextRange(g_textLayout, start, length, originX, originY, metrics, capacity, &actual);
  if (hr == E_NOT_SUFFICIENT_BUFFER && actual > capacity) {
    DWRITE_HIT_TEST_METRICS* larger = (DWRITE_HIT_TEST_METRICS*) realloc(metrics, actual * sizeof(*metrics));
    if (larger) {
      metrics = larger;
      capacity = actual;
      hr =
          IDWriteTextLayout_HitTestTextRange(g_textLayout, start, length, originX, originY, metrics, capacity, &actual);
    }
  }

  if (SUCCEEDED(hr)) {
    for (UINT32 i = 0; i < actual; ++i) {
      if (metrics[i].width <= 0.0f || metrics[i].height <= 0.0f) {
        continue;
      }
      D2D1_RECT_F rect = {metrics[i].left, metrics[i].top, metrics[i].left + metrics[i].width,
                          metrics[i].top + metrics[i].height};
      ID2D1HwndRenderTarget_FillRectangle(g_textRenderTarget, &rect, (ID2D1Brush*) g_textSelectionBrush);
    }
  }
  free(metrics);
}

static void draw_text_view(HWND hwnd) {
  if (!ensure_text_render_target(hwnd) || !ensure_text_layout(hwnd)) {
    HDC hdc = GetDC(hwnd);
    if (hdc) {
      RECT client;
      GetClientRect(hwnd, &client);
      draw_centered_message(hdc, client, L"Unable to display clipboard text.");
      ReleaseDC(hwnd, hdc);
    }
    return;
  }

  RECT client;
  GetClientRect(hwnd, &client);
  D2D1_RECT_F clip = {(FLOAT) client.left, (FLOAT) client.top, (FLOAT) client.right, (FLOAT) client.bottom};
  D2D1_COLOR_F background = d2d_color_from_colorref(GetSysColor(COLOR_WINDOW));
  FLOAT originX = (FLOAT) (text_padding_x(hwnd) - g_textScrollX);
  FLOAT originY = (FLOAT) (text_padding_y(hwnd) - g_textScrollY);
  D2D1_POINT_2F origin = {originX, originY};

  ID2D1HwndRenderTarget_BeginDraw(g_textRenderTarget);
  ID2D1HwndRenderTarget_Clear(g_textRenderTarget, &background);
  ID2D1HwndRenderTarget_PushAxisAlignedClip(g_textRenderTarget, &clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

  draw_text_selection(hwnd, originX, originY);

  UINT32 selectionStart = 0;
  UINT32 selectionLength = 0;
  bool hasSelection = text_selection_range(&selectionStart, &selectionLength) && g_textSelectionTextBrush;
  DWRITE_TEXT_RANGE selectionRange = {selectionStart, selectionLength};
  if (hasSelection) {
    IDWriteTextLayout_SetDrawingEffect(g_textLayout, (IUnknown*) g_textSelectionTextBrush, selectionRange);
  }
  ID2D1HwndRenderTarget_DrawTextLayout(g_textRenderTarget, origin, g_textLayout, (ID2D1Brush*) g_textBrush,
                                       D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT | D2D1_DRAW_TEXT_OPTIONS_CLIP);
  if (hasSelection) {
    IDWriteTextLayout_SetDrawingEffect(g_textLayout, NULL, selectionRange);
  }

  ID2D1HwndRenderTarget_PopAxisAlignedClip(g_textRenderTarget);
  HRESULT hr = ID2D1HwndRenderTarget_EndDraw(g_textRenderTarget, NULL, NULL);
  if (hr == (HRESULT) D2DERR_RECREATE_TARGET) {
    release_text_render_target();
  }
}

static void release_image_bitmap(void) {
  if (g_imageBitmap) {
    ID2D1Bitmap_Release(g_imageBitmap);
    g_imageBitmap = NULL;
  }
}

static void release_image_checker_brush(void) {
  if (g_imageCheckerBrush) {
    ID2D1BitmapBrush_Release(g_imageCheckerBrush);
    g_imageCheckerBrush = NULL;
  }
}

static void release_image_render_target(void) {
  release_image_bitmap();
  release_image_checker_brush();
  if (g_imageRenderTarget) {
    ID2D1HwndRenderTarget_Release(g_imageRenderTarget);
    g_imageRenderTarget = NULL;
  }
}

static void release_graphics_resources(void) {
  release_text_render_target();
  release_text_format();
  release_image_render_target();
  if (g_dwriteFactory) {
    IDWriteFactory_Release(g_dwriteFactory);
    g_dwriteFactory = NULL;
  }
  if (g_wicFactory) {
    IWICImagingFactory_Release(g_wicFactory);
    g_wicFactory = NULL;
  }
  if (g_d2dFactory) {
    ID2D1Factory_Release(g_d2dFactory);
    g_d2dFactory = NULL;
  }
}

static bool initialize_graphics_resources(void) {
  HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory, NULL, (void**) &g_d2dFactory);
  if (FAILED(hr)) {
    return false;
  }

  hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &kIID_IDWriteFactory, (IUnknown**) &g_dwriteFactory);
  if (FAILED(hr)) {
    release_graphics_resources();
    return false;
  }

  hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
                        (void**) &g_wicFactory);
  if (FAILED(hr)) {
    release_graphics_resources();
    return false;
  }

  return true;
}

static bool ensure_image_render_target(HWND hwnd) {
  if (g_imageRenderTarget) {
    return true;
  }
  if (!g_d2dFactory || !hwnd) {
    return false;
  }

  RECT client;
  GetClientRect(hwnd, &client);
  D2D1_SIZE_U pixelSize = {(UINT32) max(client.right - client.left, 1), (UINT32) max(client.bottom - client.top, 1)};
  D2D1_RENDER_TARGET_PROPERTIES renderProperties = {0};
  renderProperties.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
  renderProperties.pixelFormat.format = DXGI_FORMAT_UNKNOWN;
  renderProperties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_UNKNOWN;
  renderProperties.dpiX = (FLOAT) USER_DEFAULT_SCREEN_DPI;
  renderProperties.dpiY = (FLOAT) USER_DEFAULT_SCREEN_DPI;
  renderProperties.usage = D2D1_RENDER_TARGET_USAGE_NONE;
  renderProperties.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;

  D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProperties = {0};
  hwndProperties.hwnd = hwnd;
  hwndProperties.pixelSize = pixelSize;
  hwndProperties.presentOptions = D2D1_PRESENT_OPTIONS_NONE;

  HRESULT hr =
      ID2D1Factory_CreateHwndRenderTarget(g_d2dFactory, &renderProperties, &hwndProperties, &g_imageRenderTarget);
  if (FAILED(hr)) {
    return false;
  }

  ID2D1HwndRenderTarget_SetDpi(g_imageRenderTarget, (FLOAT) USER_DEFAULT_SCREEN_DPI, (FLOAT) USER_DEFAULT_SCREEN_DPI);
  return true;
}

static bool resize_image_render_target(HWND hwnd) {
  if (!g_imageRenderTarget) {
    return true;
  }

  RECT client;
  GetClientRect(hwnd, &client);
  D2D1_SIZE_U pixelSize = {(UINT32) max(client.right - client.left, 1), (UINT32) max(client.bottom - client.top, 1)};
  return SUCCEEDED(ID2D1HwndRenderTarget_Resize(g_imageRenderTarget, &pixelSize));
}

static bool ensure_image_checker_brush(void) {
  if (g_imageCheckerBrush) {
    return true;
  }
  if (!g_imageRenderTarget) {
    return false;
  }

  uint32_t pixels[IMAGE_CHECKER_TILE_SIZE * IMAGE_CHECKER_TILE_SIZE];
  for (int y = 0; y < IMAGE_CHECKER_TILE_SIZE; ++y) {
    for (int x = 0; x < IMAGE_CHECKER_TILE_SIZE; ++x) {
      bool dark = ((x / IMAGE_CHECKER_CELL_SIZE) + (y / IMAGE_CHECKER_CELL_SIZE)) % 2 != 0;
      pixels[y * IMAGE_CHECKER_TILE_SIZE + x] = dark ? 0xffc8c8c8u : 0xfff0f0f0u;
    }
  }

  D2D1_BITMAP_PROPERTIES bitmapProperties = {0};
  bitmapProperties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
  bitmapProperties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
  bitmapProperties.dpiX = (FLOAT) USER_DEFAULT_SCREEN_DPI;
  bitmapProperties.dpiY = (FLOAT) USER_DEFAULT_SCREEN_DPI;

  ID2D1Bitmap* tileBitmap = NULL;
  D2D1_SIZE_U tileSize = {IMAGE_CHECKER_TILE_SIZE, IMAGE_CHECKER_TILE_SIZE};
  HRESULT hr =
      ID2D1HwndRenderTarget_CreateBitmap(g_imageRenderTarget, tileSize, pixels,
                                         IMAGE_CHECKER_TILE_SIZE * sizeof(pixels[0]), &bitmapProperties, &tileBitmap);
  if (SUCCEEDED(hr)) {
    D2D1_BITMAP_BRUSH_PROPERTIES brushProperties = {0};
    brushProperties.extendModeX = D2D1_EXTEND_MODE_WRAP;
    brushProperties.extendModeY = D2D1_EXTEND_MODE_WRAP;
    brushProperties.interpolationMode = D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR;
    hr = g_imageRenderTarget->lpVtbl->Base.CreateBitmapBrush((ID2D1RenderTarget*) g_imageRenderTarget, tileBitmap,
                                                             &brushProperties, NULL, &g_imageCheckerBrush);
  }

  if (tileBitmap) {
    ID2D1Bitmap_Release(tileBitmap);
  }
  if (FAILED(hr)) {
    release_image_checker_brush();
    return false;
  }
  return true;
}

static bool ensure_d2d_bitmap(HWND hwnd) {
  if (g_imageBitmap) {
    return true;
  }
  if (!g_wicFactory || g_snapshot.type != CLIPBOARD_CONTENT_IMAGE || !g_snapshot.bitmap ||
      !ensure_image_render_target(hwnd)) {
    return false;
  }

  IWICBitmap* wicBitmap = NULL;
  IWICFormatConverter* converter = NULL;
  HRESULT hr =
      IWICImagingFactory_CreateBitmapFromHBITMAP(g_wicFactory, g_snapshot.bitmap, NULL, WICBitmapUseAlpha, &wicBitmap);
  if (SUCCEEDED(hr)) {
    hr = IWICImagingFactory_CreateFormatConverter(g_wicFactory, &converter);
  }
  if (SUCCEEDED(hr)) {
    hr = IWICFormatConverter_Initialize(converter, (IWICBitmapSource*) wicBitmap, &GUID_WICPixelFormat32bppPBGRA,
                                        WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeMedianCut);
  }
  if (SUCCEEDED(hr)) {
    hr = ID2D1HwndRenderTarget_CreateBitmapFromWicBitmap(g_imageRenderTarget, (IWICBitmapSource*) converter, NULL,
                                                         &g_imageBitmap);
  }

  if (converter) {
    IWICFormatConverter_Release(converter);
  }
  if (wicBitmap) {
    IWICBitmap_Release(wicBitmap);
  }

  return SUCCEEDED(hr);
}

static RECT fitted_image_rect(int sourceWidth, int sourceHeight, int targetWidth, int targetHeight) {
  RECT rect = {0};
  if (sourceWidth <= 0 || sourceHeight <= 0 || targetWidth <= 0 || targetHeight <= 0) {
    return rect;
  }

  int destWidth = targetWidth;
  int destHeight = MulDiv(sourceHeight, targetWidth, sourceWidth);
  if (destHeight > targetHeight) {
    destHeight = targetHeight;
    destWidth = MulDiv(sourceWidth, targetHeight, sourceHeight);
  }

  rect.left = (targetWidth - destWidth) / 2;
  rect.top = (targetHeight - destHeight) / 2;
  rect.right = rect.left + destWidth;
  rect.bottom = rect.top + destHeight;
  return rect;
}

static void draw_image_background(const RECT* client) {
  if (!g_imageRenderTarget || !client) {
    return;
  }

  if (ensure_image_checker_brush()) {
    D2D1_RECT_F background = {(FLOAT) client->left, (FLOAT) client->top, (FLOAT) client->right, (FLOAT) client->bottom};
    ID2D1HwndRenderTarget_FillRectangle(g_imageRenderTarget, &background, (ID2D1Brush*) g_imageCheckerBrush);
    return;
  }

  const D2D1_COLOR_F fallback = {0.94f, 0.94f, 0.94f, 1.0f};
  ID2D1HwndRenderTarget_Clear(g_imageRenderTarget, &fallback);
}

static void draw_image_view(HWND hwnd) {
  if (!ensure_image_render_target(hwnd)) {
    HDC hdc = GetDC(hwnd);
    if (hdc) {
      RECT client;
      GetClientRect(hwnd, &client);
      draw_centered_message(hdc, client, L"Unable to display clipboard image.");
      ReleaseDC(hwnd, hdc);
    }
    return;
  }

  if (!ensure_d2d_bitmap(hwnd)) {
    RECT client;
    GetClientRect(hwnd, &client);
    ID2D1HwndRenderTarget_BeginDraw(g_imageRenderTarget);
    draw_image_background(&client);
    HRESULT hr = ID2D1HwndRenderTarget_EndDraw(g_imageRenderTarget, NULL, NULL);
    if (hr == (HRESULT) D2DERR_RECREATE_TARGET) {
      release_image_render_target();
    }
    return;
  }

  RECT client;
  GetClientRect(hwnd, &client);
  int clientWidth = client.right - client.left;
  int clientHeight = client.bottom - client.top;
  RECT imageRect = fitted_image_rect(g_snapshot.bitmapWidth, g_snapshot.bitmapHeight, clientWidth, clientHeight);
  D2D1_RECT_F destination = {(FLOAT) imageRect.left, (FLOAT) imageRect.top, (FLOAT) imageRect.right,
                             (FLOAT) imageRect.bottom};

  ID2D1HwndRenderTarget_BeginDraw(g_imageRenderTarget);
  draw_image_background(&client);
  ID2D1HwndRenderTarget_DrawBitmap(g_imageRenderTarget, g_imageBitmap, &destination, 1.0f,
                                   D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, NULL);
  HRESULT hr = ID2D1HwndRenderTarget_EndDraw(g_imageRenderTarget, NULL, NULL);
  if (hr == (HRESULT) D2DERR_RECREATE_TARGET) {
    release_image_render_target();
  }
}

static void invalidate_image_view(void) {
  if (g_imageView) {
    InvalidateRect(g_imageView, NULL, TRUE);
  }
}

static void update_views(HWND hwnd) {
  if (g_textView) {
    if (g_snapshot.type == CLIPBOARD_CONTENT_TEXT) {
      reset_text_view_state();
      ShowWindow(g_textView, SW_SHOW);
      if (GetFocus() == hwnd || GetFocus() == g_imageView) {
        SetFocus(g_textView);
      }
    } else {
      reset_text_view_state();
      ShowWindow(g_textView, SW_HIDE);
      if (GetFocus() == g_textView) {
        SetFocus(hwnd);
      }
    }
  }
  if (g_imageView) {
    if (g_snapshot.type == CLIPBOARD_CONTENT_IMAGE) {
      ShowWindow(g_imageView, SW_SHOW);
      SetFocus(g_imageView);
    } else {
      ShowWindow(g_imageView, SW_HIDE);
    }
  }

  layout_children(hwnd);
  update_window_title(hwnd);
  invalidate_image_view();
  InvalidateRect(hwnd, NULL, TRUE);
}

static void refresh_clipboard(HWND hwnd) {
  DWORD sequence = GetClipboardSequenceNumber();
  bool clipboardChanged = sequence == 0 || sequence != g_clipboardSequence;
  bool hadImage =
      g_snapshot.type == CLIPBOARD_CONTENT_IMAGE && g_snapshot.bitmapWidth > 0 && g_snapshot.bitmapHeight > 0;
  double preservedZoom = hadImage ? (g_imageZoomExplicit ? g_imageZoom : displayed_image_zoom(hwnd)) : 1.0;

  ClipboardSnapshot snapshot;
  read_clipboard_snapshot(hwnd, &snapshot);
  bool newImage = clipboardChanged && snapshot.type == CLIPBOARD_CONTENT_IMAGE;
  if (newImage) {
    g_imageZoomExplicit = true;
    g_imageZoom = clamp_image_zoom(preservedZoom);
  } else if (clipboardChanged || snapshot.type != CLIPBOARD_CONTENT_IMAGE) {
    reset_image_view_state();
  }
  g_clipboardSequence = sequence;
  release_image_bitmap();
  replace_clipboard_snapshot(&snapshot);
  if (newImage) {
    resize_window_for_image_zoom(hwnd);
  }
  update_views(hwnd);
}

static void draw_centered_message(HDC hdc, RECT contentRect, const wchar_t* message) {
  FillRect(hdc, &contentRect, (HBRUSH) (COLOR_WINDOW + 1));

  HFONT oldFont = (HFONT) SelectObject(hdc, ui_font());
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, RGB(80, 80, 80));
  DrawTextW(hdc, message ? message : L"", -1, &contentRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  SelectObject(hdc, oldFont);
}

static void paint_content(HWND hwnd, HDC hdc) {
  RECT client;
  GetClientRect(hwnd, &client);

  switch (g_snapshot.type) {
  case CLIPBOARD_CONTENT_TEXT:
    FillRect(hdc, &client, (HBRUSH) (COLOR_WINDOW + 1));
    break;
  case CLIPBOARD_CONTENT_IMAGE:
    FillRect(hdc, &client, (HBRUSH) (COLOR_WINDOW + 1));
    break;
  case CLIPBOARD_CONTENT_EMPTY:
  case CLIPBOARD_CONTENT_UNSUPPORTED:
  case CLIPBOARD_CONTENT_ERROR:
  default:
    draw_centered_message(hdc, client, g_snapshot.message);
    break;
  }
}

static AdjustWindowRectExForDpiFn adjust_window_rect_ex_for_dpi_proc(void) {
  static bool loaded = false;
  static AdjustWindowRectExForDpiFn fn = NULL;
  if (!loaded) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
      fn = load_adjust_window_rect_ex_for_dpi(user32);
    }
    loaded = true;
  }
  return fn;
}

static void adjust_window_rect_for_client(HWND hwnd, int clientWidth, int clientHeight, RECT* rect) {
  SetRect(rect, 0, 0, clientWidth, clientHeight);
  DWORD style = (DWORD) GetWindowLongPtrW(hwnd, GWL_STYLE);
  DWORD exStyle = (DWORD) GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  AdjustWindowRectExForDpiFn adjustForDpi = adjust_window_rect_ex_for_dpi_proc();
  if (adjustForDpi) {
    adjustForDpi(rect, style, FALSE, exStyle, g_currentDpi);
  } else {
    AdjustWindowRectEx(rect, style, FALSE, exStyle);
  }
}

static void nonclient_size_for_client(HWND hwnd, int clientWidth, int clientHeight, int* width, int* height) {
  RECT adjusted;
  adjust_window_rect_for_client(hwnd, clientWidth, clientHeight, &adjusted);
  *width = adjusted.right - adjusted.left - clientWidth;
  *height = adjusted.bottom - adjusted.top - clientHeight;
}

static double max_double(double a, double b) {
  return a > b ? a : b;
}

static double min_double(double a, double b) {
  return a < b ? a : b;
}

static bool image_window_size_for_zoom(HWND hwnd, double zoom, int* windowWidth, int* windowHeight) {
  if (!current_image_ratio(NULL) || !g_snapshot.bitmap) {
    return false;
  }

  int desiredClientWidth = rounded_zoom_dimension(g_snapshot.bitmapWidth, zoom);
  int desiredClientHeight = rounded_zoom_dimension(g_snapshot.bitmapHeight, zoom);
  int nonclientWidth = 0;
  int nonclientHeight = 0;
  nonclient_size_for_client(hwnd, desiredClientWidth, desiredClientHeight, &nonclientWidth, &nonclientHeight);

  int minWidth = scaled_metric(hwnd, WINDOW_MIN_WIDTH);
  int minHeight = scaled_metric(hwnd, WINDOW_MIN_HEIGHT);
  int minClientWidth = max(minWidth - nonclientWidth, 1);
  int minClientHeight = max(minHeight - nonclientHeight, 1);

  double minScale = max_double((double) minClientWidth / (double) g_snapshot.bitmapWidth,
                               (double) minClientHeight / (double) g_snapshot.bitmapHeight);
  double maxScale = (double) (INT_MAX / 4);

  HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
  MONITORINFO monitorInfo = {0};
  monitorInfo.cbSize = sizeof(monitorInfo);
  if (monitor && GetMonitorInfoW(monitor, &monitorInfo)) {
    int margin = scaled_metric(hwnd, 24);
    int workWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
    int workHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
    int maxWidth = workWidth - 2 * margin;
    int maxHeight = workHeight - 2 * margin;
    int maxClientWidth = max(maxWidth - nonclientWidth, 1);
    int maxClientHeight = max(maxHeight - nonclientHeight, 1);
    maxScale = min_double((double) maxClientWidth / (double) g_snapshot.bitmapWidth,
                          (double) maxClientHeight / (double) g_snapshot.bitmapHeight);
  }

  double desiredScale = max_double((double) desiredClientWidth / (double) g_snapshot.bitmapWidth,
                                   (double) desiredClientHeight / (double) g_snapshot.bitmapHeight);
  double scale = desiredScale;
  if (maxScale > 0.0) {
    if (minScale <= maxScale) {
      if (scale < minScale) {
        scale = minScale;
      }
      if (scale > maxScale) {
        scale = maxScale;
      }
    } else {
      scale = maxScale;
    }
  } else if (scale < minScale) {
    scale = minScale;
  }

  int clientWidth = rounded_zoom_dimension(g_snapshot.bitmapWidth, scale);
  int clientHeight = rounded_zoom_dimension(g_snapshot.bitmapHeight, scale);
  RECT desired;
  adjust_window_rect_for_client(hwnd, clientWidth, clientHeight, &desired);
  *windowWidth = desired.right - desired.left;
  *windowHeight = desired.bottom - desired.top;
  return true;
}

static void set_window_size_preserving_position(HWND hwnd, int windowWidth, int windowHeight) {
  HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
  MONITORINFO monitorInfo = {0};
  monitorInfo.cbSize = sizeof(monitorInfo);
  if (monitor && GetMonitorInfoW(monitor, &monitorInfo)) {
    int margin = scaled_metric(hwnd, 24);

    RECT current;
    GetWindowRect(hwnd, &current);
    int x = current.left;
    int y = current.top;
    if (x + windowWidth > monitorInfo.rcWork.right - margin) {
      x = monitorInfo.rcWork.right - margin - windowWidth;
    }
    if (y + windowHeight > monitorInfo.rcWork.bottom - margin) {
      y = monitorInfo.rcWork.bottom - margin - windowHeight;
    }
    if (x < monitorInfo.rcWork.left + margin) {
      x = monitorInfo.rcWork.left + margin;
    }
    if (y < monitorInfo.rcWork.top + margin) {
      y = monitorInfo.rcWork.top + margin;
    }
    SetWindowPos(hwnd, NULL, x, y, windowWidth, windowHeight, SWP_NOZORDER | SWP_NOACTIVATE);
  } else {
    SetWindowPos(hwnd, NULL, 0, 0, windowWidth, windowHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
}

static bool resize_window_for_image_zoom(HWND hwnd) {
  if (g_snapshot.type != CLIPBOARD_CONTENT_IMAGE || !g_snapshot.bitmap) {
    return false;
  }

  int windowWidth = 0;
  int windowHeight = 0;
  if (!image_window_size_for_zoom(hwnd, current_image_zoom(hwnd), &windowWidth, &windowHeight)) {
    return false;
  }

  set_window_size_preserving_position(hwnd, windowWidth, windowHeight);
  return true;
}

static void image_min_track_size(HWND hwnd, LONG* width, LONG* height) {
  int nonclientWidth = 0;
  int nonclientHeight = 0;
  nonclient_size_for_client(hwnd, 100, 100, &nonclientWidth, &nonclientHeight);

  int baseMinWidth = scaled_metric(hwnd, WINDOW_MIN_WIDTH);
  int baseMinHeight = scaled_metric(hwnd, WINDOW_MIN_HEIGHT);
  if (!current_image_ratio(NULL)) {
    *width = (LONG) baseMinWidth;
    *height = (LONG) baseMinHeight;
    return;
  }

  int minClientWidth = max(baseMinWidth - nonclientWidth, 1);
  int minClientHeight = max(baseMinHeight - nonclientHeight, 1);
  double scale = max_double((double) minClientWidth / (double) g_snapshot.bitmapWidth,
                            (double) minClientHeight / (double) g_snapshot.bitmapHeight);
  int clientWidth = rounded_zoom_dimension(g_snapshot.bitmapWidth, scale);
  int clientHeight = rounded_zoom_dimension(g_snapshot.bitmapHeight, scale);

  RECT adjusted;
  adjust_window_rect_for_client(hwnd, clientWidth, clientHeight, &adjusted);
  *width = (LONG) max(adjusted.right - adjusted.left, baseMinWidth);
  *height = (LONG) max(adjusted.bottom - adjusted.top, baseMinHeight);
}

static void grow_client_size_to_minimum(HWND hwnd, int* clientWidth, int* clientHeight) {
  int nonclientWidth = 0;
  int nonclientHeight = 0;
  nonclient_size_for_client(hwnd, *clientWidth, *clientHeight, &nonclientWidth, &nonclientHeight);

  int minClientWidth = max(scaled_metric(hwnd, WINDOW_MIN_WIDTH) - nonclientWidth, 1);
  int minClientHeight = max(scaled_metric(hwnd, WINDOW_MIN_HEIGHT) - nonclientHeight, 1);
  double scale = max_double((double) *clientWidth / (double) g_snapshot.bitmapWidth,
                            (double) *clientHeight / (double) g_snapshot.bitmapHeight);
  scale = max_double(scale, (double) minClientWidth / (double) g_snapshot.bitmapWidth);
  scale = max_double(scale, (double) minClientHeight / (double) g_snapshot.bitmapHeight);

  *clientWidth = rounded_zoom_dimension(g_snapshot.bitmapWidth, scale);
  *clientHeight = rounded_zoom_dimension(g_snapshot.bitmapHeight, scale);
}

static void apply_sizing_window_size(WPARAM edge, RECT* rect, int width, int height) {
  switch (edge) {
  case WMSZ_LEFT: {
    int centerY = rect->top + (rect->bottom - rect->top) / 2;
    rect->left = rect->right - width;
    rect->top = centerY - height / 2;
    rect->bottom = rect->top + height;
    break;
  }
  case WMSZ_RIGHT: {
    int centerY = rect->top + (rect->bottom - rect->top) / 2;
    rect->right = rect->left + width;
    rect->top = centerY - height / 2;
    rect->bottom = rect->top + height;
    break;
  }
  case WMSZ_TOP: {
    int centerX = rect->left + (rect->right - rect->left) / 2;
    rect->top = rect->bottom - height;
    rect->left = centerX - width / 2;
    rect->right = rect->left + width;
    break;
  }
  case WMSZ_BOTTOM: {
    int centerX = rect->left + (rect->right - rect->left) / 2;
    rect->bottom = rect->top + height;
    rect->left = centerX - width / 2;
    rect->right = rect->left + width;
    break;
  }
  case WMSZ_TOPLEFT:
    rect->left = rect->right - width;
    rect->top = rect->bottom - height;
    break;
  case WMSZ_TOPRIGHT:
    rect->right = rect->left + width;
    rect->top = rect->bottom - height;
    break;
  case WMSZ_BOTTOMLEFT:
    rect->left = rect->right - width;
    rect->bottom = rect->top + height;
    break;
  case WMSZ_BOTTOMRIGHT:
  default:
    rect->right = rect->left + width;
    rect->bottom = rect->top + height;
    break;
  }
}

static bool enforce_image_aspect_sizing(HWND hwnd, WPARAM edge, RECT* rect) {
  double ratio = 1.0;
  if (!rect || !current_image_ratio(&ratio)) {
    return false;
  }

  int nonclientWidth = 0;
  int nonclientHeight = 0;
  nonclient_size_for_client(hwnd, 100, 100, &nonclientWidth, &nonclientHeight);

  int windowWidth = max(rect->right - rect->left, nonclientWidth + 1);
  int windowHeight = max(rect->bottom - rect->top, nonclientHeight + 1);
  int clientWidth = max(windowWidth - nonclientWidth, 1);
  int clientHeight = max(windowHeight - nonclientHeight, 1);

  switch (edge) {
  case WMSZ_LEFT:
  case WMSZ_RIGHT:
    clientHeight = rounded_dimension((double) clientWidth / ratio);
    break;
  case WMSZ_TOP:
  case WMSZ_BOTTOM:
    clientWidth = rounded_dimension((double) clientHeight * ratio);
    break;
  default: {
    int heightFromWidth = rounded_dimension((double) clientWidth / ratio);
    int widthFromHeight = rounded_dimension((double) clientHeight * ratio);
    int windowHeightFromWidth = heightFromWidth + nonclientHeight;
    int windowWidthFromHeight = widthFromHeight + nonclientWidth;
    if (abs(windowHeightFromWidth - windowHeight) <= abs(windowWidthFromHeight - windowWidth)) {
      clientHeight = heightFromWidth;
    } else {
      clientWidth = widthFromHeight;
    }
    break;
  }
  }

  grow_client_size_to_minimum(hwnd, &clientWidth, &clientHeight);
  apply_sizing_window_size(edge, rect, clientWidth + nonclientWidth, clientHeight + nonclientHeight);
  return true;
}

static bool set_image_zoom(HWND hwnd, double zoom) {
  if (g_snapshot.type != CLIPBOARD_CONTENT_IMAGE || !g_snapshot.bitmap) {
    return false;
  }

  g_imageZoomExplicit = true;
  g_imageZoom = clamp_image_zoom(zoom);
  resize_window_for_image_zoom(hwnd);
  layout_children(hwnd);
  update_window_title(hwnd);
  invalidate_image_view();
  return true;
}

static bool set_text_zoom(HWND hwnd, double zoom) {
  if (g_snapshot.type != CLIPBOARD_CONTENT_TEXT) {
    return false;
  }

  g_textZoom = clamp_text_zoom(zoom);
  create_text_format();
  clamp_text_scroll(hwnd);
  InvalidateRect(hwnd, NULL, TRUE);

  HWND parent = GetParent(hwnd);
  update_window_title(parent ? parent : hwnd);
  return true;
}

static bool digit_key_value(WPARAM key, int* value) {
  if (key >= L'0' && key <= L'9') {
    *value = (int) (key - L'0');
    return true;
  }
  if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9) {
    *value = (int) (key - VK_NUMPAD0);
    return true;
  }
  return false;
}

static double regular_digit_zoom(int digit) {
  static const double zoomByDigit[10] = {
      2.0, 1.0, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9,
  };

  if (digit < 0 || digit > 9) {
    return 1.0;
  }
  return zoomByDigit[digit];
}

typedef bool (*SetZoomFn)(HWND hwnd, double zoom);

static bool handle_zoom_key(HWND hwnd, WPARAM key, double currentZoom, SetZoomFn setZoom) {
  bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
  bool altDown = (GetKeyState(VK_MENU) & 0x8000) != 0;
  bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  if (ctrlDown) {
    return false;
  }

  if (!altDown && (key == VK_SUBTRACT || key == VK_OEM_MINUS)) {
    return setZoom(hwnd, currentZoom - 0.1);
  }
  if (!altDown && (key == VK_ADD || (key == VK_OEM_PLUS && shiftDown))) {
    return setZoom(hwnd, currentZoom + 0.1);
  }

  int digit = 0;
  if (!digit_key_value(key, &digit)) {
    return false;
  }

  if (altDown) {
    if (digit < 1 || digit > 9) {
      return false;
    }
    return setZoom(hwnd, (double) (10 - digit) / 10.0);
  }

  if (digit >= 0 && digit <= 9) {
    return setZoom(hwnd, regular_digit_zoom(digit));
  }

  return false;
}

static bool handle_image_key(HWND hwnd, WPARAM key) {
  if (g_snapshot.type != CLIPBOARD_CONTENT_IMAGE) {
    return false;
  }

  return handle_zoom_key(hwnd, key, displayed_image_zoom(hwnd), set_image_zoom);
}

static void scroll_text_by(HWND hwnd, int dx, int dy) {
  set_text_scroll(hwnd, g_textScrollX + dx, g_textScrollY + dy);
}

static void handle_text_scroll_message(HWND hwnd, int bar, UINT code) {
  int maxX = 0;
  int maxY = 0;
  text_max_scroll(hwnd, &maxX, &maxY);
  int line = text_line_height(hwnd);
  int page = 0;
  int current = 0;
  int maximum = 0;
  if (bar == SB_VERT) {
    RECT client;
    GetClientRect(hwnd, &client);
    page = max((client.bottom - client.top) - line, line);
    current = g_textScrollY;
    maximum = maxY;
  } else {
    RECT client;
    GetClientRect(hwnd, &client);
    page = max((client.right - client.left) - text_char_width(hwnd), text_char_width(hwnd));
    current = g_textScrollX;
    maximum = maxX;
    line = text_char_width(hwnd);
  }

  switch (code) {
  case SB_LINEUP:
    current -= line;
    break;
  case SB_LINEDOWN:
    current += line;
    break;
  case SB_PAGEUP:
    current -= page;
    break;
  case SB_PAGEDOWN:
    current += page;
    break;
  case SB_TOP:
    current = 0;
    break;
  case SB_BOTTOM:
    current = maximum;
    break;
  case SB_THUMBTRACK:
  case SB_THUMBPOSITION: {
    SCROLLINFO si = {0};
    si.cbSize = sizeof(si);
    si.fMask = SIF_TRACKPOS;
    if (GetScrollInfo(hwnd, bar, &si)) {
      current = si.nTrackPos;
    }
    break;
  }
  default:
    break;
  }

  if (bar == SB_VERT) {
    set_text_scroll(hwnd, g_textScrollX, current);
  } else {
    set_text_scroll(hwnd, current, g_textScrollY);
  }
}

static UINT wheel_scroll_lines(void) {
  UINT lines = 3;
  SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
  return lines == 0 ? 3 : lines;
}

static void handle_text_mouse_wheel(HWND hwnd, int delta) {
  g_textWheelRemainder += delta;
  int steps = g_textWheelRemainder / WHEEL_DELTA;
  g_textWheelRemainder %= WHEEL_DELTA;
  if (steps == 0) {
    return;
  }

  UINT lines = wheel_scroll_lines();
  if (lines == WHEEL_PAGESCROLL) {
    RECT client;
    GetClientRect(hwnd, &client);
    scroll_text_by(hwnd, 0, -steps * max(client.bottom - client.top, text_line_height(hwnd)));
  } else {
    scroll_text_by(hwnd, 0, -steps * (int) lines * text_line_height(hwnd));
  }
}

static void handle_text_mouse_hwheel(HWND hwnd, int delta) {
  g_textHWheelRemainder += delta;
  int steps = g_textHWheelRemainder / WHEEL_DELTA;
  g_textHWheelRemainder %= WHEEL_DELTA;
  if (steps == 0) {
    return;
  }
  scroll_text_by(hwnd, steps * 3 * text_char_width(hwnd), 0);
}

static bool handle_text_key(HWND hwnd, WPARAM key) {
  bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

  if (ctrlDown && (key == L'A' || key == L'a')) {
    g_textSelectionAnchor = 0;
    g_textSelectionCaret = text_layout_length();
    InvalidateRect(hwnd, NULL, TRUE);
    return true;
  }
  if (ctrlDown && (key == L'C' || key == L'c')) {
    copy_selected_text(hwnd);
    return true;
  }

  if (ctrlDown && key != VK_HOME && key != VK_END) {
    return false;
  }

  if (handle_zoom_key(hwnd, key, g_textZoom, set_text_zoom)) {
    return true;
  }

  RECT client;
  GetClientRect(hwnd, &client);
  int line = text_line_height(hwnd);
  int pageY = max((client.bottom - client.top) - line, line);
  int maxX = 0;
  int maxY = 0;
  text_max_scroll(hwnd, &maxX, &maxY);

  switch (key) {
  case VK_UP:
    scroll_text_by(hwnd, 0, -line);
    return true;
  case VK_DOWN:
    scroll_text_by(hwnd, 0, line);
    return true;
  case VK_LEFT:
    scroll_text_by(hwnd, -text_char_width(hwnd), 0);
    return true;
  case VK_RIGHT:
    scroll_text_by(hwnd, text_char_width(hwnd), 0);
    return true;
  case VK_PRIOR:
    scroll_text_by(hwnd, 0, -pageY);
    return true;
  case VK_NEXT:
    scroll_text_by(hwnd, 0, pageY);
    return true;
  case VK_HOME:
    if (ctrlDown) {
      set_text_scroll(hwnd, 0, 0);
    } else {
      set_text_scroll(hwnd, 0, g_textScrollY);
    }
    return true;
  case VK_END:
    if (ctrlDown) {
      set_text_scroll(hwnd, maxX, maxY);
    } else {
      set_text_scroll(hwnd, maxX, g_textScrollY);
    }
    return true;
  case VK_SPACE:
    scroll_text_by(hwnd, 0, pageY);
    return true;
  default:
    return false;
  }
}

static void update_text_autoscroll(HWND hwnd) {
  if (!g_textSelecting) {
    return;
  }

  POINT point;
  GetCursorPos(&point);
  ScreenToClient(hwnd, &point);

  RECT client;
  GetClientRect(hwnd, &client);
  int dx = 0;
  int dy = 0;
  if (point.x < client.left) {
    dx = -text_char_width(hwnd);
  } else if (point.x >= client.right) {
    dx = text_char_width(hwnd);
  }
  if (point.y < client.top) {
    dy = -text_line_height(hwnd);
  } else if (point.y >= client.bottom) {
    dy = text_line_height(hwnd);
  }

  if (dx != 0 || dy != 0) {
    scroll_text_by(hwnd, dx, dy);
    update_text_selection_at_point(hwnd, point.x, point.y, true);
  }
}

static LRESULT CALLBACK text_view_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_ERASEBKGND:
    return 1;
  case WM_SETFOCUS:
  case WM_KILLFOCUS:
    InvalidateRect(hwnd, NULL, TRUE);
    return 0;
  case WM_LBUTTONDOWN:
    SetFocus(hwnd);
    SetCapture(hwnd);
    g_textSelecting = true;
    update_text_selection_at_point(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), false);
    SetTimer(hwnd, TEXT_AUTOSCROLL_TIMER, TEXT_AUTOSCROLL_INTERVAL_MS, NULL);
    return 0;
  case WM_MOUSEMOVE:
    if (g_textSelecting && GetCapture() == hwnd) {
      update_text_selection_at_point(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), true);
    }
    return 0;
  case WM_LBUTTONUP:
    if (g_textSelecting && GetCapture() == hwnd) {
      update_text_selection_at_point(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), true);
      ReleaseCapture();
    }
    g_textSelecting = false;
    KillTimer(hwnd, TEXT_AUTOSCROLL_TIMER);
    return 0;
  case WM_CAPTURECHANGED:
    g_textSelecting = false;
    KillTimer(hwnd, TEXT_AUTOSCROLL_TIMER);
    return 0;
  case WM_TIMER:
    if (wParam == TEXT_AUTOSCROLL_TIMER) {
      update_text_autoscroll(hwnd);
      return 0;
    }
    break;
  case WM_MOUSEWHEEL:
    handle_text_mouse_wheel(hwnd, GET_WHEEL_DELTA_WPARAM(wParam));
    return 0;
  case WM_MOUSEHWHEEL:
    handle_text_mouse_hwheel(hwnd, GET_WHEEL_DELTA_WPARAM(wParam));
    return 0;
  case WM_VSCROLL:
    handle_text_scroll_message(hwnd, SB_VERT, LOWORD(wParam));
    return 0;
  case WM_HSCROLL:
    handle_text_scroll_message(hwnd, SB_HORZ, LOWORD(wParam));
    return 0;
  case WM_KEYDOWN:
  case WM_SYSKEYDOWN:
    if (handle_text_key(hwnd, wParam)) {
      return 0;
    }
    break;
  case WM_SIZE:
    resize_text_render_target(hwnd);
    clamp_text_scroll(hwnd);
    InvalidateRect(hwnd, NULL, TRUE);
    return 0;
  case WM_SYSCOLORCHANGE:
    release_text_render_target();
    InvalidateRect(hwnd, NULL, TRUE);
    return 0;
  case WM_PAINT: {
    PAINTSTRUCT ps;
    BeginPaint(hwnd, &ps);
    EndPaint(hwnd, &ps);
    draw_text_view(hwnd);
    return 0;
  }
  case WM_DESTROY:
    if (hwnd == g_textView) {
      release_text_render_target();
      g_textView = NULL;
    }
    return 0;
  default:
    break;
  }

  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool register_text_view_class(HINSTANCE instance) {
  WNDCLASSEXW wc = {0};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = text_view_proc;
  wc.hInstance = instance;
  wc.lpszClassName = kTextViewClassName;
  wc.style = CS_DBLCLKS;
  wc.hCursor = LoadCursorW(NULL, IDC_IBEAM);
  wc.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);
  return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

static LRESULT CALLBACK image_view_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_ERASEBKGND:
    return 1;
  case WM_LBUTTONDOWN:
    SetFocus(hwnd);
    return 0;
  case WM_LBUTTONDBLCLK:
    set_image_zoom(GetParent(hwnd), 1.0);
    SetFocus(hwnd);
    return 0;
  case WM_KEYDOWN:
  case WM_SYSKEYDOWN:
    if (handle_image_key(GetParent(hwnd), wParam)) {
      return 0;
    }
    break;
  case WM_SIZE:
    resize_image_render_target(hwnd);
    invalidate_image_view();
    return 0;
  case WM_PAINT: {
    PAINTSTRUCT ps;
    BeginPaint(hwnd, &ps);
    EndPaint(hwnd, &ps);
    draw_image_view(hwnd);
    return 0;
  }
  case WM_DESTROY:
    if (hwnd == g_imageView) {
      release_image_render_target();
      g_imageView = NULL;
    }
    return 0;
  default:
    break;
  }

  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool register_image_view_class(HINSTANCE instance) {
  WNDCLASSEXW wc = {0};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = image_view_proc;
  wc.hInstance = instance;
  wc.lpszClassName = kImageViewClassName;
  wc.style = CS_DBLCLKS;
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);
  return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

static void cleanup_image_view_resources(void) {
  release_graphics_resources();
}

static void request_previous_instance_shutdown(void) {
  HWND existing = FindWindowW(kWindowClassName, NULL);
  if (!existing) {
    return;
  }

  DWORD existingPid = 0;
  GetWindowThreadProcessId(existing, &existingPid);
  if (existingPid == GetCurrentProcessId()) {
    return;
  }

  PostMessageW(existing, WM_APP_EXIT, 0, 0);
}

static void release_single_instance_mutex(void) {
  if (g_singleInstanceMutex) {
    ReleaseMutex(g_singleInstanceMutex);
    CloseHandle(g_singleInstanceMutex);
    g_singleInstanceMutex = NULL;
  }
}

static bool acquire_single_instance(void) {
  g_singleInstanceMutex = CreateMutexW(NULL, FALSE, kSingleInstanceMutexName);
  if (!g_singleInstanceMutex) {
    MessageBoxW(NULL, L"Unable to create the single-instance mutex.", kAppTitle, MB_ICONERROR | MB_OK);
    return false;
  }

  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    request_previous_instance_shutdown();
  }

  DWORD waitResult = WaitForSingleObject(g_singleInstanceMutex, 5000);
  if (waitResult == WAIT_OBJECT_0 || waitResult == WAIT_ABANDONED) {
    return true;
  }

  if (waitResult == WAIT_TIMEOUT) {
    MessageBoxW(NULL, L"Timed out waiting for the previous Board instance to exit.", kAppTitle, MB_ICONERROR | MB_OK);
  } else {
    MessageBoxW(NULL, L"Unable to wait for the single-instance mutex.", kAppTitle, MB_ICONERROR | MB_OK);
  }

  CloseHandle(g_singleInstanceMutex);
  g_singleInstanceMutex = NULL;
  return false;
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_CREATE: {
    HINSTANCE instance = ((CREATESTRUCTW*) lParam)->hInstance;
    recreate_fonts(hwnd);

    g_textView = CreateWindowExW(WS_EX_CLIENTEDGE, kTextViewClassName, L"",
                                 WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP | WS_CLIPSIBLINGS, 0, 0,
                                 0, 0, hwnd, (HMENU) (INT_PTR) IDC_TEXT_VIEW, instance, NULL);
    if (!g_textView) {
      return -1;
    }
    create_text_tooltip(hwnd);

    g_imageView = CreateWindowExW(0, kImageViewClassName, L"", WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP, 0, 0, 0, 0,
                                  hwnd, NULL, instance, NULL);
    if (!g_imageView) {
      return -1;
    }
    ShowWindow(g_imageView, SW_HIDE);

    layout_children(hwnd);
    refresh_clipboard(hwnd);

    if (!AddClipboardFormatListener(hwnd)) {
      MessageBoxW(hwnd, L"Unable to register for clipboard updates.", kAppTitle, MB_ICONERROR | MB_OK);
      return -1;
    }
    g_clipboardListenerRegistered = true;
    return 0;
  }
  case WM_APP_EXIT:
    DestroyWindow(hwnd);
    return 0;
  case WM_CLIPBOARDUPDATE:
    refresh_clipboard(hwnd);
    return 0;
  case WM_LBUTTONDOWN:
    if (g_snapshot.type == CLIPBOARD_CONTENT_IMAGE) {
      SetFocus(g_imageView ? g_imageView : hwnd);
    }
    return 0;
  case WM_LBUTTONDBLCLK:
    if (g_snapshot.type == CLIPBOARD_CONTENT_IMAGE) {
      if (set_image_zoom(hwnd, 1.0)) {
        SetFocus(g_imageView ? g_imageView : hwnd);
      }
    }
    return 0;
  case WM_KEYDOWN:
  case WM_SYSKEYDOWN:
    if (g_snapshot.type == CLIPBOARD_CONTENT_TEXT && g_textView && handle_text_key(g_textView, wParam)) {
      return 0;
    }
    if (handle_image_key(hwnd, wParam)) {
      return 0;
    }
    break;
  case WM_DPICHANGED: {
    UINT newDpi = LOWORD(wParam);
    RECT* suggestedRect = (RECT*) lParam;
    if (suggestedRect) {
      SetWindowPos(hwnd, NULL, suggestedRect->left, suggestedRect->top, suggestedRect->right - suggestedRect->left,
                   suggestedRect->bottom - suggestedRect->top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    recreate_fonts_for_dpi(newDpi ? newDpi : dpi_for_window(hwnd));
    apply_text_font();
    refresh_text_tooltip(hwnd);
    if (g_snapshot.type == CLIPBOARD_CONTENT_IMAGE) {
      resize_window_for_image_zoom(hwnd);
    }
    layout_children(hwnd);
    update_window_title(hwnd);
    invalidate_image_view();
    InvalidateRect(hwnd, NULL, TRUE);
    return 0;
  }
  case WM_SETFOCUS:
    if (g_snapshot.type == CLIPBOARD_CONTENT_TEXT && g_textView) {
      SetFocus(g_textView);
    }
    return 0;
  case WM_SIZE:
    layout_children(hwnd);
    update_window_title(hwnd);
    invalidate_image_view();
    InvalidateRect(hwnd, NULL, TRUE);
    return 0;
  case WM_SIZING:
    if (enforce_image_aspect_sizing(hwnd, wParam, (RECT*) lParam)) {
      return TRUE;
    }
    break;
  case WM_EXITSIZEMOVE:
    if (g_snapshot.type == CLIPBOARD_CONTENT_IMAGE) {
      g_imageZoomExplicit = true;
      g_imageZoom = clamp_image_zoom(displayed_image_zoom(hwnd));
      update_window_title(hwnd);
    }
    return 0;
  case WM_GETMINMAXINFO: {
    MINMAXINFO* minMax = (MINMAXINFO*) lParam;
    image_min_track_size(hwnd, &minMax->ptMinTrackSize.x, &minMax->ptMinTrackSize.y);
    return 0;
  }
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    paint_content(hwnd, hdc);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_DESTROY:
    if (g_clipboardListenerRegistered) {
      RemoveClipboardFormatListener(hwnd);
      g_clipboardListenerRegistered = false;
    }
    free_clipboard_snapshot(&g_snapshot);
    destroy_text_tooltip();
    free_fonts();
    cleanup_image_view_resources();
    release_single_instance_mutex();
    PostQuitMessage(0);
    return 0;
  default:
    return DefWindowProcW(hwnd, msg, wParam, lParam);
  }

  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR commandLine, int nCmdShow) {
  (void) hPrevInstance;
  (void) commandLine;

  enable_process_dpi_awareness();
  initialize_common_controls();
  g_currentDpi = dpi_for_window(NULL);

  HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  if (FAILED(hr)) {
    MessageBoxW(NULL, L"Unable to initialize COM.", kAppTitle, MB_ICONERROR | MB_OK);
    return 1;
  }
  g_comInitialized = true;

  if (!initialize_graphics_resources()) {
    MessageBoxW(NULL, L"Unable to initialize rendering.", kAppTitle, MB_ICONERROR | MB_OK);
    cleanup_image_view_resources();
    CoUninitialize();
    g_comInitialized = false;
    return 1;
  }

  if (!acquire_single_instance()) {
    cleanup_image_view_resources();
    CoUninitialize();
    g_comInitialized = false;
    return 1;
  }

  WNDCLASSEXW wc = {0};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = window_proc;
  wc.hInstance = hInstance;
  wc.lpszClassName = kWindowClassName;
  wc.style = CS_DBLCLKS;
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hIcon = (HICON) LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
  wc.hIconSm = (HICON) LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 16, 16, 0);
  wc.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);

  if (!RegisterClassExW(&wc)) {
    cleanup_image_view_resources();
    release_single_instance_mutex();
    CoUninitialize();
    g_comInitialized = false;
    return 1;
  }

  if (!register_text_view_class(hInstance)) {
    cleanup_image_view_resources();
    release_single_instance_mutex();
    CoUninitialize();
    g_comInitialized = false;
    return 1;
  }

  if (!register_image_view_class(hInstance)) {
    cleanup_image_view_resources();
    release_single_instance_mutex();
    CoUninitialize();
    g_comInitialized = false;
    return 1;
  }

  HWND hwnd =
      CreateWindowExW(0, kWindowClassName, kAppTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                      scale_for_dpi(900, g_currentDpi), scale_for_dpi(650, g_currentDpi), NULL, NULL, hInstance, NULL);
  if (!hwnd) {
    cleanup_image_view_resources();
    release_single_instance_mutex();
    CoUninitialize();
    g_comInitialized = false;
    return 1;
  }

  ShowWindow(hwnd, nCmdShow);
  UpdateWindow(hwnd);

  MSG msg;
  while (GetMessageW(&msg, NULL, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  cleanup_image_view_resources();
  if (g_comInitialized) {
    CoUninitialize();
    g_comInitialized = false;
  }

  return (int) msg.wParam;
}
