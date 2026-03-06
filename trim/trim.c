#ifndef UNICODE
#define UNICODE
#endif

#define PCRE2_STATIC
#define PCRE2_CODE_UNIT_WIDTH 16

#include "pcre2.h"

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <wchar.h>
#include <wctype.h>

#include <mmsystem.h>

#include "trim.h"

#define WM_APP_EXIT (WM_APP + 1)
#define SINGLE_INSTANCE_MUTEX_NAME L"Local\\ClipTrimSingleton"

static const wchar_t kWindowClassName[] = L"ClipboardTrimWatcher";
static const wchar_t kRulesFileName[] = L"trim.rules";
static const char kDefaultRulesFileContents[] =
    "# Copy this file to `trim.rules` in the launch directory to override the\n"
    "# executable-side default. If neither location has a config, ClipTrim generates\n"
    "# this default rule beside `trim.exe` on first launch.\n"
    "# Syntax:\n"
    "# - Start each replacement with `rule`.\n"
    "# - Add one or more `pattern <<TOKEN` blocks; each block body is a PCRE2 regexp.\n"
    "# - Add exactly one `replace <<TOKEN` block; its body is the literal replacement.\n"
    "# - A block ends when a line exactly matches `TOKEN`.\n"
    "# - Block bodies do not include the terminator line break.\n"
    "# - Add a blank line before `TOKEN` if you need the replacement to end with a newline.\n"
    "# Rules run in file order. Patterns inside one rule share the same replacement.\n"
    "\n"
    "# Default rule: strip a leading quote marker from the full clipboard string.\n"
    "rule\n"
    "pattern <<EOF\n"
    "\\A›\n"
    "EOF\n"
    "pattern <<EOF\n"
    "\\A»\n"
    "EOF\n"
    "replace <<EOF\n"
    "EOF\n"
    "\n"
    "# Default rule: trim the same trailing whitespace set the pre-regex trimmer used.\n"
    "rule\n"
    "pattern <<EOF\n"
    "[ \\t\\f\\x0B\\x{00A0}\\x{1680}\\x{180E}\\x{2000}-\\x{200A}\\x{2028}\\x{2029}\\x{202F}\\x{205F}\\x{3000}]+(?=\\r\\n?|\\n|\\z)\n"
    "EOF\n"
    "replace <<EOF\n"
    "EOF\n";
static bool g_isUpdatingClipboard = false;
static HANDLE g_singleInstanceMutex = NULL;
static wchar_t* g_executableDirectory = NULL;

typedef struct {
  wchar_t* text;
  size_t length; // number of wchar_t excluding null terminator
} ClipboardBuffer;

typedef struct {
  pcre2_code* code;
  wchar_t* source;
  size_t sourceLength;
  size_t lineNumber;
} RegexPattern;

typedef struct {
  RegexPattern* patterns;
  size_t patternCount;
  wchar_t* replacement;
  size_t replacementLength;
  size_t replaceLineNumber;
} RegexRule;

typedef struct {
  RegexRule* rules;
  size_t ruleCount;
} RuleSet;

typedef struct {
  size_t lineNumber;
  char message[256];
} RuleLoadError;

typedef struct {
  size_t substitutionsApplied;
  size_t patternsTouched;
  size_t rulesTouched;
} ReplacementStats;

typedef struct {
  wchar_t* text;
  size_t length;
  size_t lineCount;
  ReplacementStats replacementStats;
} NormalizedBuffer;

typedef struct {
  wchar_t* activePath;
  FILETIME activeWriteTime;
  RuleSet activeRules;
  bool hasActiveFile;

  wchar_t* lastResolvedPath;
  FILETIME lastResolvedWriteTime;
  bool lastResolvedExists;
  bool lastLoadFailed;
} RuleConfigState;

static RuleConfigState g_ruleConfig = {0};

_Static_assert(sizeof(wchar_t) == 2, "ClipTrim requires 16-bit wchar_t");

static void log_time_prefix(void) {
  SYSTEMTIME st;
  GetLocalTime(&st);
  printf("[%02u:%02u:%02u.%03u] ", (unsigned) st.wHour, (unsigned) st.wMinute, (unsigned) st.wSecond,
         (unsigned) st.wMilliseconds);
}

static void log_info(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  log_time_prefix();
  vprintf(fmt, args);
  printf("\n");
  fflush(stdout);
  va_end(args);
}

static char* utf8_from_wide_length(const wchar_t* text, size_t length) {
  if (!text) {
    return NULL;
  }
  if (length > (size_t) INT_MAX) {
    return NULL;
  }

  int utf8Bytes = WideCharToMultiByte(CP_UTF8, 0, text, (int) length, NULL, 0, NULL, NULL);
  if (utf8Bytes <= 0) {
    return NULL;
  }

  char* utf8 = (char*) malloc((size_t) utf8Bytes + 1);
  if (!utf8) {
    return NULL;
  }

  if (WideCharToMultiByte(CP_UTF8, 0, text, (int) length, utf8, utf8Bytes, NULL, NULL) <= 0) {
    free(utf8);
    return NULL;
  }

  utf8[utf8Bytes] = '\0';
  return utf8;
}

static char* utf8_from_wide(const wchar_t* text) {
  if (!text) {
    return NULL;
  }
  return utf8_from_wide_length(text, wcslen(text));
}

