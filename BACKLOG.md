# OVCC macOS Backlog

Issues to fix and features to implement, focused on the macOS port. Ordered by priority within each section. File references point at the code as of 2026-07-20.

Every item is also filed as a GitHub issue, in order: §1.1 → [#1](https://github.com/hathaway3/OVCC/issues/1) through §4.4 → [#18](https://github.com/hathaway3/OVCC/issues/18). The issues are the working tracker; this file is the one-page overview.

Note: the codebase itself is multi-platform (Linux and Mingw makefiles exist for every module), but active development and the CI release workflow target macOS only.

---

## P1 — Distribution correctness (released app may not run on other Macs)

### 1.1 `-march=native` makes release binaries non-portable
`Makefile.common:13` compiles with `-Ofast -march=native -flto`. The GitHub release workflow builds on `macos-latest` (Apple Silicon), so published binaries are tuned for the runner's exact CPU and are arm64-only — they cannot run on Intel Macs at all, and may use instructions older Apple Silicon lacks.
- Replace `-march=native` with a safe baseline (e.g. `-O2` or `-Ofast -mcpu=apple-m1` for arm64) for release builds; keep `native` as an opt-in dev flag.
- **Feature**: build a universal binary (`-arch arm64 -arch x86_64`, or lipo two builds) so one `ovcc.app` runs everywhere. Note `-flto` + multi-arch needs care.

### 1.2 Code signing and notarization
The bundle is unsigned. Gatekeeper blocks transferred copies (documented as a manual bypass in `README_MAC.md` §7), and newer macOS versions have made the right-click-Open bypass less reliable.
- Minimum: ad-hoc sign (`codesign --force --deep -s -`) in `build_and_deploy.sh` and CI, and document `xattr -dr com.apple.quarantine ovcc.app` as the fallback.
- Full fix: Developer ID signing + notarization in the release workflow.
- Blocker: signing requires fixing 1.3 and 1.4 first — a valid signature forbids runtime writes inside the bundle and expects a standard layout.

### 1.3 Non-standard bundle layout
The real executable is installed at `Contents/ovcc` (`CoCo/Makefiles/Darwin/makefile:75`), launched by a shell script at `Contents/MacOS/Ovcc`; modules go to `Contents/modules`, dylibs to `Contents/libs`. `Info.plist` (`ovcc.app/Contents/Info.plist`) contains only `CFBundleExecutable` and `CFBundleIconFile`.
- Move to the conventional layout: binary in `Contents/MacOS/`, dylibs in `Contents/Frameworks/`, paks in `Contents/PlugIns/`.
- Fill out `Info.plist`: `CFBundleIdentifier`, `CFBundleName`, `CFBundleVersion`/`CFBundleShortVersionString` (wire to the existing `GIT_VERSION`), `CFBundlePackageType`, `LSMinimumSystemVersion`, `NSHighResolutionCapable`.

### 1.4 Launcher writes symlinks *inside* the bundle; config lives in the bundle
`ovcc.app/Contents/MacOS/Ovcc` symlinks ROMs/`dsks`/`vhds` from the bundle's parent directory into `Contents/`, and `Vcc.ini` + `ovcc.log` are written to the process cwd (also `Contents/`).
- Breaks when the app is in a read-only location, breaks any code signature, and fails under Gatekeeper app translocation (the app runs from a randomized path, so `../../` no longer points at the user's folder).
- User config is destroyed when the app is replaced/updated.
- Fix: read/write `Vcc.ini` and logs in `~/Library/Application Support/OVCC/`; search for ROMs and media there (plus next to the bundle as a convenience) instead of symlinking. Touch points: `CoCo/vcc.c` (`GlobalExecFolder` from `getcwd`), `CoCo/config.c`, `CoCo/iniman.c`.

### 1.5 `DYLD_LIBRARY_PATH` launch mechanism
The launcher runs `env DYLD_LIBRARY_PATH=libs ./ovcc`. Once the binary is signed with a hardened runtime, `DYLD_*` variables are ignored and every dylib/module load breaks. Fix alongside 1.3 by setting proper `@rpath`/install names via `dylibbundler`/`install_name_tool` instead of an env var.

---

## P2 — Build system

### 2.1 Hardcoded `/usr/local/lib` passed to dylibbundler
All ten Darwin makefiles invoke `dylibbundler ... -s /usr/local/lib` (e.g. `CoCo/Makefiles/Darwin/makefile:76`). On Apple Silicon, Homebrew lives in `/opt/homebrew`, so brew-installed dylibs (SDL2, freetype, …) may not be found. `Makefile.common:18` already computes `BREW_PREFIX` — pass `-s $(BREW_PREFIX)/lib -s /usr/local/lib` (libagar installs to `/usr/local`).

### 2.2 `build_and_deploy.sh` rebuilds libagar from scratch every run
The script re-clones libagar, patches it, and `sudo make install`s to `/usr/local` unconditionally, then deletes the source tree.
- Add a check/flag to skip the libagar step when it's already installed (e.g. `agar-config --version` matches), so day-to-day rebuilds don't need sudo or network.
- The inline `python3 -c` string-replacement patches of `gui/drv_cocoa.m` and `fix-dylibs.sh` are fragile; convert them to proper patch files in `Patches/AGAR/` alongside the existing three.
- Consider installing libagar under the Homebrew prefix (or a project-local prefix) instead of `/usr/local` on Apple Silicon.

### 2.3 CI builds a single architecture with no cache
`.github/workflows/release.yml` runs the full `build_and_deploy.sh` (including the libagar clone+build) on one arm64 runner.
- Cache the built libagar between runs.
- Build Intel (`macos-13`) and arm64 (`macos-latest`) and ship a universal or dual-download release (ties into 1.1).
- **Feature**: a non-release CI job that builds on every push/PR so build breakage is caught before tagging.

### 2.4 Misc build cleanup
- `Makefile:26` runs `touch ovcc.app` to refresh the Finder icon cache — harmless but undocumented; comment it or replace with `touch ovcc.app/Contents/Info.plist`.
- `ovcc.app/.DS_Store` is tracked in git despite the `.gitignore` rule (`git rm --cached ovcc.app/.DS_Store`).
- Warnings are globally suppressed (`WARN = -w` in `Makefile.common:14`). At least build the Darwin-specific files with `-Wall` periodically; the Cocoa/AGAR interop is exactly where silent type mismatches bite.

---

## P3 — Emulator behavior on macOS

### 3.1 Keyboard: finish and de-duplicate the Darwin layout table
`CoCo/keyboardLayoutAGAR.c` `#ifdef DARWIN` block (~line 205+):
- Duplicate/conflicting entries: `AG_KEY_BACKQUOTE` and `AG_KEY_TILDE` both map to CoCo tilde; brace entries exist both as `AG_KEY_LEFTBRACKET+SHIFT` (common table) and `AG_KEY_LEFTBRACE` (Darwin table). Audit for first-match vs last-match behavior and remove the losers.
- Right-ALT handling is stubbed out (`#if 0 // TODO: ALT?` in `CoCo/keyboardAGAR.c:327`); left-ALT is the CoCo ALT but right-ALT maps to `@` in the common table — verify this is intended on macOS where Option produces dead keys.
- Non-US layouts: the table assumes US ANSI. At minimum document this; ideally map by character (AGAR provides the unicode value — `vccgui.c` already receives `uc`) rather than by keysym.

### 3.2 Caps Lock synthesizes an immediate down+up pair
`CoCo/vccgui.c:1723` (`#ifdef DARWIN` block): macOS delivers Caps Lock as a toggle, and the workaround fakes a keydown/keyup and then fakes SHIFT for alpha keys while `capslocked` is set. Verify state can't get stuck when the app loses focus while Caps Lock is on (focus loss won't deliver the release toggle). Consider querying the real modifier state on focus-gain.

### 3.3 `getcwd` buffer and `GlobalExecFolder` lifetime
`CoCo/vcc.c` `main()` uses `char cwd[260]` and points the global `GlobalExecFolder` at this stack buffer. macOS `PATH_MAX` is 1024 — a deep user path truncates or fails. Use `PATH_MAX` and copy into static/heap storage. (Becomes moot for config paths if 1.4 lands, but the buffer size is still wrong.)

### 3.4 Verify HiDPI/Retina rendering
After adding `NSHighResolutionCapable` (1.3), check that the AGAR cocoa driver + `tcc1014graphicsAGAR.c` render at the right scale and that the artifact-color/composite modes still look correct. Currently the app renders at 1x and is scaled by the OS.

### 3.5 Confirm the `mpu` module works on Apple Silicon
`mpu/` (GPU/FPU/DMA coprocessor) is built on Darwin but its float/threading behavior was developed on Linux. Run its functionality (e.g. `flt` test data in `mpu/flt`) on arm64 and check for endian/float-precision assumptions.

---

## P4 — Documentation and developer experience

### 4.1 Broken link in README.md
`README.md:47` links the macOS guide as `file:///Users/jimmiehathaway/OVCC/README_MAC.md` — an absolute local path (with a different username). Replace with a relative link: `[macOS Build and Run Guide](README_MAC.md)`.

### 4.2 README_MAC.md drift
- §7 says compilation flags "automatically include `-I/opt/homebrew/include`" — true only via `brew --prefix`; clarify.
- Document the quarantine `xattr` workaround next to the Gatekeeper section (until 1.2 lands).
- Document that `make install` must run **after** a full `make` and what lands where in the bundle.

### 4.3 Packaging convenience (feature)
Once 1.1–1.3 land: ship a DMG with a drag-to-Applications layout instead of a zip, and/or a Homebrew cask. The release workflow already computes the version string for naming.

### 4.4 First-run ROM experience (feature)
On first launch without `coco3.rom` the emulator is a black screen. Add a startup dialog (AGAR file dialog already exists for cart loading) that prompts for ROM locations and stores them in the (relocated, per 1.4) config — this replaces most of what the symlink launcher hack was for.
