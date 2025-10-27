# `cliptrim.exe` – Repository Guidelines
![Cliptrim icon](cliptrim.png)

> Maintained by **Elefunc, Inc.** · https://elefunc.com

## Project Structure & Module Organization
- `cliptrim.c`: Primary Win32 entry point, message loop, trimming logic, and icon loading. Keep platform-specific code here and prefer static helpers over new translation units until complexity demands otherwise.
- `cliptrim.rc`: Resource script linking `cliptrim.ico` as the main application icon; update IDs here when adding resources.
- `cliptrim.ico`: Multi-resolution icon generated from `cliptrim.png`; update via `convert cliptrim.png -define icon:auto-resize=256,128,64,48,32,16 cliptrim.ico`.
- `Makefile`: Cross-build orchestration. Targets `cliptrim32.exe` and `cliptrim64.exe` with shared warning flags and resource compilation. Add new source files via the `SRC` variable before introducing subdirectories.
- `paste/paste.c`: Win32 console utility that emits clipboard text (UTF-8) or images (PNG) to stdout. Keep clipboard-specific helpers local until the feature set justifies additional files.
- `paste/Makefile`: Dedicated build script for `paste32.exe` and `paste64.exe`, sharing warning flags and link settings with the top-level build.

## Build, Test, and Development Commands
- `make`: Builds both Cliptrim executables using mingw-w64 cross-compilers. Outputs land in the repository root.
- `make clean`: Removes generated Cliptrim artifacts. Run before committing to keep the repo binary-free.
- `make -C paste`: Builds `paste64.exe` and `paste32.exe` with the same toolchain defaults. Pair with `make -C paste clean` when tidying the workspace.
- `wine cliptrim64.exe` (optional): Quick smoke test on Linux hosts with Wine; confirm console logs appear and clipboard updates succeed.
- `./paste/paste64.exe --text` (optional): Validate text clipboard capture on Windows or WSL->Windows clipboard; append `--image` and redirect stdout to verify PNG output.

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

## Release Checklist
1. **Update version references**
   - Edit `cliptrim.rc` and bump `FILEVERSION`, `PRODUCTVERSION`, and the `StringFileInfo` `FileVersion`/`ProductVersion` strings to the new semantic version (e.g., `0.5.2`).
   - Regenerate the build after editing to refresh `cliptrim32.res`/`cliptrim64.res`.
   - Commit the change with a message like `Bump version to 0.x.y` and push it upstream.
2. **Make a clean build**
   - Run `make clean` to remove prior artifacts.
   - Run `make` to rebuild `cliptrim32.exe` and `cliptrim64.exe`; confirm both executables are produced without warnings.
3. **Make a GitHub release**
   - Tag the release (`git tag -a v0.x.y -m "Cliptrim 0.x.y"` and `git push origin v0.x.y`).
   - Publish it with the CLI: `gh release create v0.x.y cliptrim64.exe cliptrim32.exe paste/paste64.exe paste/paste32.exe --title "Cliptrim 0.x.y" --notes "<bullet summary + checks>"`.
   - Double-check the release page to ensure all four executables and notes rendered correctly.

## Agent Notes & Environment
- Cross-compilers (`x86_64-w64-mingw32-gcc`, `i686-w64-mingw32-gcc`) are expected locally; verify versions before onboarding.
- On Ubuntu 24.04, install the toolchain via `sudo apt install mingw-w64` before running `make`.
- Avoid adding compiled binaries to version control. Update `.gitignore` if new build artifacts emerge.
- When editing, prefer `apply_patch` or targeted diffs to keep reviews small and traceable.

---

Cliptrim is an Elefunc, Inc. project — reach us at https://elefunc.com for support.