static void log_wide_value(const char* label, const wchar_t* value) {
  char* utf8 = utf8_from_wide(value);
  if (!utf8) {
    log_info("Failed to convert %s to UTF-8", label);
    return;
  }

  log_info("%s: %s", label, utf8);
  free(utf8);
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

static wchar_t* duplicate_wide_string(const wchar_t* text) {
  if (!text) {
    return NULL;
  }
  return duplicate_wide_range(text, wcslen(text));
}

static bool filetime_equal(FILETIME lhs, FILETIME rhs) {
  return lhs.dwLowDateTime == rhs.dwLowDateTime && lhs.dwHighDateTime == rhs.dwHighDateTime;
}

static bool paths_equal_ignore_case(const wchar_t* lhs, const wchar_t* rhs) {
  if (!lhs || !rhs) {
    return lhs == rhs;
  }
  return _wcsicmp(lhs, rhs) == 0;
}

static wchar_t* get_current_directory_string(void) {
  DWORD required = GetCurrentDirectoryW(0, NULL);
  if (required == 0) {
    return NULL;
  }

  wchar_t* cwd = (wchar_t*) malloc((size_t) required * sizeof(wchar_t));
  if (!cwd) {
    return NULL;
  }

  DWORD length = GetCurrentDirectoryW(required, cwd);
  if (length == 0 || length >= required) {
    free(cwd);
    return NULL;
  }

  return cwd;
}

static bool initialize_executable_directory(void) {
  wchar_t path[32768];
  DWORD length = GetModuleFileNameW(NULL, path, (DWORD) (sizeof(path) / sizeof(path[0])));
  if (length == 0 || length >= (DWORD) (sizeof(path) / sizeof(path[0]) - 1)) {
    return false;
  }

  size_t separator = (size_t) length;
  while (separator > 0 && path[separator - 1] != L'\\' && path[separator - 1] != L'/') {
    separator--;
  }
  if (separator == 0) {
    return false;
  }

  size_t directoryLength = separator - 1;
  if (separator == 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) {
    directoryLength = separator;
  }

  wchar_t* directory = duplicate_wide_range(path, directoryLength);
  if (!directory) {
    return false;
  }

  free(g_executableDirectory);
  g_executableDirectory = directory;
  return true;
}

static wchar_t* join_path(const wchar_t* directory, const wchar_t* leaf) {
  if (!directory || !leaf) {
    return NULL;
  }

  size_t directoryLength = wcslen(directory);
  size_t leafLength = wcslen(leaf);
  bool needsSeparator = directoryLength > 0 && directory[directoryLength - 1] != L'\\' && directory[directoryLength - 1] != L'/';

  size_t totalLength = directoryLength + (needsSeparator ? 1u : 0u) + leafLength;
  wchar_t* path = (wchar_t*) malloc((totalLength + 1) * sizeof(wchar_t));
  if (!path) {
    return NULL;
  }

  memcpy(path, directory, directoryLength * sizeof(wchar_t));
  if (needsSeparator) {
    path[directoryLength++] = L'\\';
  }
  memcpy(path + directoryLength, leaf, leafLength * sizeof(wchar_t));
  path[totalLength] = L'\0';
  return path;
}

static bool get_file_last_write_time(const wchar_t* path, FILETIME* outWriteTime) {
  if (!path || !outWriteTime) {
    return false;
  }

  WIN32_FILE_ATTRIBUTE_DATA attributes;
  if (!GetFileAttributesExW(path, GetFileExInfoStandard, &attributes)) {
    return false;
  }

  *outWriteTime = attributes.ftLastWriteTime;
  return true;
}

static size_t count_clipboard_lines(const wchar_t* text, size_t length) {
  if (!text || length == 0) {
    return 0;
  }

  size_t lineCount = 0;
  size_t position = 0;
  while (position < length) {
    size_t lineStart = position;
    while (position < length && text[position] != L'\r' && text[position] != L'\n') {
      position++;
    }

    if (position > lineStart || position < length) {
      lineCount++;
    }

    if (position < length && text[position] == L'\r') {
      position++;
      if (position < length && text[position] == L'\n') {
        position++;
      }
    } else if (position < length && text[position] == L'\n') {
      position++;
    }
  }

  return lineCount;
}

static void log_current_working_directory(void) {
  wchar_t* cwd = get_current_directory_string();
  if (!cwd) {
    log_info("GetCurrentDirectory failed (%lu)", GetLastError());
    return;
  }

  log_wide_value("Current working directory", cwd);
  free(cwd);
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

static void free_clipboard_buffer(ClipboardBuffer* buffer) {
  if (buffer && buffer->text) {
    free(buffer->text);
    buffer->text = NULL;
    buffer->length = 0;
  }
}

static bool fetch_clipboard_text(HWND hwnd, ClipboardBuffer* outBuffer, bool* outWasUnicode) {
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
    wchar_t* locked = (wchar_t*) GlobalLock(hData);
    if (!locked) {
      CloseClipboard();
      log_info("Failed to lock Unicode clipboard data");
      return false;
    }
    size_t len = wcslen(locked);
    wchar_t* copy = (wchar_t*) malloc((len + 1) * sizeof(wchar_t));
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
  char* lockedAnsi = (char*) GlobalLock(hAnsi);
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
  wchar_t* copy = (wchar_t*) malloc((size_t) required * sizeof(wchar_t));
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

static void set_rule_load_error(RuleLoadError* error, size_t lineNumber, const char* fmt, ...) {
  if (!error) {
    return;
  }

  va_list args;
  va_start(args, fmt);
  vsnprintf(error->message, sizeof(error->message), fmt, args);
  va_end(args);
  error->lineNumber = lineNumber;
}

static void free_regex_rule(RegexRule* rule) {
  if (!rule) {
    return;
  }

  for (size_t i = 0; i < rule->patternCount; ++i) {
    pcre2_code_free(rule->patterns[i].code);
    free(rule->patterns[i].source);
  }

  free(rule->patterns);
  free(rule->replacement);
  memset(rule, 0, sizeof(*rule));
}

static void free_rule_set(RuleSet* ruleSet) {
  if (!ruleSet) {
    return;
  }

  for (size_t i = 0; i < ruleSet->ruleCount; ++i) {
    free_regex_rule(&ruleSet->rules[i]);
  }

  free(ruleSet->rules);
  ruleSet->rules = NULL;
  ruleSet->ruleCount = 0;
}

static void clear_active_rule_config(void) {
  free(g_ruleConfig.activePath);
  g_ruleConfig.activePath = NULL;
  memset(&g_ruleConfig.activeWriteTime, 0, sizeof(g_ruleConfig.activeWriteTime));
  free_rule_set(&g_ruleConfig.activeRules);
  g_ruleConfig.hasActiveFile = false;
}

static void free_rule_config_state(void) {
  clear_active_rule_config();
  free(g_ruleConfig.lastResolvedPath);
  g_ruleConfig.lastResolvedPath = NULL;
  memset(&g_ruleConfig.lastResolvedWriteTime, 0, sizeof(g_ruleConfig.lastResolvedWriteTime));
  g_ruleConfig.lastResolvedExists = false;
  g_ruleConfig.lastLoadFailed = false;
}

static bool parse_block_header(const wchar_t* line, size_t length, const wchar_t* keyword, wchar_t** outToken) {
  if (!line || !keyword || !outToken) {
    return false;
  }

  size_t keywordLength = wcslen(keyword);
  if (length <= keywordLength || wcsncmp(line, keyword, keywordLength) != 0 || !iswspace(line[keywordLength])) {
    return false;
  }

  size_t position = keywordLength;
  while (position < length && iswspace(line[position])) {
    position++;
  }
  if (position + 1 >= length || line[position] != L'<' || line[position + 1] != L'<') {
    return false;
  }

  position += 2;
  while (position < length && iswspace(line[position])) {
    position++;
  }

  size_t tokenStart = position;
  size_t tokenEnd = length;
  while (tokenEnd > tokenStart && iswspace(line[tokenEnd - 1])) {
    tokenEnd--;
  }
  if (tokenEnd == tokenStart) {
    return false;
  }

  for (size_t i = tokenStart; i < tokenEnd; ++i) {
    if (iswspace(line[i])) {
      return false;
    }
  }

  *outToken = duplicate_wide_range(line + tokenStart, tokenEnd - tokenStart);
  return *outToken != NULL;
}

static bool append_pattern_to_rule(RegexRule* rule, wchar_t* source, size_t sourceLength, size_t lineNumber,
                                   RuleLoadError* error) {
  RegexPattern* grown = (RegexPattern*) realloc(rule->patterns, (rule->patternCount + 1) * sizeof(RegexPattern));
  if (!grown) {
    free(source);
    set_rule_load_error(error, lineNumber, "Out of memory while storing regex patterns");
    return false;
  }

  rule->patterns = grown;
  rule->patterns[rule->patternCount].code = NULL;
  rule->patterns[rule->patternCount].source = source;
  rule->patterns[rule->patternCount].sourceLength = sourceLength;
  rule->patterns[rule->patternCount].lineNumber = lineNumber;
  rule->patternCount++;
  return true;
}

static bool append_rule_to_set(RuleSet* ruleSet, RegexRule* rule, size_t ruleLineNumber, RuleLoadError* error) {
  if (rule->patternCount == 0) {
    set_rule_load_error(error, ruleLineNumber, "Rule must contain at least one pattern block");
    return false;
  }
  if (!rule->replacement) {
    set_rule_load_error(error, ruleLineNumber, "Rule must contain exactly one replace block");
    return false;
  }

  RegexRule* grown = (RegexRule*) realloc(ruleSet->rules, (ruleSet->ruleCount + 1) * sizeof(RegexRule));
  if (!grown) {
    set_rule_load_error(error, ruleLineNumber, "Out of memory while storing parsed rules");
    return false;
  }

  ruleSet->rules = grown;
  ruleSet->rules[ruleSet->ruleCount++] = *rule;
  memset(rule, 0, sizeof(*rule));
  return true;
}

static bool read_utf8_file(const wchar_t* path, ClipboardBuffer* outBuffer, RuleLoadError* error) {
  if (!path || !outBuffer) {
    return false;
  }

  outBuffer->text = NULL;
  outBuffer->length = 0;

  HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (file == INVALID_HANDLE_VALUE) {
    set_rule_load_error(error, 1, "Unable to open config file (%lu)", GetLastError());
    return false;
  }

  LARGE_INTEGER size = {0};
  if (!GetFileSizeEx(file, &size)) {
    DWORD lastError = GetLastError();
    CloseHandle(file);
    set_rule_load_error(error, 1, "Unable to read config file size (%lu)", lastError);
    return false;
  }
  if (size.QuadPart < 0 || size.QuadPart > INT_MAX) {
    CloseHandle(file);
    set_rule_load_error(error, 1, "Config file is too large");
    return false;
  }

  size_t byteLength = (size_t) size.QuadPart;
  char* bytes = (char*) malloc(byteLength + 1);
  if (!bytes) {
    CloseHandle(file);
    set_rule_load_error(error, 1, "Out of memory while reading config file");
    return false;
  }

  size_t bytesReadTotal = 0;
  while (bytesReadTotal < byteLength) {
    DWORD chunkRead = 0;
    DWORD chunkSize = (DWORD) (byteLength - bytesReadTotal);
    if (!ReadFile(file, bytes + bytesReadTotal, chunkSize, &chunkRead, NULL)) {
      DWORD lastError = GetLastError();
      free(bytes);
      CloseHandle(file);
      set_rule_load_error(error, 1, "Unable to read config file (%lu)", lastError);
      return false;
    }
    if (chunkRead == 0) {
      break;
    }
    bytesReadTotal += (size_t) chunkRead;
  }

  CloseHandle(file);
  bytes[bytesReadTotal] = '\0';

  size_t offset = 0;
  if (bytesReadTotal >= 3 && (unsigned char) bytes[0] == 0xEF && (unsigned char) bytes[1] == 0xBB &&
      (unsigned char) bytes[2] == 0xBF) {
    offset = 3;
  }

  size_t utf8Length = bytesReadTotal - offset;
  if (utf8Length == 0) {
    outBuffer->text = duplicate_wide_range(L"", 0);
    free(bytes);
    if (!outBuffer->text) {
      set_rule_load_error(error, 1, "Out of memory while reading config file");
      return false;
    }
    return true;
  }

  int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes + offset, (int) utf8Length, NULL, 0);
  if (required <= 0) {
    free(bytes);
    set_rule_load_error(error, 1, "Config file must be valid UTF-8");
    return false;
  }

  wchar_t* text = (wchar_t*) malloc(((size_t) required + 1) * sizeof(wchar_t));
  if (!text) {
    free(bytes);
    set_rule_load_error(error, 1, "Out of memory while decoding config file");
    return false;
  }

  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes + offset, (int) utf8Length, text, required) <= 0) {
    free(text);
    free(bytes);
    set_rule_load_error(error, 1, "Config file must be valid UTF-8");
    return false;
  }

  free(bytes);
  text[required] = L'\0';
  outBuffer->text = text;
  outBuffer->length = (size_t) required;
  return true;
}

