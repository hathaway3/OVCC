# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

OVCC — the Open Virtual Color Computer, a portable Tandy Color Computer 3 emulator derived from VCC 1.43. Plain C, GPL. Runs on macOS (Darwin), Linux, and Windows (Mingw). GUI/windowing is built on the AGAR toolkit; audio, joystick, and dynamic library loading use SDL2.

## Build commands

Dependencies: AGAR 1.7.1 (`agar-config` must be on PATH) and SDL2 (`pkg-config sdl2`). On macOS install via Homebrew; libagar must be built from source with the patches in `Patches/AGAR`.

```bash
make                 # build ovcc + all peripheral modules (OS auto-detected via uname -s)
make clean
make install         # on macOS, packages into ovcc.app bundle via dylibbundler
./build_and_deploy.sh   # macOS only: full automation — brew deps, patched libagar, build, app bundle (prompts for sudo)
```

- The top-level `Makefile` recurses into each module directory with `make -C <dir> -f Makefiles/$(TARGETOS)/makefile`, where `TARGETOS` is `Darwin`, `Linux`, or `Mingw`.
- **Build a single module**: `make -C HardDisk -f Makefiles/Darwin/makefile` (substitute module and OS).
- Every per-OS makefile includes the top-level `Makefile.common`, which holds all shared rules: compiler flags (`-Ofast -march=native -flto`, warnings suppressed with `-w` on non-Darwin), dynamic versioning from `git describe` written to `git_version.txt`, and install paths. Makefile dependency lists are maintained **by hand** in each `Makefiles/<OS>/makefile` — when you add a source file or change header includes, update those lists or you'll get stale builds.
- `make install` on macOS runs `dylibbundler` to copy shared library dependencies into `ovcc.app/Contents/Frameworks/` and rewrite load commands. Must run after a full `make`. Touches `ovcc.app` afterward so Finder/LaunchServices refreshes cached metadata.
- The `mpu` module builds only on Linux and Darwin, not Mingw.

### Isolated CPU build (Linux)

After a successful full `make`:

```bash
cd CoCo
rm obj/coco3.o obj/vcc.o obj/vccgui.o
make -f Makefiles/Linux/makefile-isocpu   # produces ovcc-isocpu
```

### Tests

There is no unit test suite. The only test target is a per-module smoke test that loads the built shared library and resolves its `ModuleName` export via `testlib` (built from `testlib.c`):

```bash
make -C HardDisk -f Makefiles/Darwin/makefile test
```

The `ovcc` executable itself has no test target.

### Running after build

macOS builds **must** be launched from `ovcc.app` (double-click or `open ovcc.app`) — running the `ovcc` binary directly from a terminal leaves keyboard focus with the terminal. The bundle launcher auto-symlinks roms/dsks/vhds placed next to the `.app`. See `README_MAC.md` for ROM placement and troubleshooting.

## Architecture

### Core emulator (`CoCo/` → the `ovcc` executable)

- `vcc.c` is the entry point. It spawns two AGAR threads: `EmuLoop` (frame/UI pacing) and `CPUloop`. `throttle.c` handles frame timing. On first run, `RunStartupWizard()` (`wizard.c`) is called before booting to let the user pick CPU/RAM/peripherals via a tabbed AGAR dialog — also reachable from Configuration → "Setup Wizard..." at any time.
- Two CPU cores: `mc6809.c` (Motorola 6809) and `hd6309.c` (Hitachi 6309); the active core is selected at runtime through function pointers.
- `tcc1014*` files emulate the GIME chip: `tcc1014mmu_*.c` (memory management/address translation — `_mm`, `_nomm`, and `_common` variants), `tcc1014registers.c`, and `tcc1014graphicsAGAR.c` (video rendering). `mc6821.c` is the PIA (keyboard/cassette/interrupts). `iobus.c` routes port I/O.
- All memory access goes through function pointers declared in `tcc1014mmu.h` (`MemRead8`/`MemWrite8` etc.) — this is the system bus every component and plugin uses.
- Platform-facing code is suffixed by backend: `*AGAR.c` (GUI, keyboard, graphics), `*SDL.c` (audio, joystick). `config.c` + `iniman.c` persist settings to `Vcc.ini` via `fileops.c`'s `ResolvePlatformPath()` (searches `~/Library/Application Support/OVCC/` → bundle `Contents/PlugIns/` → bundle-adjacent → executable dir on macOS).

### Plugin ("Pak") system — the key big-picture concept

Peripherals are **not** linked into `ovcc`. Each top-level module directory (`becker`, `FD502`, `HardDisk`, `mpi`, `mpu`, `orch90`, `Ramdisk`, `SuperIDE`, `testdev`) builds a shared library (`.so`/`.dylib`/`.dll`) that emulates a plug-in cartridge. `CoCo/pakinterface.c` loads them at runtime with `SDL_LoadObject` and resolves a C ABI by name:

- `ModuleName`, `ModuleConfig`, `ModuleStatus`, `ModuleReset`
- `PakMemRead8` / `PakMemWrite8` (cartridge ROM/RAM space), `PackPortRead` / `PackPortWrite` (I/O ports), `PakAudioSample`, `HeartBeat`
- `MemPointers` / `AssertInterupt` — the host passes callbacks into the MMU and interrupt system so plugins can read/write system memory

`mpi` (Multi-Pak Interface) is itself a pak that loads up to four slave paks and dispatches port/memory access by chip-select slot — so the same ABI nests one level deep. When changing the plugin ABI, check `pakinterface.c`, `mpi.c`, and every module together.

Module roles: `FD502` floppy controller (WD1793), `HardDisk` VHD images, `becker` DriveWire/Becker port networking, `orch90` Orchestra-90 sound, `SuperIDE` IDE/CF interface, `Ramdisk` memory board, `mpu` a math/graphics coprocessor (GPU/FPU/DMA), `testdev` a minimal example device.

### Runtime layout

`ovcc` expects `coco3.rom` and `disk11.rom` beside the executable, plugin libraries in `libs/`, optional roms in `roms/`, and generates `Vcc.ini` on first run. On macOS the emulator **must** run from the `ovcc.app` bundle (see README_MAC.md for the path resolution search order and relocatable `/Applications` install).

## Backlog and issue tracking

`BACKLOG.md` is the one-page overview of known issues and planned work, organized by priority (P1–P5). Each P1–P4 item and the three open P5 items are filed as GitHub issues [#1](https://github.com/hathaway3/OVCC/issues/1)–[#21](https://github.com/hathaway3/OVCC/issues/21). The issues are the working tracker; the backlog file is the high-level summary. Key open items include CI caching (§2.3), universal binary (§1.1), code signing (§1.2), keyboard layout gaps (§3.1), HiDPI/Retina verification (§3.4), and WD1793/SuperIDE feature gaps (P5, filed as issues #19–#21).

## Specialist agents

`.claude/agents/` defines subagents for focused areas: `build-master` (Makefile system), `cpu-logic` (6809/6309 cores + MMU), `emulator-core` (bus/MMIO), `hardware-subsystem` (mpu/orch90/FD502), `storage-manager` (HardDisk/Ramdisk). Delegate to them for work in their domain.