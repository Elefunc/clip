#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <wchar.h>
#include <wctype.h>

static const wchar_t kWindowClassName[] = L"ClipboardTrimWatcher";
static bool g_isUpdatingClipboard = false;

typedef struct {
    wchar_t *text;
    size_t length;  // number of wchar_t excluding null terminator
} ClipboardBuffer;

typedef struct {
    wchar_t *text;
    size_t length;
    size_t whitespaceRemoved;
    size_t linesTouched;
    size_t lineCount;
} TrimmedBuffer;

static void log_time_prefix(void) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    printf("[%02u:%02u:%02u.%03u] ",
           (unsigned)st.wHour,
           (unsigned)st.wMinute,
           (unsigned)st.wSecond,
           (unsigned)st.wMilliseconds);
}

static void log_info(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_time_prefix();
    vprintf(fmt, args);
    printf("\n");
    fflush(stdout);
    va_end(args);
}

static bool try_open_clipboard(HWND hwnd) {
    const int kRetries = 5;
    for (int i = 0; i < kRetries; ++i) {
        if (OpenClipboard(hwnd)) {
            return true;
        }
        Sleep(10);
    }
    return false;
}

static void free_clipboard_buffer(ClipboardBuffer *buffer) {
    if (buffer && buffer->text) {
        free(buffer->text);
        buffer->text = NULL;
        buffer->length = 0;
    }
}

static bool fetch_clipboard_text(HWND hwnd, ClipboardBuffer *outBuffer, bool *outWasUnicode) {
    if (!outBuffer) {
        return false;
    }
    outBuffer->text = NULL;
    outBuffer->length = 0;
    if (outWasUnicode) {
        *outWasUnicode = false;
    }

    if (!try_open_clipboard(hwnd)) {
        log_info("Unable to open clipboard for reading");
        return false;
    }

    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData) {
        wchar_t *locked = (wchar_t *)GlobalLock(hData);
        if (!locked) {
            CloseClipboard();
            log_info("Failed to lock Unicode clipboard data");
            return false;
        }
        size_t len = wcslen(locked);
        wchar_t *copy = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
        if (!copy) {
            GlobalUnlock(hData);
            CloseClipboard();
            log_info("Out of memory while copying clipboard data");
            return false;
        }
        memcpy(copy, locked, (len + 1) * sizeof(wchar_t));
        outBuffer->text = copy;
        outBuffer->length = len;
        if (outWasUnicode) {
            *outWasUnicode = true;
        }
        GlobalUnlock(hData);
        CloseClipboard();
        return true;
    }

    HANDLE hAnsi = GetClipboardData(CF_TEXT);
    if (!hAnsi) {
        CloseClipboard();
        return false;
    }
    char *lockedAnsi = (char *)GlobalLock(hAnsi);
    if (!lockedAnsi) {
        CloseClipboard();
        log_info("Failed to lock ANSI clipboard data");
        return false;
    }
    int required = MultiByteToWideChar(CP_ACP, 0, lockedAnsi, -1, NULL, 0);
    if (required <= 0) {
        GlobalUnlock(hAnsi);
        CloseClipboard();
        log_info("Failed to convert ANSI clipboard data to Unicode");
        return false;
    }
    wchar_t *copy = (wchar_t *)malloc((size_t)required * sizeof(wchar_t));
    if (!copy) {
        GlobalUnlock(hAnsi);
        CloseClipboard();
        log_info("Out of memory while converting clipboard data");
        return false;
    }
    MultiByteToWideChar(CP_ACP, 0, lockedAnsi, -1, copy, required);
    outBuffer->text = copy;
    outBuffer->length = wcslen(copy);
    GlobalUnlock(hAnsi);
    CloseClipboard();
    return true;
}

static TrimmedBuffer trim_lines_preserving_breaks(const wchar_t *input, size_t length) {
    TrimmedBuffer result = {0};
    if (!input) {
        return result;
    }

    wchar_t *output = (wchar_t *)malloc((length + 1) * sizeof(wchar_t));
    if (!output) {
        return result;
    }

    size_t readPos = 0;
    size_t writePos = 0;
    size_t whitespaceRemoved = 0;
    size_t linesTouched = 0;
    size_t lineCount = 0;

    while (readPos < length) {
        size_t lineStart = readPos;
        size_t lineEnd = readPos;

        while (lineEnd < length && input[lineEnd] != L'\r' && input[lineEnd] != L'\n') {
            lineEnd++;
        }

        size_t trimStart = lineStart;
        while (trimStart < lineEnd && iswspace(input[trimStart]) && input[trimStart] != L'\r' && input[trimStart] != L'\n') {
            trimStart++;
        }

        size_t trimEnd = lineEnd;
        while (trimEnd > trimStart && iswspace(input[trimEnd - 1]) && input[trimEnd - 1] != L'\r' && input[trimEnd - 1] != L'\n') {
            trimEnd--;
        }

        if (lineEnd > lineStart) {
            lineCount++;
        } else if (lineEnd == lineStart && (lineEnd < length)) {
            lineCount++;
        }

        size_t removed = (trimStart - lineStart) + (lineEnd - trimEnd);
        if (removed > 0) {
            linesTouched++;
            whitespaceRemoved += removed;
        }

        for (size_t i = trimStart; i < trimEnd; ++i) {
            output[writePos++] = input[i];
        }

        size_t newlinePos = lineEnd;
        while (newlinePos < length) {
            wchar_t ch = input[newlinePos];
            if (ch == L'\r') {
                output[writePos++] = ch;
                newlinePos++;
                if (newlinePos < length && input[newlinePos] == L'\n') {
                    output[writePos++] = input[newlinePos++];
                }
            } else if (ch == L'\n') {
                output[writePos++] = ch;
                newlinePos++;
            } else {
                break;
            }
        }

        readPos = newlinePos;
    }

    if (length == 0) {
        lineCount = 0;
    }

    output[writePos] = L'\0';

    result.text = output;
    result.length = writePos;
    result.whitespaceRemoved = whitespaceRemoved;
    result.linesTouched = linesTouched;
    result.lineCount = lineCount;
    return result;
}