static bool parse_rule_set_text(const wchar_t* text, size_t length, RuleSet* outRuleSet, RuleLoadError* error) {
  RuleSet parsed = {0};
  RegexRule currentRule = {0};
  bool hasOpenRule = false;
  size_t currentRuleLineNumber = 0;
  wchar_t* blockToken = NULL;
  size_t blockBodyStart = 0;
  size_t blockLineNumber = 0;
  enum { BLOCK_NONE, BLOCK_PATTERN, BLOCK_REPLACE } blockType = BLOCK_NONE;

  size_t position = 0;
  size_t lineNumber = 1;

  while (position < length) {
    size_t lineStart = position;
    while (position < length && text[position] != L'\r' && text[position] != L'\n') {
      position++;
    }
    size_t lineEnd = position;
    size_t nextLineStart = position;
    if (nextLineStart < length && text[nextLineStart] == L'\r') {
      nextLineStart++;
    }
    if (nextLineStart < length && text[nextLineStart] == L'\n') {
      nextLineStart++;
    }

    if (blockType != BLOCK_NONE) {
      size_t rawLength = lineEnd - lineStart;
      if (rawLength == wcslen(blockToken) && wmemcmp(text + lineStart, blockToken, rawLength) == 0) {
        size_t bodyLength = lineStart - blockBodyStart;
        if (bodyLength > 0) {
          if (text[lineStart - 1] == L'\n') {
            bodyLength--;
            if (bodyLength > 0 && text[blockBodyStart + bodyLength - 1] == L'\r') {
              bodyLength--;
            }
          } else if (text[lineStart - 1] == L'\r') {
            bodyLength--;
          }
        }
        wchar_t* body = duplicate_wide_range(text + blockBodyStart, bodyLength);
        if (!body) {
          set_rule_load_error(error, blockLineNumber, "Out of memory while reading rule block");
          goto fail;
        }

        if (blockType == BLOCK_PATTERN) {
          if (!append_pattern_to_rule(&currentRule, body, bodyLength, blockLineNumber, error)) {
            goto fail;
          }
        } else {
          currentRule.replacement = body;
          currentRule.replacementLength = bodyLength;
          currentRule.replaceLineNumber = blockLineNumber;
        }

        free(blockToken);
        blockToken = NULL;
        blockType = BLOCK_NONE;
      }

      lineNumber++;
      position = nextLineStart;
      continue;
    }

    size_t trimmedStart = lineStart;
    size_t trimmedEnd = lineEnd;
    while (trimmedStart < trimmedEnd && iswspace(text[trimmedStart])) {
      trimmedStart++;
    }
    while (trimmedEnd > trimmedStart && iswspace(text[trimmedEnd - 1])) {
      trimmedEnd--;
    }

    if (trimmedStart == trimmedEnd || text[trimmedStart] == L'#') {
      lineNumber++;
      position = nextLineStart;
      continue;
    }

    const wchar_t* trimmed = text + trimmedStart;
    size_t trimmedLength = trimmedEnd - trimmedStart;

    if (trimmedLength == 4 && wmemcmp(trimmed, L"rule", 4) == 0) {
      if (hasOpenRule && !append_rule_to_set(&parsed, &currentRule, currentRuleLineNumber, error)) {
        goto fail;
      }
      hasOpenRule = true;
      currentRuleLineNumber = lineNumber;
    } else {
      wchar_t* token = NULL;
      if (parse_block_header(trimmed, trimmedLength, L"pattern", &token)) {
        if (!hasOpenRule) {
          free(token);
          set_rule_load_error(error, lineNumber, "Pattern block must appear inside a rule");
          goto fail;
        }
        blockToken = token;
        blockType = BLOCK_PATTERN;
        blockBodyStart = nextLineStart;
        blockLineNumber = lineNumber;
      } else if (parse_block_header(trimmed, trimmedLength, L"replace", &token)) {
        if (!hasOpenRule) {
          free(token);
          set_rule_load_error(error, lineNumber, "Replace block must appear inside a rule");
          goto fail;
        }
        if (currentRule.replacement) {
          free(token);
          set_rule_load_error(error, lineNumber, "Rule may contain only one replace block");
          goto fail;
        }
        blockToken = token;
        blockType = BLOCK_REPLACE;
        blockBodyStart = nextLineStart;
        blockLineNumber = lineNumber;
      } else {
        set_rule_load_error(error, lineNumber, "Unrecognized directive");
        goto fail;
      }
    }

    lineNumber++;
    position = nextLineStart;
  }

  if (blockType != BLOCK_NONE) {
    set_rule_load_error(error, blockLineNumber, "Unterminated block");
    goto fail;
  }

  if (hasOpenRule && !append_rule_to_set(&parsed, &currentRule, currentRuleLineNumber, error)) {
    goto fail;
  }

  *outRuleSet = parsed;
  return true;

fail:
  free(blockToken);
  free_regex_rule(&currentRule);
  free_rule_set(&parsed);
  return false;
}

