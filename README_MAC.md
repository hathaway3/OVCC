# OVCC on macOS: Build, Package, and Run Guide

This guide provides comprehensive instructions on how to build, package, and run the **OVCC (Open Virtual Colour Computer)** emulator on macOS.

---

## Table of Contents

1. [Introduction & Architecture](#1-introduction--architecture)
2. [Prerequisites](#2-prerequisites)
3. [ROMs and Directory Structure](#3-roms-and-directory-structure)
4. [Method 1: Automated Build (Recommended)](#4-method-1-automated-build-recommended)
5. [Method 2: Manual Build (Step-by-Step)](#5-method-2-manual-build-step-by-step)
6. [Running the Emulator](#6-running-the-emulator)
7. [Troubleshooting & macOS-Specific Tips](#7-troubleshooting--macos-specific-tips)

---

## 1. Introduction & Architecture

OVCC is a portable Tandy Color Computer 3 emulator based on the Windows VCC emulator. On macOS, OVCC uses the **Cocoa graphics driver** provided by the **AGAR GUI toolkit**. 

### The macOS Windowing Caveat
On macOS, if a graphical application is run directly as a bare command-line binary from a terminal:
* The OS will **not** assign keyboard or mouse input focus to the application's window.
* Keyboard inputs will continue to be received by the parent terminal, making the emulator unusable.

To resolve this, OVCC must be run from a standard macOS **Application Bundle** (`ovcc.app`). The bundle utilizes a startup launcher script (`ovcc.app/Contents/MacOS/ovcc-launch`, set as the bundle's `CFBundleExecutable`) that sets up the environment and runs the `ovcc` binary under a GUI context.

> **Note:** the launcher is deliberately *not* named `Ovcc`. macOS filesystems are case-insensitive by default, so `Ovcc` and the `ovcc` binary would be treated as the same file and collide in `Contents/MacOS/`. The distinct `ovcc-launch` name lets both coexist.

Additionally, to ensure the `.app` bundle is fully portable and self-contained, the build process utilizes `dylibbundler`. This utility automatically copies all dynamic library dependencies (such as `SDL2`, `libagar`, `freetype`, etc.) into the bundle and updates their install names.

---

## 2. Prerequisites

Before building OVCC, ensure you have the following installed:

1. **Xcode Command Line Tools**:
   ```bash
   xcode-select --install
   ```
2. **Homebrew**:
   Install Homebrew from [brew.sh](https://brew.sh/) if it is not already present.
3. **Homebrew Dependencies**:
   Install the required libraries and utilities:
   ```bash
   brew install sdl2 dylibbundler pkg-config
   ```

---

## 3. ROMs and Directory Structure

OVCC requires Color Computer 3 ROM files to boot. It locates ROMs and media at
runtime by searching, in order:

1. `~/Library/Application Support/OVCC/` — **recommended**, and where `Vcc.ini` lives
2. the bundle's `Contents/PlugIns/` directory
3. the folder that contains `ovcc.app`
4. the executable directory inside the bundle

### Relocatable install (drag into /Applications)

Because of that search order, `ovcc.app` is **fully self-contained and relocatable** —
you can drag it into `/Applications` (or anywhere) and run it, with nothing required
beside it and nothing written back into the bundle at runtime. Just put your ROMs and
media in `~/Library/Application Support/OVCC/`:

```
~/Library/Application Support/OVCC/
├── Vcc.ini                 # created on first run
├── coco3.rom               # (Required) CoCo 3 ROM image (32768 bytes)
├── disk11.rom              # (Required) Disk Controller ROM image (8192 bytes)
├── roms/  dsks/  vhds/     # (Optional) peripheral ROMs / disk / hard-disk images
```

### Alternative: portable / dev layout

Or keep everything together **directly next to the `ovcc.app` bundle** (search
location 3 above) — handy for a self-contained folder you can move around or for
running out of the build tree:

```
Emulator-Folder/
├── ovcc.app/               # The compiled application bundle
├── coco3.rom               # (Required) CoCo 3 ROM image (32768 bytes)
├── disk11.rom              # (Required) Disk Controller ROM image (8192 bytes)
├── rgbdos.rom              # (Optional) RGB-DOS ROM
├── roms/                   # (Optional) Subfolder for other peripheral/MPI ROMs
│   ├── orch90.rom
│   └── hdblba.rom
├── dsks/                   # (Optional) Subfolder for floppy disk images (.dsk)
└── vhds/                   # (Optional) Subfolder for hard disk images (.vhd)
```

---

## 4. Method 1: Automated Build (Recommended)

An automated script `build_and_deploy.sh` is provided in the repository root. This script handles the entire build process, including:
1. Installing dependencies via Homebrew.
2. Clones the recommended version of `libagar` (version 1.7.1).
3. Applies macOS-specific patches to `libagar` to fix Cocoa window crashes and library paths.
4. Compiles and installs `libagar` to `/usr/local`.
5. Compiles the main OVCC emulator and all peripheral modules (e.g. Becker, HardDisk, SuperIDE, MPI).
6. Packages all binaries, modules, and library dependencies into a portable `ovcc.app` bundle.

### Running the Script

From the repository root, execute:
```bash
chmod +x build_and_deploy.sh
./build_and_deploy.sh
```

> [!IMPORTANT]
> The script installs `libagar` to `/usr/local/lib` and adjusts the library load paths. Because of this, it will prompt you for your administrator (`sudo`) password during execution.

Once finished, the compiled application bundle will be located at:
`./ovcc.app`

---

## 5. Method 2: Manual Build (Step-by-Step)

If you prefer to compile the dependencies and emulator manually, follow these steps:

### Step 5.1: Build and Patch AGAR 1.7.1

1. **Clone the official AGAR repository** and checkout the tested commit:
   ```bash
   git clone https://github.com/JulNadeauCA/libagar.git libagar-src
   cd libagar-src
   git checkout 11d8355d00a4f8c4cb05bec6496efd55fb121696
   ```

2. **Apply the macOS compatibility patches** located in the OVCC repository:
   ```bash
   git apply /path/to/OVCC/Patches/AGAR/0001-Fix-compile-on-latest-MacOS.patch
   git apply /path/to/OVCC/Patches/AGAR/0002-Hack-to-prevent-crashing-on-latest-MacOS.patch
   git apply /path/to/OVCC/Patches/AGAR/0003-Add-script-fix-dylibs.sh-for-MacOS.patch
   ```

3. **Apply the `AG_MouseButtonUpdate` inline fix**:
   In recent versions of `libagar 1.7.1`, `AG_MouseButtonUpdate` is not defined in Cocoa driver files. Open `gui/drv_cocoa.m` and make the following replacements:
   * Replace:
     `AG_MouseButtonUpdate(drv->mouse, AG_BUTTON_PRESSED, btn);`
     with:
     `drv->mouse->btnState |= AG_MOUSE_BUTTON(btn);`
   * Replace:
     `AG_MouseButtonUpdate(drv->mouse, AG_BUTTON_RELEASED, btn);`
     with:
     `drv->mouse->btnState &= ~AG_MOUSE_BUTTON(btn);`

4. **Configure, Compile, and Install**:
   ```bash
   env CFLAGS="-I/opt/homebrew/include" ./configure --with-sdl2 --without-sdl
   make depend all
   sudo make install
   ```

5. **Fix the installed dylib paths**:
   ```bash
   chmod +x fix-dylibs.sh
   sudo ./fix-dylibs.sh
   ```
   *Note: This script changes the install IDs of the `libag_*.dylib` libraries from relative names to their absolute paths in `/usr/local/lib/`.*

### Step 5.2: Build OVCC

1. Return to the OVCC repository root directory.
2. Clean any previous builds:
   ```bash
   make clean
   ```
3. Compile the emulator and its peripheral modules:
   ```bash
   make
   ```

#### Step 5.3: Package into `ovcc.app`

1. Create the bundle folders:
   ```bash
   mkdir -p ovcc.app/Contents/MacOS
   mkdir -p ovcc.app/Contents/Frameworks
   mkdir -p ovcc.app/Contents/PlugIns
   ```
2. Run `make install` to copy the binaries and modules, and resolve dependencies:
   ```bash
   make install
   ```
   *Note: `make install` must run **after** a full `make` completes. The `install` target in the Makefiles uses `dylibbundler` to search for all shared library dependencies under `/usr/local/lib`, `/opt/homebrew/lib`, and the project-local `libagar-install/lib` directory. It copies them into `ovcc.app/Contents/Frameworks/` and rewrites the load commands to use relative paths (`@executable_path/../Frameworks/` and `@loader_path/../Frameworks/`), removing the need for launcher wrapper scripts.*

---

## 6. Running the Emulator

1. Place your ROM files (`coco3.rom` and `disk11.rom`) in `~/Library/Application Support/OVCC/` (recommended), or next to the `ovcc.app` folder (see [Section 3](#3-roms-and-directory-structure)).
2. Double-click `ovcc.app` in Finder, or launch it from the terminal:
   ```bash
   open ovcc.app
   ```
3. The emulator will automatically:
   * Search for your ROMs (e.g. `coco3.rom` and `disk11.rom`) in `~/Library/Application Support/OVCC/`, then the bundle's `Contents/PlugIns/`, then next to the `ovcc.app` bundle, then the executable folder.
   * Initialize configuration settings and logs in `~/Library/Application Support/OVCC/` so they persist cleanly.
4. Once open, you can configure the ROMs and libraries through the OVCC GUI options menu.

---

## 7. Troubleshooting & macOS-Specific Tips

### Keyboard Input Doesn't Work
If the window responds to clicks but you cannot type, check how the emulator was started. If you ran `./ovcc` directly from `CoCo/ovcc` or `ovcc.app/Contents/MacOS/ovcc`, keyboard I/O remains bound to the terminal window. **Always run the emulator via the `ovcc.app` bundle (e.g. `open ovcc.app` or double-clicking in Finder).**

### Gatekeeper Blocked App Execution
If you transfer the compiled `ovcc.app` to another Mac, macOS Gatekeeper may block it because it is not signed with an Apple Developer Certificate.
* **To bypass**: Right-click (or Control-click) `ovcc.app` in Finder, select **Open**, and click **Open** in the confirmation dialog.
* **CLI bypass**: Alternatively, run this command in terminal to strip the Gatekeeper quarantine flag:
  ```bash
  xattr -dr com.apple.quarantine ovcc.app
  ```

### Homebrew Path Incompatibilities (Intel vs. Apple Silicon)
The Makefiles search for Homebrew headers and libraries. 
* On Apple Silicon Macs, Homebrew installs to `/opt/homebrew`.
* On Intel Macs, Homebrew installs to `/usr/local`.

The compilation flags automatically query the correct prefix via `brew --prefix`:
`-I$(BREW_PREFIX)/include` and `-L$(BREW_PREFIX)/lib` (via `Makefile.common`). If you are using custom directory structures, ensure your `pkg-config` environment points to the correct Homebrew paths:
```bash
export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:$PKG_CONFIG_PATH"
```
