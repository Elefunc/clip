#ifndef UNICODE
#define UNICODE
#endif

#ifndef _UNICODE
#define _UNICODE
#endif

#ifndef COBJMACROS
#define COBJMACROS
#endif

#include <windows.h>
#include <objidl.h>
#include <ocidl.h>
#include <wincodec.h>

#include <fcntl.h>
#include <io.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static bool g_debug_enabled = false;

static bool string_truthy(const wchar_t* value) {
  if (!value || !*value) {
    return false;
  }
  if (_wcsicmp(value, L"1") == 0 || _wcsicmp(value, L"true") == 0 || _wcsicmp(value, L"yes") == 0 ||
      _wcsicmp(value, L"on") == 0) {
    return true;
  }
  return false;
}

static bool should_log_level(const char* level) {
  if (!level) {
    return g_debug_enabled;
  }
  if (strcmp(level, "ERROR") == 0) {
    return true;
  }
  return g_debug_enabled;
}

static bool load_debug_flag(void) {
  const wchar_t* names[] = {L"debug", L"DEBUG"};
  wchar_t buffer[32];
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
    DWORD len = GetEnvironmentVariableW(names[i], buffer, (DWORD) (sizeof(buffer) / sizeof(buffer[0])));
    if (len == 0) {
      continue;
    }
    if (len >= (DWORD) (sizeof(buffer) / sizeof(buffer[0]))) {
      len = (DWORD) (sizeof(buffer) / sizeof(buffer[0])) - 1;
    }
    buffer[len] = L'\0';
    return string_truthy(buffer);
  }

  const wchar_t* env = _wgetenv(L"debug");
  if (!env) {
    env = _wgetenv(L"DEBUG");
  }
  return string_truthy(env);
}

static void log_line(const char* level, const char* fmt, ...) {
  if (!should_log_level(level)) {
    return;
  }
  SYSTEMTIME st;
  GetLocalTime(&st);
  fprintf(stderr, "[%02u:%02u:%02u.%03u] %s: ", (unsigned) st.wHour, (unsigned) st.wMinute, (unsigned) st.wSecond,
          (unsigned) st.wMilliseconds, level);
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fputc('\n', stderr);
  fflush(stderr);
}

typedef enum {
  OUTPUT_MODE_AUTO = 0,
  OUTPUT_MODE_TEXT,
  OUTPUT_MODE_IMAGE,
} output_mode;

static bool parse_args(int argc, wchar_t** argv, output_mode* mode) {
  if (!mode) {
    return false;
  }
  *mode = OUTPUT_MODE_AUTO;

  for (int i = 1; i < argc; ++i) {
    const wchar_t* arg = argv[i];
    if (!arg) {
      continue;
    }

    if (wcscmp(arg, L"--type") == 0 || wcscmp(arg, L"-t") == 0) {
      if (i + 1 >= argc) {
        log_line("ERROR", "--type requires a value (auto, text, or image)");
        return false;
      }
      arg = argv[++i];
      if (_wcsicmp(arg, L"auto") == 0) {
        *mode = OUTPUT_MODE_AUTO;
      } else if (_wcsicmp(arg, L"text") == 0) {
        *mode = OUTPUT_MODE_TEXT;
      } else if (_wcsicmp(arg, L"image") == 0) {
        *mode = OUTPUT_MODE_IMAGE;
      } else {
        log_line("ERROR", "Unknown --type value: %ls", arg);
        return false;
      }
      continue;
    }

    if (wcscmp(arg, L"--text") == 0) {
      *mode = OUTPUT_MODE_TEXT;
    } else if (wcscmp(arg, L"--image") == 0) {
      *mode = OUTPUT_MODE_IMAGE;
    } else if (wcscmp(arg, L"--auto") == 0) {
      *mode = OUTPUT_MODE_AUTO;
    } else {
      log_line("ERROR", "Unknown argument: %ls", arg);
      return false;
    }
  }

  return true;
}