static bool compile_rule_set(RuleSet* ruleSet, RuleLoadError* error) {
  pcre2_compile_context* context = pcre2_compile_context_create(NULL);
  if (!context) {
    set_rule_load_error(error, 1, "Out of memory while creating regex compile context");
    return false;
  }

  if (pcre2_set_newline(context, PCRE2_NEWLINE_ANY) != 0) {
    pcre2_compile_context_free(context);
    set_rule_load_error(error, 1, "Unable to configure regex newline mode");
    return false;
  }

  for (size_t ruleIndex = 0; ruleIndex < ruleSet->ruleCount; ++ruleIndex) {
    RegexRule* rule = &ruleSet->rules[ruleIndex];
    for (size_t patternIndex = 0; patternIndex < rule->patternCount; ++patternIndex) {
      RegexPattern* pattern = &rule->patterns[patternIndex];
      int compileError = 0;
      PCRE2_SIZE errorOffset = 0;

      pattern->code = pcre2_compile((PCRE2_SPTR) pattern->source, pattern->sourceLength, PCRE2_UTF | PCRE2_UCP,
                                    &compileError, &errorOffset, context);
      if (!pattern->code) {
        PCRE2_UCHAR messageBuffer[256];
        int messageLength = pcre2_get_error_message(compileError, messageBuffer,
                                                    sizeof(messageBuffer) / sizeof(messageBuffer[0]));
        char* utf8Message = NULL;
        if (messageLength > 0) {
          utf8Message = utf8_from_wide_length((const wchar_t*) messageBuffer, (size_t) messageLength);
        }

        set_rule_load_error(error, pattern->lineNumber, "Pattern compile error at offset %zu: %s",
                            (size_t) errorOffset, utf8Message ? utf8Message : "Unknown regex error");
        free(utf8Message);
        pcre2_compile_context_free(context);
        return false;
      }
    }
  }

  pcre2_compile_context_free(context);
  return true;
}