static bool set_clipboard_text(HWND hwnd, const wchar_t *text, size_t length) {
    if (!text) {
        return false;
    }

    if (!try_open_clipboard(hwnd)) {
        log_info("Unable to open clipboard for writing");
        return false;
    }

    if (!EmptyClipboard()) {
        CloseClipboard();
        log_info("Failed to empty clipboard before writing");
        return false;
    }

    size_t bytes = (length + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hMem) {
        CloseClipboard();
        log_info("Failed to allocate global memory for clipboard");
        return false;
    }

    void *dest = GlobalLock(hMem);
    if (!dest) {
        GlobalFree(hMem);
        CloseClipboard();
        log_info("Failed to lock global memory for clipboard");
        return false;
    }
    memcpy(dest, text, bytes);
    GlobalUnlock(hMem);

    if (!SetClipboardData(CF_UNICODETEXT, hMem)) {
        GlobalFree(hMem);
        CloseClipboard();
        log_info("SetClipboardData failed");
        return false;
    }

    CloseClipboard();
    return true;
}

static void handle_clipboard_update(HWND hwnd) {
    if (g_isUpdatingClipboard) {
        return;
    }

    ClipboardBuffer original = {0};
    bool wasUnicode = false;
    if (!fetch_clipboard_text(hwnd, &original, &wasUnicode)) {
        log_info("Clipboard update contained no compatible text");
        return;
    }

    TrimmedBuffer trimmed = trim_lines_preserving_breaks(original.text, original.length);
    if (!trimmed.text) {
        log_info("Failed to allocate memory while trimming clipboard text");
        free_clipboard_buffer(&original);
        return;
    }

    bool changed = false;
    if (trimmed.length != original.length) {
        changed = true;
    } else if (wcsncmp(trimmed.text, original.text, trimmed.length) != 0) {
        changed = true;
    }

    if (!changed) {
        log_info("Clipboard text already trimmed (%zu line%s)",
                 trimmed.lineCount,
                 trimmed.lineCount == 1 ? "" : "s");
        free(trimmed.text);
        free_clipboard_buffer(&original);
        return;
    }

    g_isUpdatingClipboard = true;
    if (set_clipboard_text(hwnd, trimmed.text, trimmed.length)) {
        log_info("Trimmed clipboard text: removed %zu whitespace char%s across %zu line%s", 
                 trimmed.whitespaceRemoved,
                 trimmed.whitespaceRemoved == 1 ? "" : "s",
                 trimmed.linesTouched,
                 trimmed.linesTouched == 1 ? "" : "s");
    } else {
        log_info("Failed to set trimmed text back onto clipboard");
    }
    g_isUpdatingClipboard = false;

    free(trimmed.text);
    free_clipboard_buffer(&original);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            if (!AddClipboardFormatListener(hwnd)) {
                log_info("AddClipboardFormatListener failed");
                return -1;
            }
            log_info("Clipboard listener registered");
            return 0;
        case WM_CLIPBOARDUPDATE:
            handle_clipboard_update(hwnd);
            return 0;
        case WM_DESTROY:
            RemoveClipboardFormatListener(hwnd);
            PostQuitMessage(0);
            log_info("Shutting down");
            return 0;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

int wmain(void) {
    SetConsoleOutputCP(CP_UTF8);
    log_info("Starting clipboard whitespace trimmer");

    HINSTANCE hInstance = GetModuleHandle(NULL);

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = window_proc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kWindowClassName;

    if (!RegisterClassExW(&wc)) {
        log_info("RegisterClassEx failed (%lu)", GetLastError());
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kWindowClassName,
        L"Clipboard Whitespace Trimmer",
        WS_POPUP,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        NULL,
        NULL,
        hInstance,
        NULL);

    if (!hwnd) {
        log_info("CreateWindowEx failed (%lu)", GetLastError());
        return 1;
    }

    log_info("Monitoring clipboard. Press Ctrl+C in this console to exit.");

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