static size_t dib_bits_offset(const BITMAPINFOHEADER* header) {
  size_t offset = header->biSize;
  if (header->biCompression == BI_BITFIELDS) {
    offset += 3 * sizeof(DWORD);
  }
  size_t colors = header->biClrUsed;
  if (colors == 0 && header->biBitCount <= 8) {
    colors = 1u << header->biBitCount;
  }
  offset += colors * sizeof(RGBQUAD);
  return offset;
}

static HBITMAP bitmap_from_dib(UINT format) {
  if (!IsClipboardFormatAvailable(format)) {
    return NULL;
  }

  HANDLE handle = GetClipboardData(format);
  if (!handle) {
    log_line("ERROR", "GetClipboardData failed for format %u (%lu)", format, (unsigned long) GetLastError());
    return NULL;
  }

  void* locked = GlobalLock(handle);
  if (!locked) {
    log_line("ERROR", "GlobalLock failed for DIB (%lu)", (unsigned long) GetLastError());
    return NULL;
  }

  SIZE_T totalSize = GlobalSize(handle);
  if (totalSize < sizeof(BITMAPINFOHEADER)) {
    log_line("ERROR", "DIB buffer too small (%zu bytes)", (size_t) totalSize);
    GlobalUnlock(handle);
    return NULL;
  }

  BITMAPINFOHEADER* header = (BITMAPINFOHEADER*) locked;
  if (header->biSize < sizeof(BITMAPINFOHEADER)) {
    log_line("ERROR", "Invalid DIB header size (%u)", (unsigned) header->biSize);
    GlobalUnlock(handle);
    return NULL;
  }

  size_t offset = dib_bits_offset(header);
  if (offset >= totalSize) {
    log_line("ERROR", "DIB pixel data offset %zu exceeds buffer size %zu", offset, (size_t) totalSize);
    GlobalUnlock(handle);
    return NULL;
  }

  BYTE* bits = (BYTE*) locked + offset;
  HDC screenDC = GetDC(NULL);
  if (!screenDC) {
    log_line("ERROR", "GetDC failed (%lu)", (unsigned long) GetLastError());
    GlobalUnlock(handle);
    return NULL;
  }

  HBITMAP bitmap = CreateDIBitmap(screenDC, header, CBM_INIT, bits, (BITMAPINFO*) header, DIB_RGB_COLORS);
  ReleaseDC(NULL, screenDC);
  GlobalUnlock(handle);

  if (!bitmap) {
    log_line("ERROR", "CreateDIBitmap failed (%lu)", (unsigned long) GetLastError());
  }

  return bitmap;
}