static bool load_rule_set_from_file(const wchar_t* path, RuleSet* outRuleSet, RuleLoadError* error) {
  ClipboardBuffer fileContents = {0};
  RuleSet parsed = {0};

  if (!read_utf8_file(path, &fileContents, error)) {
    return false;
  }
  if (!parse_rule_set_text(fileContents.text, fileContents.length, &parsed, error)) {
    free_clipboard_buffer(&fileContents);
    return false;
  }
  if (!compile_rule_set(&parsed, error)) {
    free_clipboard_buffer(&fileContents);
    free_rule_set(&parsed);
    return false;
  }

  free_clipboard_buffer(&fileContents);
  *outRuleSet = parsed;
  return true;
}

static bool generate_default_rules_file(void) {
  if (!g_executableDirectory) {
    log_info("Unable to generate default replacement config: executable directory unavailable");
    return false;
  }

  wchar_t* path = join_path(g_executableDirectory, kRulesFileName);
  if (!path) {
    log_info("Out of memory while preparing default replacement config path");
    return false;
  }

  HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
  if (file == INVALID_HANDLE_VALUE) {
    DWORD lastError = GetLastError();
    if (lastError == ERROR_FILE_EXISTS || lastError == ERROR_ALREADY_EXISTS) {
      free(path);
      return true;
    }

    char* utf8Path = utf8_from_wide(path);
    if (utf8Path) {
      log_info("Failed to generate default replacement config %s (%lu)", utf8Path, lastError);
      free(utf8Path);
    } else {
      log_info("Failed to generate default replacement config (%lu)", lastError);
    }
    free(path);
    return false;
  }

  DWORD bytesWritten = 0;
  DWORD bytesToWrite = (DWORD) strlen(kDefaultRulesFileContents);
  bool writeOk = WriteFile(file, kDefaultRulesFileContents, bytesToWrite, &bytesWritten, NULL) != 0 &&
                 bytesWritten == bytesToWrite;
  DWORD writeError = writeOk ? ERROR_SUCCESS : GetLastError();
  CloseHandle(file);

  if (!writeOk) {
    DeleteFileW(path);

    char* utf8Path = utf8_from_wide(path);
    if (utf8Path) {
      log_info("Failed to write default replacement config %s (%lu)", utf8Path, writeError);
      free(utf8Path);
    } else {
      log_info("Failed to write default replacement config (%lu)", writeError);
    }
    free(path);
    return false;
  }

  char* utf8Path = utf8_from_wide(path);
  if (utf8Path) {
    log_info("Generated default replacement config %s", utf8Path);
    free(utf8Path);
  } else {
    log_info("Generated default replacement config");
  }

  free(path);
  return true;
}

