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

To resolve this, OVCC must be run from a standard macOS **Application Bundle** (`ovcc.app`). The bundle utilizes a startup launcher script (`ovcc.app/Contents/MacOS/Ovcc`) that sets up the environment and runs the binary under a GUI context.

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

OVCC requires Color Computer 3 ROM files to boot. Because macOS application bundles are read-only when relocated or code-signed, the `ovcc` launcher script features a convenient auto-linking mechanism.

If you place your ROMs and media directories **directly next to the `ovcc.app` bundle**, the launcher script will automatically symlink them into the bundle at startup.

### Recommended Directory Structure

Create a folder for your emulator setup as follows:

```
Emulator-Folder/
├── ovcc.app/               # The compiled application bundle
├── coco3.rom               # (Required) CoCo 3 ROM image (32KB)
├── disk11.rom              # (Required) Disk Controller ROM image (8KB)
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

### Step 5.3: Package into `ovcc.app`

1. Create the bundle folders:
   ```bash
   mkdir -p ovcc.app/Contents/libs
   mkdir -p ovcc.app/Contents/modules
   ```
2. Run `make install` to copy the binaries and modules, and resolve dependencies:
   ```bash
   make install
   ```
   *Note: The `install` target in the Makefiles uses `dylibbundler` to search for all shared library dependencies under `/usr/local/lib` and `/opt/homebrew/lib`, copy them into `ovcc.app/Contents/libs`, and rewrite the binary load commands to use relative paths (`@executable_path/../libs/`).*

---

## 6. Running the Emulator

1. Place your ROM files (`coco3.rom` and `disk11.rom`) next to the `ovcc.app` folder (see directory layout in [Section 3](#3-roms-and-directory-structure)).
2. Double-click `ovcc.app` in Finder, or launch it from the terminal:
   ```bash
   open ovcc.app
   ```
3. The launcher script will automatically:
   * Setup links to your ROMs and directories.
   * Start `ovcc` with the correct dynamic library paths.
4. Once open, you can configure the ROMs and libraries through the OVCC GUI options menu.

---

## 7. Troubleshooting & macOS-Specific Tips

### Keyboard Input Doesn't Work
If the window responds to clicks but you cannot type, check how the emulator was started. If you ran `./ovcc` directly from `CoCo/ovcc` or `ovcc.app/Contents/ovcc`, keyboard I/O remains bound to the terminal window. **Always run the emulator via the `ovcc.app` bundle (e.g. `open ovcc.app` or double-clicking in Finder).**

### Gatekeeper Blocked App Execution
If you transfer the compiled `ovcc.app` to another Mac, macOS Gatekeeper may block it because it is not signed with an Apple Developer Certificate.
* **To bypass**: Right-click (or Control-click) `ovcc.app` in Finder, select **Open**, and click **Open** in the confirmation dialog.

### Homebrew Path Incompatibilities (Intel vs. Apple Silicon)
The Makefiles search for Homebrew headers and libraries. 
* On Apple Silicon Macs, Homebrew installs to `/opt/homebrew`.
* On Intel Macs, Homebrew installs to `/usr/local`.

The compilation flags automatically include:
`-I/opt/homebrew/include` and `-L/opt/homebrew/lib` (via `Makefile.common`). If you are using custom directory structures, ensure your `pkgconf` environment points to the correct Homebrew paths:
```bash
export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:$PKG_CONFIG_PATH"
```
