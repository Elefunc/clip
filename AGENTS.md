# Clip – Repository Guidelines
![Clip icon](trim/trim.png)

> Maintained by **Elefunc, Inc.** · https://elefunc.com

## Project Structure & Module Organization
- `trim/`
  - `trim.c`: Primary Win32 entry point, message loop, default-rule generation, rule loading, regex replacement logic, and icon loading. Keep platform-specific code here and prefer static helpers over new translation units until complexity demands otherwise.
  - `trim.rc`: Resource script linking `trim/trim.ico` as the main application icon and embedding version metadata; update IDs here when adding resources, and refresh version/copyright strings during releases.
  - `trim.h`: Shared resource identifiers consumed by both the resource script and the C runtime.
  - `trim.ico`: Multi-resolution icon generated from `trim/trim.png`; update via `convert trim/trim.png -define icon:auto-resize=256,128,64,48,32,16 trim/trim.ico`.
  - `trim.png`: Source artwork for the Trim application icon—regenerate `.ico` variants from this PNG.
  - `trim.rules.example`: Sample UTF-8 rule file showing the generated default rules, inline syntax comments, and the multiline regex replacement format; copy it to `trim.rules` in the launch directory to override the executable-side default config.
  - `third_party/pcre2/`: Vendored PCRE2 sources used for multiline regex replacement support. Keep local patches limited to the copied `config.h`, generated `pcre2.h`, and build integration.
  - `Makefile`: Cross-build orchestration. Targets `trim32.exe` and `trim64.exe`, builds vendored PCRE2 objects under `trim/obj/`, and signs rebuilt executables. Add new source files via the declared source variables before introducing more build indirection.
- `paste/`
  - `paste.c`: Win32 console utility that emits clipboard text (UTF-8) or images (PNG) to stdout. Keep clipboard-specific helpers local until the feature set justifies additional files.
  - `paste.rc`: Resource script embedding `paste/paste.ico` as the application icon alongside version metadata; update here when bumping versions, changing company/product strings, or rolling the copyright year.
  - `paste.ico`: Multi-resolution icon generated from `paste/paste.png`; update via `convert paste/paste.png -define icon:auto-resize=256,128,64,48,32,16 paste/paste.ico`.
  - `paste.png`: Source artwork for the Paste application icon—regenerate `.ico` variants from this PNG.
  - `Makefile`: Dedicated build script for `paste32.exe` and `paste64.exe`, storing intermediate objects under `paste/obj/`, leaving the executables in place, and rebuilding resources when `paste.ico` changes.
- `board/`
  - `board.c`: Win32 GUI clipboard viewer entry point, clipboard format detection, custom DirectWrite text display, WIC/SVG image rendering through the Direct2D device-context path, dark-mode/high-contrast handling, DPI-aware image zoom/window sizing, font selection, tooltip text, keyboard shortcuts, and single-instance handling. Keep related helpers local until the file becomes too large to navigate safely.
  - `d2d1_3_compat.h`: Local C-compatible declarations for the Direct2D image-source and SVG interfaces used by Board's image renderer. Keep this header aligned with `board.c` and list it as a Makefile dependency when adding or changing Direct2D interface use.
  - `board.rc`: Resource script embedding `board/board.ico`, the manifest, and version metadata; update version/product/copyright strings here during releases.
  - `board.h`: Shared resource identifiers consumed by the resource script.
  - `board.ico`: Multi-resolution icon generated from `board/board.png`; update via `convert board/board.png -define icon:auto-resize=256,128,64,48,32,16 board/board.ico`.
  - `board.png`: Source artwork for the Board application icon—regenerate `.ico` variants from this PNG.
  - `board.manifest`: DPI-awareness manifest for the GUI app. Keep it aligned with runtime DPI setup in `board.c`.
  - `Makefile`: Dedicated build script for `board32.exe` and `board64.exe`, storing intermediate objects under `board/obj/`, linking Direct2D/D3D11/DXGI/DirectWrite/WIC/DWM/UxTheme/common-controls dependencies, leaving the executables in place, rebuilding resources when icon/manifest/resource inputs change, and signing rebuilt executables.