static bool resolve_rules_config_path(wchar_t** outPath, FILETIME* outWriteTime) {
  wchar_t* currentDirectory = get_current_directory_string();
  if (currentDirectory) {
    wchar_t* currentPath = join_path(currentDirectory, kRulesFileName);
    free(currentDirectory);
    if (currentPath) {
      if (get_file_last_write_time(currentPath, outWriteTime)) {
        *outPath = currentPath;
        return true;
      }
      free(currentPath);
    }
  }

  if (!g_executableDirectory) {
    return false;
  }

  wchar_t* executablePath = join_path(g_executableDirectory, kRulesFileName);
  if (!executablePath) {
    return false;
  }
  if (!get_file_last_write_time(executablePath, outWriteTime)) {
    free(executablePath);
    return false;
  }

  *outPath = executablePath;
  return true;
}

static void refresh_replacement_config(void) {
  wchar_t* resolvedPath = NULL;
  FILETIME resolvedTime = {0};

  if (!resolve_rules_config_path(&resolvedPath, &resolvedTime)) {
    bool generatedDefaultRules = generate_default_rules_file();
    if (generatedDefaultRules && resolve_rules_config_path(&resolvedPath, &resolvedTime)) {
      // Continue below and load the generated executable-side config.
    } else {
      bool shouldLog = g_ruleConfig.lastResolvedExists || g_ruleConfig.hasActiveFile || g_ruleConfig.lastLoadFailed;

      clear_active_rule_config();
      free(g_ruleConfig.lastResolvedPath);
      g_ruleConfig.lastResolvedPath = NULL;
      memset(&g_ruleConfig.lastResolvedWriteTime, 0, sizeof(g_ruleConfig.lastResolvedWriteTime));
      g_ruleConfig.lastResolvedExists = false;
      g_ruleConfig.lastLoadFailed = false;

      if (shouldLog || !generatedDefaultRules) {
        log_info("No replacement config found; normalization rules are inactive");
      }
      return;
    }
  }

  if (g_ruleConfig.lastResolvedExists && paths_equal_ignore_case(resolvedPath, g_ruleConfig.lastResolvedPath) &&
      filetime_equal(resolvedTime, g_ruleConfig.lastResolvedWriteTime)) {
    free(resolvedPath);
    return;
  }

  RuleSet loadedRules = {0};
  RuleLoadError loadError = {0};
  if (!load_rule_set_from_file(resolvedPath, &loadedRules, &loadError)) {
    char* utf8Path = utf8_from_wide(resolvedPath);
    if (utf8Path) {
      log_info("Failed to load replacement config %s at line %zu: %s", utf8Path,
               loadError.lineNumber == 0 ? 1u : loadError.lineNumber, loadError.message);
      free(utf8Path);
    } else {
      log_info("Failed to load replacement config at line %zu: %s", loadError.lineNumber == 0 ? 1u : loadError.lineNumber,
               loadError.message);
    }

    free(g_ruleConfig.lastResolvedPath);
    g_ruleConfig.lastResolvedPath = resolvedPath;
    g_ruleConfig.lastResolvedWriteTime = resolvedTime;
    g_ruleConfig.lastResolvedExists = true;
    g_ruleConfig.lastLoadFailed = true;
    return;
  }

  clear_active_rule_config();
  g_ruleConfig.activePath = resolvedPath;
  g_ruleConfig.activeWriteTime = resolvedTime;
  g_ruleConfig.activeRules = loadedRules;
  g_ruleConfig.hasActiveFile = true;

  free(g_ruleConfig.lastResolvedPath);
  g_ruleConfig.lastResolvedPath = duplicate_wide_string(g_ruleConfig.activePath);
  g_ruleConfig.lastResolvedWriteTime = g_ruleConfig.activeWriteTime;
  g_ruleConfig.lastResolvedExists = g_ruleConfig.lastResolvedPath != NULL;
  g_ruleConfig.lastLoadFailed = false;

  char* utf8Path = utf8_from_wide(g_ruleConfig.activePath);
  if (utf8Path) {
    log_info("Loaded replacement config %s (%zu rule%s)", utf8Path, g_ruleConfig.activeRules.ruleCount,
             g_ruleConfig.activeRules.ruleCount == 1 ? "" : "s");
    free(utf8Path);
  } else {
    log_info("Loaded replacement config (%zu rule%s)", g_ruleConfig.activeRules.ruleCount,
             g_ruleConfig.activeRules.ruleCount == 1 ? "" : "s");
  }
}

