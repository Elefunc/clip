# `cliptrim.exe` – Repository Guidelines
![Cliptrim icon](cliptrim.png)

> Maintained by **Elefunc, Inc.** · https://elefunc.com

## Project Structure & Module Organization
- `cliptrim.c`: Primary Win32 entry point, message loop, trimming logic, and icon loading. Keep platform-specific code here and prefer static helpers over new translation units until complexity demands otherwise.
- `cliptrim.rc`: Resource script linking `cliptrim.ico` as the main application icon; update IDs here when adding resources.
- `cliptrim.ico`: Multi-resolution icon generated from `cliptrim.png`; update via `convert cliptrim.png -define icon:auto-resize=256,128,64,48,32,16 cliptrim.ico`.
- `Makefile`: Cross-build orchestration. Targets `cliptrim32.exe` and `cliptrim64.exe` with shared warning flags and resource compilation. Add new source files via the `SRC` variable before introducing subdirectories.

## Build, Test, and Development Commands
- `make`: Builds both 32-bit and 64-bit executables using mingw-w64 cross-compilers. Outputs land in the repository root.
- `make clean`: Removes generated executables. Run before committing to keep the repo binary-free.
- `wine cliptrim64.exe` (optional): Quick smoke test on Linux hosts with Wine; confirm console logs appear and clipboard updates succeed.

## Coding Style & Naming Conventions
- Target C11 (`-std=c11`) with UTF-16 Windows APIs. Maintain 2-space indentation and brace-on-same-line style already present in `cliptrim.c`.
- Prefer descriptive static helpers (e.g., `handle_clipboard_update`) and snake_case identifiers.
- Log messages should remain concise, capitalized, and timestamp-friendly to preserve console readability.
- Run `clang-format -i cliptrim.c` before committing; configuration lives in `.clang-format`.

## Testing Guidelines
- No automated tests yet. After building, validate manually on Windows by running `cliptrim64.exe`, then issuing `echo "  sample text  " | clip` to confirm whitespace trimming and log output.
- Check both CRLF and LF-only sources (e.g., paste from Notepad and from WSL) to ensure line break preservation.
- Document new edge cases in pull requests so future automation can capture them.

## Commit & Pull Request Guidelines
- Follow the existing concise, imperative commit style (e.g., "Add clipboard trimmer source and build"). Keep scope narrow and explain user-facing behavior changes in the body when needed.
- Pull requests should link relevant issues, summarize behavior changes, list manual tests (commands run, environments), and include screenshots of console logs when they help reviewers.

## Agent Notes & Environment
- Cross-compilers (`x86_64-w64-mingw32-gcc`, `i686-w64-mingw32-gcc`) are expected locally; verify versions before onboarding.
- On Ubuntu 24.04, install the toolchain via `sudo apt install mingw-w64` before running `make`.
- Avoid adding compiled binaries to version control. Update `.gitignore` if new build artifacts emerge.
- When editing, prefer `apply_patch` or targeted diffs to keep reviews small and traceable.

---

Cliptrim is an Elefunc, Inc. project — reach us at https://elefunc.com for support.