## Build, Test, and Development Commands
- `cd trim && make`: Builds both Trim executables using mingw-w64 cross-compilers, compiles the vendored PCRE2 sources, and automatically code-signs any rebuilt `.exe` outputs with `cs`. Outputs land inside the `trim` directory.
- `cd trim && make clean`: Removes generated Trim artifacts. Run before committing to keep the repo binary-free.
- `cd paste && make`: Builds `paste64.exe` and `paste32.exe` with the same toolchain defaults, then automatically code-signs any rebuilt `.exe` outputs with `cs`. Pair with `cd paste && make clean` when tidying the workspace.
- `cd board && make`: Builds `board64.exe` and `board32.exe` with the same toolchain defaults, links Direct2D/D3D11/DXGI/DirectWrite/WIC/DWM/UxTheme/common-controls dependencies, and automatically code-signs rebuilt executables with `cs`. Pair with `cd board && make clean` when tidying the workspace.
- All three builds emit intermediate `.res` files (e.g., `trim/trim64.res`, `paste/paste64.res`, `board/board64.res`) generated by `windres`, and keep `.o` files under each directory's `obj/` subdirectory; they are safe to delete via each directory's `make clean`.
- `cs trim/trim64.exe trim/trim32.exe paste/paste64.exe paste/paste32.exe board/board64.exe board/board32.exe`: Manually sign one or more executables with the local Azure Trusted Signing wrapper. Useful for re-signing existing artifacts without rebuilding them.
- `wine trim/trim64.exe` (optional): Quick smoke test on Linux hosts with Wine; confirm console logs appear and clipboard updates succeed.
- `./paste/paste64.exe --text` (optional): Validate text clipboard capture on Windows or WSL->Windows clipboard; append `--image` and redirect stdout to verify PNG output.
- `./board/board64.exe` (optional on Windows/WSL interop): Validate the GUI clipboard viewer against current text, bitmap image, and unsupported clipboard states.

## Coding Style & Naming Conventions
- Target C11 (`-std=c11`) with UTF-16 Windows APIs. Maintain 2-space indentation and brace-on-same-line style already present in `trim.c`.
- Prefer descriptive static helpers (e.g., `handle_clipboard_update`) and snake_case identifiers.
- Log messages should remain concise, capitalized, and timestamp-friendly to preserve console readability.
- Run `clang-format -i trim/trim.c paste/paste.c board/board.c` on touched C sources before committing; configuration lives in `.clang-format`.

## Testing Guidelines
- No automated tests yet. After building, validate manually on Windows by running `trim/trim64.exe` with no existing config, then confirm it generates `trim.rules` beside the executable and preserves empty lines while trimming the same trailing whitespace set as the original pre-regex implementation.
- Copy `trim/trim.rules.example` to `trim.rules` in the working directory, then confirm it removes a leading `›` or `»` from the full clipboard string and trims trailing per-line whitespace without collapsing blank lines.
- Check both CRLF and LF-only sources (e.g., paste from Notepad and from WSL) to ensure line break preservation.
- Edit `trim.rules` while `trim` is running and confirm the next clipboard update uses the reloaded rules without restarting the process.
- For Paste, run `./paste/paste64.exe --text` after copying Unicode text and verify UTF-8 stdout. Run `./paste/paste64.exe --image > out.png` after copying a bitmap image and verify the PNG opens.
- For Board, run `board/board64.exe` on Windows, then copy text and confirm the title uses `# chars (<encoding>)`, the custom text view is borderless, uses the monospace fallback order, and the font tooltip lists the three families. Confirm dark/light mode follows Windows app theme, high contrast uses system colors, and dark-mode launches do not flash a white client area.
- In Board text mode, verify `Alt+Z` toggles word wrap, `T` and `Ctrl+T` toggle always-on-top with `[Top]` in the title, and `F`, `F11`, and `Alt+Enter` toggle fullscreen. Confirm `Ctrl+A`, `Ctrl+C`, scrolling, selection, and zoom shortcuts still work.
- In Board image mode, copy bitmap and SVG clipboard content and confirm the title uses `W×H - #%` or `SVG W×H - #%`, double-click resets to 100%, `0`/`1`-`9` and `Alt+1`-`Alt+9` zoom shortcuts toggle fixed zooms back to the previous zoom even when the requested zoom is monitor-constrained, and the window remains DPI/aspect-ratio aware. Check transparent images against the checker background, SVGs with explicit dimensions and `viewBox`, and unsupported content without crashes.
- Document new edge cases in pull requests so future automation can capture them.

## Commit & Pull Request Guidelines
- Follow the existing concise, imperative commit style (e.g., "Add clipboard trimmer source and build"). Keep scope narrow and explain user-facing behavior changes in the body when needed.
- Pull requests should link relevant issues, summarize behavior changes, list manual tests (commands run, environments), and include screenshots of console logs when they help reviewers.