static bool substitute_pattern_literal(const RegexPattern* pattern, const wchar_t* replacement, size_t replacementLength,
                                       const wchar_t* subject, size_t subjectLength, wchar_t** outText, size_t* outLength,
                                       int* outCount, char* errorMessage, size_t errorMessageSize) {
  *outText = NULL;
  *outLength = 0;
  *outCount = 0;

  pcre2_match_data* matchData = pcre2_match_data_create_from_pattern(pattern->code, NULL);
  if (!matchData) {
    snprintf(errorMessage, errorMessageSize, "Out of memory while creating regex match data");
    return false;
  }

  PCRE2_SIZE outputLength = subjectLength + replacementLength + 1;
  if (outputLength < 64) {
    outputLength = 64;
  }

  wchar_t* output = NULL;
  int rc = 0;
  for (;;) {
    output = (wchar_t*) malloc(((size_t) outputLength + 1) * sizeof(wchar_t));
    if (!output) {
      snprintf(errorMessage, errorMessageSize, "Out of memory while applying regex replacement");
      pcre2_match_data_free(matchData);
      return false;
    }

    PCRE2_SIZE actualLength = outputLength;
    rc = pcre2_substitute(pattern->code, (PCRE2_SPTR) subject, subjectLength, 0,
                          PCRE2_SUBSTITUTE_GLOBAL | PCRE2_SUBSTITUTE_LITERAL | PCRE2_SUBSTITUTE_OVERFLOW_LENGTH,
                          matchData, NULL, (PCRE2_SPTR) replacement, replacementLength, (PCRE2_UCHAR*) output,
                          &actualLength);
    if (rc == PCRE2_ERROR_NOMEMORY) {
      free(output);
      output = NULL;
      outputLength = actualLength + 1;
      continue;
    }

    if (rc < 0) {
      PCRE2_UCHAR messageBuffer[256];
      int messageLength = pcre2_get_error_message(rc, messageBuffer, sizeof(messageBuffer) / sizeof(messageBuffer[0]));
      char* utf8Message = NULL;
      if (messageLength > 0) {
        utf8Message = utf8_from_wide_length((const wchar_t*) messageBuffer, (size_t) messageLength);
      }
      snprintf(errorMessage, errorMessageSize, "%s", utf8Message ? utf8Message : "Unknown regex substitution error");
      free(utf8Message);
      free(output);
      pcre2_match_data_free(matchData);
      return false;
    }

    output[actualLength] = L'\0';
    *outText = output;
    *outLength = (size_t) actualLength;
    *outCount = rc;
    pcre2_match_data_free(matchData);
    return true;
  }
}

static void apply_configured_replacements(NormalizedBuffer* buffer) {
  if (!buffer || !buffer->text || !g_ruleConfig.hasActiveFile || g_ruleConfig.activeRules.ruleCount == 0) {
    return;
  }

  for (size_t ruleIndex = 0; ruleIndex < g_ruleConfig.activeRules.ruleCount; ++ruleIndex) {
    const RegexRule* rule = &g_ruleConfig.activeRules.rules[ruleIndex];
    bool ruleChanged = false;

    for (size_t patternIndex = 0; patternIndex < rule->patternCount; ++patternIndex) {
      const RegexPattern* pattern = &rule->patterns[patternIndex];
      wchar_t* replacedText = NULL;
      size_t replacedLength = 0;
      int substitutionCount = 0;
      char errorMessage[256] = {0};

      if (!substitute_pattern_literal(pattern, rule->replacement, rule->replacementLength, buffer->text, buffer->length,
                                      &replacedText, &replacedLength, &substitutionCount, errorMessage,
                                      sizeof(errorMessage))) {
        log_info("Regex replacement failed for pattern on line %zu: %s", pattern->lineNumber, errorMessage);
        continue;
      }

      if (substitutionCount == 0) {
        free(replacedText);
        continue;
      }

      free(buffer->text);
      buffer->text = replacedText;
      buffer->length = replacedLength;
      buffer->replacementStats.substitutionsApplied += (size_t) substitutionCount;
      buffer->replacementStats.patternsTouched++;
      ruleChanged = true;
    }

    if (ruleChanged) {
      buffer->replacementStats.rulesTouched++;
    }
  }
}

static NormalizedBuffer normalize_clipboard_text(const wchar_t* input, size_t length) {
  NormalizedBuffer result = {0};
  if (!input) {
    return result;
  }

  result.text = duplicate_wide_range(input, length);
  if (!result.text) {
    return result;
  }

  result.length = length;
  result.lineCount = count_clipboard_lines(input, length);
  apply_configured_replacements(&result);
  return result;
}

