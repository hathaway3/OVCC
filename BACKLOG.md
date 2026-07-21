# OVCC macOS Backlog

Issues to fix and features to implement, focused on the macOS port. Ordered by priority within each section.

**Status legend:** ✅ Done · 🟡 Partial · ❌ Open. Status reviewed against the code as of **2026-07-21**; original problem descriptions were written 2026-07-20.

Every item was also filed as a GitHub issue, in order: §1.1 → [#1](https://github.com/hathaway3/OVCC/issues/1) through §4.4 → [#18](https://github.com/hathaway3/OVCC/issues/18). The issues are the working tracker; this file is the one-page overview.

Note: the codebase itself is multi-platform (Linux and Mingw makefiles exist for every module), but active development and the CI release workflow target macOS only.

**Where things stand:** P1 (distribution correctness) is essentially complete and P2 (build system) is nearly complete. What remains is CI (§2.3), Developer-ID signing/notarization (§1.2), the universal-binary feature (§1.1), and the P3/P4 polish and feature items.

---

## P1 — Distribution correctness (released app may not run on other Macs)

### 1.1 `-march=native` makes release binaries non-portable — 🟡 Partial (portability fixed)
`Makefile.common` no longer uses `-march=native` on macOS: Darwin/arm64 builds with `-Ofast -mcpu=apple-m1 -flto` and other Darwin builds with `-Ofast -flto` (native remains the default only on the non-Darwin branch). Published arm64 binaries are now portable across Apple Silicon.
- **Feature (open):** build a universal binary (`-arch arm64 -arch x86_64`, or lipo two builds) so one `ovcc.app` runs on Intel too. Note `-flto` + multi-arch needs care.

### 1.2 Code signing and notarization — 🟡 Partial
`build_and_deploy.sh` ad-hoc signs the finished bundle (`codesign --force --deep -s - ovcc.app`), and with §1.3/§1.4 done the bundle no longer writes inside itself at runtime, so a real signature is now viable.
- **Open:** Developer ID signing + notarization in the release workflow; document `xattr -dr com.apple.quarantine ovcc.app` as the transfer fallback.

### 1.3 Non-standard bundle layout — ✅ Done
The bundle now uses the conventional layout: binary in `Contents/MacOS/`, dylibs in `Contents/Frameworks/`, paks in `Contents/PlugIns/` (see the `install` targets in `CoCo/Makefiles/Darwin/makefile` and each module makefile, driven by `dylibbundler`). `Info.plist` (generated from `Makefiles/Darwin/Info.plist.template`) carries `CFBundleIdentifier`, `CFBundleName`, `CFBundleVersion`/`CFBundleShortVersionString` (wired to `GIT_VERSION`), `CFBundlePackageType`, `LSMinimumSystemVersion`, and `NSHighResolutionCapable`.

### 1.4 Launcher writes symlinks *inside* the bundle; config lives in the bundle — ✅ Done
The launcher no longer symlinks anything into the bundle. ROMs, media, and `Vcc.ini` are located at runtime by `ResolvePlatformPath()` (`CoCo/fileops.c`), which searches `~/Library/Application Support/OVCC/` → `Contents/PlugIns/` → the folder containing `ovcc.app` → the executable dir. `coco3.rom` (`tcc1014mmu_mm.c` / `_nomm.c`), `disk11.rom` (`FD502/fd502.c`), disk images (via `CheckPath`), and the ini (`config.c`) all go through it. Paths derive from `_NSGetExecutablePath`, so it is Gatekeeper-translocation-safe, and the bundle is fully relocatable into `/Applications`.
- **Follow-up (open, minor):** `Module/OnBoot` stores an *absolute* plugin path, so moving the app leaves a stale auto-load path (re-loading the cart fixes it). Could store the bundle-relative name and resolve via the same search (`ResolvePlatformPath` already checks `Contents/PlugIns/`).

### 1.5 `DYLD_LIBRARY_PATH` launch mechanism — ✅ Done
The launcher (`Makefiles/Darwin/ovcc-launch`, installed as `CFBundleExecutable`) no longer sets `DYLD_*`; it just `exec`s the binary. All dylib/pak loads resolve via `@executable_path`/`@loader_path`/`@rpath` install names set by `dylibbundler`, so a hardened-runtime signature won't break loading.

---

## P2 — Build system

### 2.1 Hardcoded `/usr/local/lib` passed to dylibbundler — ✅ Done
All Darwin makefiles now pass `-s $(BREW_PREFIX)/lib -s /usr/local/lib` to `dylibbundler`, so brew dylibs are found under `/opt/homebrew` on Apple Silicon (and libagar under its project-local prefix / `/usr/local`).

### 2.2 `build_and_deploy.sh` rebuilds libagar from scratch every run — ✅ Done (mostly)
The script now builds libagar into a **project-local prefix** (`libagar-install/`, no `sudo`, no network on repeat runs) and skips the build when a valid install is already present — the check requires the shared `libag_gui.8.dylib` to exist, so a broken static-only install is rebuilt. libagar is configured with `--enable-shared`, and the macOS-specific fixes are proper patch files in `Patches/AGAR/` (`0004` drv_cocoa, `0005` shared-dylib link).
- **Open (minor):** the `fix-dylibs.sh` install-name step is still generated inline rather than being a patch file.

### 2.3 CI builds a single architecture with no cache — ❌ Open
`.github/workflows/release.yml` runs the full `build_and_deploy.sh` (including the libagar build) on one arm64 runner.
- Cache the built libagar between runs.
- Build Intel (`macos-13`) and arm64 (`macos-latest`) → universal or dual-download release (ties into §1.1).
- **Feature:** a non-release CI job that builds on every push/PR so breakage is caught before tagging.

### 2.4 Misc build cleanup — 🟡 Partial
- ✅ `ovcc.app/.DS_Store` untracked and removed from git.
- ✅ Warnings: Darwin now builds with `-Wall` (`Makefile.common`); other platforms keep `-w`.
- ❌ `Makefile:26` still runs `touch ovcc.app` to refresh the Finder icon cache — harmless but undocumented; comment it or target `Contents/Info.plist`.

---

## P3 — Emulator behavior on macOS

### 3.1 Keyboard: finish and de-duplicate the Darwin layout table — ❌ Open
`CoCo/keyboardLayoutAGAR.c` `#ifdef DARWIN` block:
- Duplicate/conflicting entries (`AG_KEY_BACKQUOTE`/`AG_KEY_TILDE`; brace entries in both common and Darwin tables). Audit first-match vs last-match and remove the losers.
- Right-ALT handling is stubbed (`#if 0 // TODO: ALT?` in `CoCo/keyboardAGAR.c`); verify behavior on macOS where Option produces dead keys.
- Non-US layouts assume US ANSI; ideally map by character (the unicode value is already available) rather than by keysym.

### 3.2 Caps Lock synthesizes an immediate down+up pair — ❌ Open
`CoCo/vccgui.c` `#ifdef DARWIN` block fakes a keydown/keyup and then SHIFT for alpha keys while `capslocked`. Verify the state can't get stuck when the app loses focus with Caps Lock on (focus loss won't deliver the release toggle); consider querying real modifier state on focus-gain.