static HBITMAP copy_bitmap_handle(HBITMAP source) {
  if (!source) {
    return NULL;
  }
  return (HBITMAP) CopyImage(source, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
}

static HBITMAP acquire_clipboard_bitmap(void) {
  HBITMAP dup = NULL;

  if (IsClipboardFormatAvailable(CF_BITMAP)) {
    HBITMAP clipboardBitmap = (HBITMAP) GetClipboardData(CF_BITMAP);
    if (!clipboardBitmap) {
      log_line("ERROR", "GetClipboardData for CF_BITMAP failed (%lu)", (unsigned long) GetLastError());
    } else {
      dup = copy_bitmap_handle(clipboardBitmap);
      if (!dup) {
        log_line("ERROR", "CopyImage failed while duplicating clipboard bitmap (%lu)", (unsigned long) GetLastError());
      }
    }
  }

  if (dup) {
    return dup;
  }

  UINT dibFormats[] = {CF_DIBV5, CF_DIB};
  for (size_t i = 0; i < sizeof(dibFormats) / sizeof(dibFormats[0]); ++i) {
    dup = bitmap_from_dib(dibFormats[i]);
    if (dup) {
      return dup;
    }
  }

  return NULL;
}

static bool emit_clipboard_text_utf8(void) {
  HANDLE handle = GetClipboardData(CF_UNICODETEXT);
  if (!handle) {
    log_line("ERROR", "GetClipboardData for CF_UNICODETEXT failed (%lu)", (unsigned long) GetLastError());
    return false;
  }

  const wchar_t* locked = (const wchar_t*) GlobalLock(handle);
  if (!locked) {
    log_line("ERROR", "GlobalLock failed for text (%lu)", (unsigned long) GetLastError());
    return false;
  }

  size_t length = wcslen(locked);
  size_t bytesWritten = 0;

  if (length > 0) {
    int required = WideCharToMultiByte(CP_UTF8, 0, locked, (int) length, NULL, 0, NULL, NULL);
    if (required <= 0) {
      log_line("ERROR", "WideCharToMultiByte sizing failed (%lu)", (unsigned long) GetLastError());
      GlobalUnlock(handle);
      return false;
    }

    char* buffer = (char*) malloc((size_t) required);
    if (!buffer) {
      log_line("ERROR", "Out of memory while converting clipboard text");
      GlobalUnlock(handle);
      return false;
    }

    int converted = WideCharToMultiByte(CP_UTF8, 0, locked, (int) length, buffer, required, NULL, NULL);
    GlobalUnlock(handle);
    if (converted <= 0) {
      log_line("ERROR", "WideCharToMultiByte failed (%lu)", (unsigned long) GetLastError());
      free(buffer);
      return false;
    }

    size_t written = fwrite(buffer, 1, (size_t) converted, stdout);
    free(buffer);
    if (written != (size_t) converted) {
      log_line("ERROR", "Failed to write text to stdout (%zu/%d bytes)", written, converted);
      return false;
    }
    bytesWritten = written;
  } else {
    GlobalUnlock(handle);
  }

  fflush(stdout);
  log_line("INFO", "Text data written to stdout (%zu bytes)", bytesWritten);
  return true;
}

static bool emit_png_bytes(IWICImagingFactory* factory, IWICBitmap* bitmap) {
  if (!factory || !bitmap) {
    return false;
  }

  IStream* stream = NULL;
  HRESULT hr = CreateStreamOnHGlobal(NULL, TRUE, &stream);
  if (FAILED(hr)) {
    log_line("ERROR", "CreateStreamOnHGlobal failed (0x%08lx)", (unsigned long) hr);
    return false;
  }

  IWICBitmapEncoder* encoder = NULL;
  hr = IWICImagingFactory_CreateEncoder(factory, &GUID_ContainerFormatPng, NULL, &encoder);
  if (FAILED(hr)) {
    log_line("ERROR", "CreateEncoder failed (0x%08lx)", (unsigned long) hr);
    IStream_Release(stream);
    return false;
  }

  hr = IWICBitmapEncoder_Initialize(encoder, stream, WICBitmapEncoderNoCache);
  if (FAILED(hr)) {
    log_line("ERROR", "Encoder initialize failed (0x%08lx)", (unsigned long) hr);
    IWICBitmapEncoder_Release(encoder);
    IStream_Release(stream);
    return false;
  }

  IWICBitmapFrameEncode* frame = NULL;
  IPropertyBag2* props = NULL;
  hr = IWICBitmapEncoder_CreateNewFrame(encoder, &frame, &props);
  if (FAILED(hr)) {
    log_line("ERROR", "CreateNewFrame failed (0x%08lx)", (unsigned long) hr);
    IWICBitmapEncoder_Release(encoder);
    IStream_Release(stream);
    return false;
  }

  hr = IWICBitmapFrameEncode_Initialize(frame, props);
  if (FAILED(hr)) {
    log_line("ERROR", "Frame initialize failed (0x%08lx)", (unsigned long) hr);
    if (props) {
      IPropertyBag2_Release(props);
    }
    IWICBitmapFrameEncode_Release(frame);
    IWICBitmapEncoder_Release(encoder);
    IStream_Release(stream);
    return false;
  }

  UINT width = 0;
  UINT height = 0;
  hr = IWICBitmap_GetSize(bitmap, &width, &height);
  if (FAILED(hr)) {
    log_line("ERROR", "GetSize failed (0x%08lx)", (unsigned long) hr);
    if (props) {
      IPropertyBag2_Release(props);
    }
    IWICBitmapFrameEncode_Release(frame);
    IWICBitmapEncoder_Release(encoder);
    IStream_Release(stream);
    return false;
  }

  hr = IWICBitmapFrameEncode_SetSize(frame, width, height);
  if (FAILED(hr)) {
    log_line("ERROR", "SetSize failed (0x%08lx)", (unsigned long) hr);
    if (props) {
      IPropertyBag2_Release(props);
    }
    IWICBitmapFrameEncode_Release(frame);
    IWICBitmapEncoder_Release(encoder);
    IStream_Release(stream);
    return false;
  }

  WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
  hr = IWICBitmapFrameEncode_SetPixelFormat(frame, &format);
  if (FAILED(hr)) {
    log_line("ERROR", "SetPixelFormat failed (0x%08lx)", (unsigned long) hr);
    if (props) {
      IPropertyBag2_Release(props);
    }
    IWICBitmapFrameEncode_Release(frame);
    IWICBitmapEncoder_Release(encoder);
    IStream_Release(stream);
    return false;
  }

  hr = IWICBitmapFrameEncode_WriteSource(frame, (IWICBitmapSource*) bitmap, NULL);
  if (FAILED(hr)) {
    log_line("ERROR", "WriteSource failed (0x%08lx)", (unsigned long) hr);
    if (props) {
      IPropertyBag2_Release(props);
    }
    IWICBitmapFrameEncode_Release(frame);
    IWICBitmapEncoder_Release(encoder);
    IStream_Release(stream);
    return false;
  }

  hr = IWICBitmapFrameEncode_Commit(frame);
  if (FAILED(hr)) {
    log_line("ERROR", "Frame commit failed (0x%08lx)", (unsigned long) hr);
    if (props) {
      IPropertyBag2_Release(props);
    }
    IWICBitmapFrameEncode_Release(frame);
    IWICBitmapEncoder_Release(encoder);
    IStream_Release(stream);
    return false;
  }

  hr = IWICBitmapEncoder_Commit(encoder);
  if (FAILED(hr)) {
    log_line("ERROR", "Encoder commit failed (0x%08lx)", (unsigned long) hr);
    if (props) {
      IPropertyBag2_Release(props);
    }
    IWICBitmapFrameEncode_Release(frame);
    IWICBitmapEncoder_Release(encoder);
    IStream_Release(stream);
    return false;
  }

  HGLOBAL hGlobal = NULL;
  hr = GetHGlobalFromStream(stream, &hGlobal);
  if (FAILED(hr) || !hGlobal) {
    log_line("ERROR", "GetHGlobalFromStream failed (0x%08lx)", (unsigned long) hr);
    if (props) {
      IPropertyBag2_Release(props);
    }
    IWICBitmapFrameEncode_Release(frame);
    IWICBitmapEncoder_Release(encoder);
    IStream_Release(stream);
    return false;
  }

  SIZE_T dataSize = GlobalSize(hGlobal);
  if (dataSize == 0 || dataSize > MAXDWORD) {
    log_line("ERROR", "PNG buffer has unexpected size %zu", (size_t) dataSize);
    if (props) {
      IPropertyBag2_Release(props);
    }
    IWICBitmapFrameEncode_Release(frame);
    IWICBitmapEncoder_Release(encoder);
    IStream_Release(stream);
    return false;
  }

  void* data = GlobalLock(hGlobal);
  if (!data) {
    log_line("ERROR", "GlobalLock for PNG buffer failed (%lu)", (unsigned long) GetLastError());
    if (props) {
      IPropertyBag2_Release(props);
    }
    IWICBitmapFrameEncode_Release(frame);
    IWICBitmapEncoder_Release(encoder);
    IStream_Release(stream);
    return false;
  }

  size_t written = fwrite(data, 1, (size_t) dataSize, stdout);
  if (written != (size_t) dataSize) {
    log_line("ERROR", "Failed to write PNG to stdout (%zu/%zu bytes)", written, (size_t) dataSize);
    GlobalUnlock(hGlobal);
    if (props) {
      IPropertyBag2_Release(props);
    }
    IWICBitmapFrameEncode_Release(frame);
    IWICBitmapEncoder_Release(encoder);
    IStream_Release(stream);
    return false;
  }

  fflush(stdout);

  GlobalUnlock(hGlobal);

  if (props) {
    IPropertyBag2_Release(props);
  }
  IWICBitmapFrameEncode_Release(frame);
  IWICBitmapEncoder_Release(encoder);
  IStream_Release(stream);
  return true;
}

int wmain(int argc, wchar_t** argv) {
  SetConsoleOutputCP(CP_UTF8);
  g_debug_enabled = load_debug_flag();
  log_line("INFO", "paste starting up");

  output_mode mode = OUTPUT_MODE_AUTO;
  if (!parse_args(argc, argv, &mode)) {
    log_line("INFO", "Usage: paste64.exe [--text|--image|--type auto|text|image]");
    return 1;
  }

  if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
    log_line("ERROR", "Failed to switch stdout to binary mode");
    return 1;
  }

  HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  if (FAILED(hr)) {
    log_line("ERROR", "CoInitializeEx failed (0x%08lx)", (unsigned long) hr);
    return 1;
  }

  bool coInitialized = true;
  bool clipboardOpen = false;
  IWICImagingFactory* factory = NULL;
  IWICBitmap* wicBitmap = NULL;
  HBITMAP clipboardBitmap = NULL;
  int exitCode = 1;

  if (!OpenClipboard(NULL)) {
    log_line("ERROR", "OpenClipboard failed (%lu)", (unsigned long) GetLastError());
    goto cleanup;
  }
  clipboardOpen = true;

  bool textSuccess = false;
  bool textConsidered = mode != OUTPUT_MODE_IMAGE;
  if (textConsidered && IsClipboardFormatAvailable(CF_UNICODETEXT)) {
    textSuccess = emit_clipboard_text_utf8();
  } else if (mode == OUTPUT_MODE_TEXT) {
    log_line("ERROR", "Clipboard does not contain Unicode text");
  }

  if (mode == OUTPUT_MODE_TEXT) {
    exitCode = textSuccess ? 0 : 1;
    goto cleanup;
  }

  if (textSuccess) {
    exitCode = 0;
    goto cleanup;
  }

  clipboardBitmap = acquire_clipboard_bitmap();
  if (!clipboardBitmap) {
    log_line("ERROR", "Clipboard does not contain a compatible bitmap image");
    goto cleanup;
  }

  CloseClipboard();
  clipboardOpen = false;

  hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
                        (void**) &factory);
  if (FAILED(hr)) {
    log_line("ERROR", "CoCreateInstance for WIC factory failed (0x%08lx)", (unsigned long) hr);
    goto cleanup;
  }

  hr = IWICImagingFactory_CreateBitmapFromHBITMAP(factory, clipboardBitmap, NULL, WICBitmapUseAlpha, &wicBitmap);
  DeleteObject(clipboardBitmap);
  clipboardBitmap = NULL;

  if (FAILED(hr)) {
    log_line("ERROR", "CreateBitmapFromHBITMAP failed (0x%08lx)", (unsigned long) hr);
    goto cleanup;
  }

  UINT width = 0;
  UINT height = 0;
  if (SUCCEEDED(IWICBitmap_GetSize(wicBitmap, &width, &height))) {
    log_line("INFO", "Captured %ux%u image from clipboard", (unsigned) width, (unsigned) height);
  }

  if (!emit_png_bytes(factory, wicBitmap)) {
    log_line("ERROR", "Failed to emit PNG data");
    goto cleanup;
  }

  log_line("INFO", "Image data written to stdout as PNG");
  exitCode = 0;

cleanup:
  if (clipboardOpen) {
    CloseClipboard();
  }
  if (clipboardBitmap) {
    DeleteObject(clipboardBitmap);
  }
  if (wicBitmap) {
    IWICBitmap_Release(wicBitmap);
  }
  if (factory) {
    IWICImagingFactory_Release(factory);
  }
  if (coInitialized) {
    CoUninitialize();
  }
  return exitCode;
}