static bool set_clipboard_text(HWND hwnd, const wchar_t* text, size_t length) {
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

  void* dest = GlobalLock(hMem);
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

  refresh_replacement_config();

  NormalizedBuffer normalized = normalize_clipboard_text(original.text, original.length);
  if (!normalized.text) {
    log_info("Failed to allocate memory while normalizing clipboard text");
    free_clipboard_buffer(&original);
    return;
  }

  bool changed = false;
  if (normalized.length != original.length) {
    changed = true;
  } else if (wcsncmp(normalized.text, original.text, normalized.length) != 0) {
    changed = true;
  }

  if (!changed) {
    log_info("Clipboard text already normalized (%zu line%s)", normalized.lineCount,
             normalized.lineCount == 1 ? "" : "s");
    free(normalized.text);
    free_clipboard_buffer(&original);
    return;
  }

  g_isUpdatingClipboard = true;
  if (set_clipboard_text(hwnd, normalized.text, normalized.length)) {
    log_info("Applied %zu regex replacement%s across %zu rule%s", normalized.replacementStats.substitutionsApplied,
             normalized.replacementStats.substitutionsApplied == 1 ? "" : "s",
             normalized.replacementStats.rulesTouched, normalized.replacementStats.rulesTouched == 1 ? "" : "s");
    if (!PlaySoundW(L"SystemNotification", NULL, SND_ALIAS | SND_ASYNC | SND_NODEFAULT)) {
      MessageBeep(MB_ICONASTERISK);
    }
  } else {
    log_info("Failed to set normalized text back onto clipboard");
  }
  g_isUpdatingClipboard = false;

  free(normalized.text);
  free_clipboard_buffer(&original);
}

static void release_single_instance_mutex(void) {
  if (g_singleInstanceMutex) {
    ReleaseMutex(g_singleInstanceMutex);
    CloseHandle(g_singleInstanceMutex);
    g_singleInstanceMutex = NULL;
  }
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
  log_info("Requesting previous instance (PID %lu) to exit", (unsigned long) existingPid);
  PostMessageW(existing, WM_APP_EXIT, 0, 0);
  for (int i = 0; i < 200; ++i) {
    if (!IsWindow(existing)) {
      log_info("Previous instance exited");
      return;
    }
    Sleep(25);
  }
  log_info("Previous instance did not exit within timeout; continuing startup");
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
  case WM_APP_EXIT:
    log_info("Received shutdown request from newer instance");
    DestroyWindow(hwnd);
    return 0;
  case WM_CLIPBOARDUPDATE:
    handle_clipboard_update(hwnd);
    return 0;
  case WM_DESTROY:
    RemoveClipboardFormatListener(hwnd);
    PostQuitMessage(0);
    log_info("Shutting down");
    free_rule_config_state();
    free(g_executableDirectory);
    g_executableDirectory = NULL;
    release_single_instance_mutex();
    return 0;
  default:
    return DefWindowProc(hwnd, msg, wParam, lParam);
  }
}

int wmain(void) {
  SetConsoleOutputCP(CP_UTF8);
  log_info("\xC2\xA9 2026 Elefunc, Inc. All rights reserved.");
  log_info("https://elefunc.com");
  log_info("Starting ClipTrim clipboard normalizer");
  log_current_working_directory();
  if (!initialize_executable_directory()) {
    log_info("GetModuleFileName failed (%lu)", GetLastError());
  }
  refresh_replacement_config();

  g_singleInstanceMutex = CreateMutexW(NULL, FALSE, SINGLE_INSTANCE_MUTEX_NAME);
  if (!g_singleInstanceMutex) {
    log_info("CreateMutex failed (%lu)", GetLastError());
    return 1;
  }

  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    request_previous_instance_shutdown();
  }

  DWORD waitResult = WaitForSingleObject(g_singleInstanceMutex, 5000);
  if (waitResult == WAIT_TIMEOUT) {
    log_info("Timed out waiting for previous instance to exit");
    CloseHandle(g_singleInstanceMutex);
    g_singleInstanceMutex = NULL;
    return 1;
  } else if (waitResult == WAIT_FAILED) {
    log_info("WaitForSingleObject on singleton mutex failed (%lu)", GetLastError());
    CloseHandle(g_singleInstanceMutex);
    g_singleInstanceMutex = NULL;
    return 1;
  }

  if (waitResult == WAIT_ABANDONED) {
    log_info("Previous instance ended unexpectedly; continuing startup");
  }

  HINSTANCE hInstance = GetModuleHandle(NULL);

  WNDCLASSEXW wc = {0};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.lpfnWndProc = window_proc;
  wc.hInstance = hInstance;
  wc.lpszClassName = kWindowClassName;
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hIcon = (HICON) LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
  wc.hIconSm = (HICON) LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 16, 16, 0);

  if (!RegisterClassExW(&wc)) {
    log_info("RegisterClassEx failed (%lu)", GetLastError());
    return 1;
  }

  HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClassName, L"Clipboard Whitespace Trimmer", WS_POPUP,
                              CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hInstance, NULL);

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

  release_single_instance_mutex();

  return 0;
}