## Release Checklist
1. **Update version references**
   - Edit `trim/trim.rc`, `paste/paste.rc`, and `board/board.rc`, then bump `FILEVERSION`, `PRODUCTVERSION`, and the `StringFileInfo` `FileVersion`/`ProductVersion` strings to the new semantic version (e.g., `0.5.2`).
   - If the release crosses into a new calendar year, update `LegalCopyright` in all resource scripts and the startup banner in `trim/trim.c`.
   - Regenerate the build after editing to refresh `trim/trim32.res`/`trim/trim64.res`, `paste/paste32.res`/`paste/paste64.res`, and `board/board32.res`/`board/board64.res`.
   - Commit the change with a message like `Bump version to 0.x.y` and push it upstream.
2. **Make a clean build**
   - Run `cd trim && make clean` to remove prior artifacts.
   - Run `cd trim && make` to rebuild `trim32.exe` and `trim64.exe`; confirm both executables are produced without warnings and that signing is attempted for each rebuilt `.exe`.
   - Run `cd paste && make clean && make` to rebuild `paste32.exe` and `paste64.exe`; confirm both executables are produced without warnings and that signing is attempted for each rebuilt `.exe`.
   - Run `cd board && make clean && make` to rebuild `board32.exe` and `board64.exe`; confirm both executables are produced without warnings and that signing is attempted for each rebuilt `.exe`.
   - If you need to re-sign already-built artifacts without rebuilding them, run `cs trim/trim64.exe trim/trim32.exe paste/paste64.exe paste/paste32.exe board/board64.exe board/board32.exe` from the repository root.
3. **Verify the signatures**
   - Run `osslsigncode verify -in <file>` for each signed executable.
   - If the local Linux CA bundle does not include Microsoft identity-verification roots, pass a PEM bundle with `-CAfile` and `-TSA-CAfile` so `osslsigncode` can validate the signing and timestamp chains.
4. **Make a GitHub release**
   - Tag the release (`git tag -a v0.x.y -m "Clip 0.x.y"` and `git push origin v0.x.y`).
   - Publish it with the CLI: `gh release create v0.x.y trim/trim64.exe trim/trim32.exe paste/paste64.exe paste/paste32.exe board/board64.exe board/board32.exe --title "Clip 0.x.y" --notes "<bullet summary + checks>"`.
   - Double-check the release page to ensure all six signed executables and notes rendered correctly.

## Agent Notes & Environment
- Cross-compilers (`x86_64-w64-mingw32-gcc`, `i686-w64-mingw32-gcc`) are expected locally; verify versions before onboarding.
- On Ubuntu 24.04, install the toolchain via `sudo apt install mingw-w64` before running `make`.
- Use the local `cs` wrapper for code signing instead of calling `sign.exe` directly; directory-local `make` now signs rebuilt `.exe` targets automatically, while `cs <file> [file ...]` remains available for manual or repeat signing.
- Signing failures during `make` are warnings, not build failures. Link failures still fail the build.
- Board's image renderer uses a Direct2D device-context pipeline backed by D3D11/DXGI and WIC image sources, with SVG support via `d2d1_3_compat.h`. Keep the Board Makefile libraries and header dependency in sync with these renderer requirements.
- `trim` looks for `trim.rules` in the current working directory first, then beside `trim.exe` if the working-directory file is missing. If both are absent, it generates the default `trim.rules` beside `trim.exe` on first launch.
- `trim.rules` is UTF-8 and uses `rule`, repeated `pattern <<TOKEN` blocks, and one `replace <<TOKEN` block per rule. Generated default files include syntax comments; block bodies exclude the terminator line break, so leave a blank line before the terminator if you need the replacement to end with a newline. Replacement text is literal; regex behavior comes from PCRE2 patterns and inline modifiers.
- The generated default rules first remove a leading `›` or `»` from the full clipboard string via two explicit `\A›` and `\A»` patterns, then trim the same trailing `iswspace()` set the original pre-regex code removed, using `[ \t\f\x0B\x{00A0}\x{1680}\x{180E}\x{2000}-\x{200A}\x{2028}\x{2029}\x{202F}\x{205F}\x{3000}]+(?=\r\n?|\n|\z)`.
- Existing `trim.rules` files are not auto-migrated. Delete or edit older generated files manually if you need them to pick up the corrected default rule.
- Replacement configs reload automatically when the selected `trim.rules` file changes. Invalid updates are logged and the last known good rules remain active.
- `osslsigncode` is useful for Linux-side verification. If verification fails with a Microsoft chain error, provide explicit `-CAfile` and `-TSA-CAfile` inputs instead of assuming the signature is invalid.
- `.gitignore` currently ignores generated `.exe`, `.res`, and `.o` outputs, any `obj/` directory, logs, and the local generated `trim/trim.rules`; keep it in sync if the build layout or runtime-generated files change.
- When editing, prefer `apply_patch` or targeted diffs to keep reviews small and traceable.

---

Clip is an Elefunc, Inc. project — reach us at https://elefunc.com for support.