### 3.3 `getcwd` buffer and `GlobalExecFolder` lifetime — ✅ Done (on Darwin)
On Darwin, `GlobalExecFolder` is derived from `_NSGetExecutablePath` into static storage (`CoCo/vcc.c`), not the small `getcwd` stack buffer, so the macOS `PATH_MAX` concern no longer applies. (The non-Darwin `getcwd` buffer size is a separate, lower-priority nit.)

### 3.4 Verify HiDPI/Retina rendering — ❌ Open
`NSHighResolutionCapable` is now set, but the app still needs checking that the AGAR cocoa driver + `tcc1014graphicsAGAR.c` render at native scale (and that artifact-color/composite modes still look correct) rather than being 1× scaled by the OS.

### 3.5 Confirm the `mpu` module works on Apple Silicon — ❌ Open
`mpu/` (GPU/FPU/DMA coprocessor) builds on Darwin but its float/threading behavior was developed on Linux. Exercise it on arm64 (e.g. `mpu/flt` test data) and check endian/float-precision assumptions.

---

## P4 — Documentation and developer experience

### 4.1 Broken link in README.md — ✅ Done
`README.md` now links the macOS guide with a relative path: `[macOS Build and Run Guide](README_MAC.md)`.

### 4.2 README_MAC.md drift — 🟡 Partial
- ✅ §3 now documents the runtime ROM/media search order and the relocatable `/Applications` install with ROMs in `~/Library/Application Support/OVCC/`.
- ❌ Still to do: clarify the `-I$(BREW_PREFIX)/include` flag wording; document the quarantine `xattr` workaround by the Gatekeeper section (until §1.2 lands); note that `make install` must run after a full `make`.

### 4.3 Packaging convenience (feature) — ❌ Open
Once §1.1–§1.3 land (they have): ship a DMG with a drag-to-Applications layout instead of a zip, and/or a Homebrew cask. The release workflow already computes the version string for naming.

### 4.4 First-run ROM experience (feature) — ❌ Open
On first launch without `coco3.rom` the emulator is a black screen. Add a startup dialog (the AGAR file dialog already exists for cart loading) that prompts for ROM locations and stores them in the (relocated) config. Now that §1.4 resolves ROMs from `~/Library/Application Support/OVCC/`, this is the remaining piece of what the old symlink launcher hack was for.

---

## Resolved along the way (not originally in the backlog)

These macOS bugs were found and fixed while completing the P1/P2 work:

- **Static-linked AGAR broke plugin menus.** libagar was linked statically into both `ovcc` and every pak, so each had its own copy of AGAR's global state and plugins couldn't add to the host menus. Fixed by building libagar as a shared library (patch `0005` + `--enable-shared`) so there is one shared AGAR instance.
- **No plugin could load** (`Cartridge → Load Cart` did nothing). `InsertModule` prepended `./` to absolute paths, mangling them so `SDL_LoadObject` failed. Fixed in `CoCo/pakinterface.c`.
- **MPI config dialog corrupted on slot switch.** The polled status label resized without a layout pass and overflowed the controls below it; fixed by re-running `AG_WindowUpdate` on slot change (`mpi/mpi.c`).
- **Launcher name collision.** The `Ovcc` launcher collided case-insensitively with the `ovcc` binary; renamed to `ovcc-launch`.
