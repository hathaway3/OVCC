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

## P5 — Plugin (Pak) module code review (added 2026-07-22)

Findings from a full review of the nine pak modules (`becker`, `FD502`, `HardDisk`, `mpi`, `mpu`, `orch90`, `Ramdisk`, `SuperIDE`, `testdev`). All items ❌ Open unless noted. Ordered by severity within each subsection.

### 5.1 Crashes and memory safety

- ✅ **Done — mpi: heap out-of-bounds read on banked ROM carts.** `MountModule` allocated `0x4000` (16K) for a slot ROM, but `PakMemRead8` indexes it with `Address & 32767` (32K mask). Fixed: buffer now allocated as `0x8000`, zero-padded with `0xFF`, loaded via `fread` (`mpi/mpi.c`). This also fixed the related "16K ROM loads drop the last byte" bug below (the old byte-at-a-time `fgetc`/`--index` loop is gone).
- ✅ **Done — becker: NULL deref when DNS resolution fails.** `attemptDWConnection` now returns immediately (instead of falling through) when `gethostbyname` fails or a failed `socket()` call returns `-1`; the socket variable is reset to `0` on failure so downstream `dwSocket==0` checks stay consistent (`becker/becker.c`).
- ✅ **Done — SuperIDE: file I/O on unmounted drive.** `ExecuteCommand` 0x20/0x21/0x30/0x31 now check `hDiskFile[DiskSelect]` for NULL before touching it and abort the command (`Status=ERR|RDY`, `Error=ABRT`) instead of crashing; `DropDisk` now bounds-checks `DiskNumber` and skips `fclose` when the handle is already closed (`SuperIDE/idebus.c`).
- ✅ **Done — Cloud9 RTC (both copies): unsafe use of `localtime()`'s shared static buffer.** `now` was a raw pointer into libc's process-wide `localtime()` buffer, held across multiple emulated port accesses — another pak module's clock (or a later `localtime()` call anywhere in the process) could invalidate it before `SetTime()` consumed it. Fixed in both `HardDisk/cloud9.c` and `SuperIDE/cloud9.c`: the result is now copied by value into a module-owned `static struct tm` immediately, and `SetTime()` has a NULL guard.
- ✅ **Done — mpi: status string buffer overflows.** `ModuleStatus` now uses a 256-byte `TempStatus` (matching the host's `StatusLine[256]`) and bounds every concatenation into `MyStatus` with tracked remaining length instead of unchecked `strcat` (`mpi/mpi.c`).
- ✅ **Done — mpi: `PakSetCart` called without NULL guard** in both `PackPortWrite` port 0x7F and `ModuleReset` — both now check for NULL before calling (`mpi/mpi.c`).
- ❌ **Still open — FD502: RAW ("real disk") path is dead Windows code with live landmines.** `OpenFloppy` fopens an *uninitialized* `szDevice`; `GetDriverVersion` tests uninitialized `h`; `FormatTrack` has no return statement; `ReadSector`'s RAW branch tests uninitialized `dwRet` (`FD502/wd1793.c:1490-1535, 504`). Reachable if `Vcc.ini` persists `*Floppy A:`. Left open because the right fix is a product decision (port to a real backend vs. strip the RAW/fdrawcmd code and the `FDRAWREAD` config UI at `fd502.c:429`), not a mechanical bug fix.
- ✅ **Done — Ramdisk: no guard on unallocated buffer, and a leak on unload.** `WriteArray`/`ReadArray` now check `RamBuffer` for NULL; added `FreeMemBoard()` (`memboard.c`/`.h`) called from a new `ModuleConfig(0)` export and from the library destructor, so the 512K buffer is released on eject/unload instead of leaking per load/unload cycle (`Ramdisk/ramdisk.c`, `Ramdisk/memboard.c`).
- ✅ **Done — UB: `ModuleReset` declared `unsigned char` but returned nothing** in `becker.c`, `fd502.c`, and `harddisk.c` — all three now `return(0)`.

### 5.2 Threading and synchronization

- ✅ **Done — becker: socket ring buffer had no synchronization.** Added `BufferLock` (an `AG_Mutex`) guarding `dwSocket`, `retry`, and the `InBuffer`/`InReadPos`/`InWritePos` ring buffer, shared between the DriveWire TCP thread and the CPU thread (`becker/becker.c`). Also fixed the classic full==empty ambiguity by adding an explicit `InBufferCount`, so a completely full buffer is no longer indistinguishable from an empty one (previously `InReadPos == InWritePos` meant both, silently stalling/dropping data once the buffer filled). `attemptDWConnection` now resolves a new connection into a local variable and only publishes it to the shared `dwSocket` after `connect()` succeeds or fails — previously `dwSocket` was published right after `socket()`, so the CPU thread could `send()`/check status on a socket that wasn't actually connected yet. `killDWTCPThread` now force-closes immediately (instead of `usleep`-and-hope) and `AG_ThreadJoin`s the thread — guarded by a new `threadRunning` flag, since `dw_setaddr`/`dw_setport` can reach `killDWTCPThread` during initial config load before any thread was ever started, and joining a never-created thread handle is UB. `SetDWTCPConnectionEnable` now rolls back `DWTCPEnabled` if thread creation fails (previously a failed start left the module permanently unable to retry). **Residual, accepted gap:** `curaddress`/`curport` (used only for reconnect-on-change detection) are still read/written without the lock — a stale read there just risks one extra/missed reconnect cycle, not a memory-safety issue, so it was left as-is rather than expanding the lock's scope further.
- ✅ **Done — mpu: GPU queue races.** Rewrote `ProcessGPUqueue`'s wait loop to check the "is there work, or are we stopping" predicate and `pthread_cond_wait` under the *same* mutex (`GPUlock`, now properly initialized via `PTHREAD_MUTEX_INITIALIZER` — previously default-zero-initialized, which is not portably valid) — this closes the lost-wakeup window that existed between the old separate `condLock` and the unguarded `QueueList.ListHead` read. `StartGPUQueue` now checks `pthread_create`'s actual return value (was checking `GPUthread == 0`, which is not a valid "did creation fail" test) and resets `queueActive = 1` (previously, a pak eject/reinsert cycle left it permanently `0` from the prior stop, so the new thread would exit immediately). `StopGPUqueue` now `pthread_join`s the thread before returning — it previously returned immediately, and since it runs right before `mpi.c`'s `UnloadModule` calls `SDL_UnloadObject` (`dlclose`), the GPU thread could still be executing code from the pak's `.so` at the moment it was unmapped.
- ✅ **Done — mpu: use-after-free window on `ScreenList`/`TextureList`.** Added `ScreenListLock` (`gpuprimitives.c`) and `TextureListLock` (`gputextures.c`), each guarding every `AppendListItem`/`FindListItem`/`RemovelistItem` call on their respective list — this closes the underlying list-corruption race between `NewScreen`/`NewTexture` (CPU thread) and `DestroyScreen`/`DestroyTexture` (GPU thread only, via the queue) mutating the same linked list concurrently. Additionally, `RenderTexture` no longer resolves `Screen*`/`Texture*` pointers on the CPU thread and queues the raw pointers — it now queues `screenid`/`textureid` and the resolution happens on the GPU thread immediately before use (`ProcessGPUqueue`'s `CMD_RenderTexture` case, via the list locks). This was the more serious half of the original finding: with the old code, a `DestroyScreen`/`DestroyTexture` queued *before* a direct, unqueued `RenderTexture` call could still execute *after* it on the GPU thread (no ordering guarantee between the CPU thread's direct calls and the queue's contents), freeing the object before the queued render used it — a real use-after-free that per-list locking alone would not have fixed, since the stale pointer was already captured before the lock could do anything. Also fixed while here: `struct _linkedlistItem.nextItem` in `linkedlists.h` was declared as the wrong pointer type (`struct _linkedlist *` instead of `struct _linkedlistItem *`), the source of several "incompatible pointer types" warnings in the build log; harmless in practice (all pointer representations are uniform on real targets) but incorrect and now fixed.
- ✅ **Done — mpu: `va_start(ArgumentPointer, 1)` used a literal instead of the last named parameter.** Fixed to `va_start(ArgumentPointer, cmd)` in every case (`mpu/gpu.c`). This surfaced a second, related UB: `cmd`'s declared type was `unsigned char`, which undergoes default argument promotion — a parameter subject to that promotion cannot be used as `va_start`'s named argument either, so `QueueGPUrequest`'s parameter was widened to `unsigned int` (callers already convert safely through the ordinary prototype, no caller changes needed). Also fixed while here: `va_end` was called unconditionally after the switch (UB on the `default:` case, which never called `va_start`) — moved into each case; and the `default:` case leaked the just-malloc'd `QueueRequest` — now freed before returning.

### 5.3 Functional bugs

- ✅ **Done — mpi: `ModuleReset` clears the wrong array element.** `BankedCartOffset[Temp]=0` used the stale global `Temp` instead of the loop variable; fixed to `BankedCartOffset[modidx]=0` (`mpi/mpi.c`).
- ❌ **Still open — mpi: port reads broadcast to all slots.** `PackPortRead` polls every slot until one returns non-zero (`mpi/mpi.c:238`), so reads with side effects (becker's data port 0x42) fire on unselected paks, and a device legitimately returning 0x00 loses arbitration. Real MPI dispatches by `SpareSelectSlot`. `PackPortWrite` broadcasts too. Left open deliberately: this changes I/O dispatch behavior for every existing MPI+multi-pak configuration and needs sign-off before changing, not a drive-by fix.
- ✅ **Done — mpi: 16K ROM loads drop the last byte.** Fixed as part of the §5.1 OOB-read fix — `fread` replaced the byte-at-a-time `fgetc`/`--index` loop, so `ExtRomSizes` is now the exact byte count with no off-by-one (`mpi/mpi.c`).
- ✅ **Done — HardDisk: one filename buffer for two drives.** `HDDfilename` is now `HDDfilename[2][MAX_PATH]`, one slot per drive; `SaveConfig` takes the drive index directly instead of a pre-formatted ini-key string (`HardDisk/harddisk.c`). Careful re-trace of every call site showed this hadn't actually produced wrong output yet with the code's current call patterns (each site sets the filename immediately before using it), but it was one refactor away from silently swapping/clobbering eject labels and persisted `VHDImage0/1` entries — fixed defensively rather than left as a footgun, matching how `FD502` already does this correctly via `Drive[].ImageName`.
- ✅ **Done — HardDisk: `DISK_FLUSH` didn't flush, and short reads/writes were reported as success.** `fflush()` now actually runs on `DISK_FLUSH` (`HardDisk/cc3vhd.c`); `SECTOR_READ`/`SECTOR_WRITE` now check `fread`/`fwrite`'s return value and report `HD_NODSK` (skipping the DMA transfer, for reads) instead of unconditionally reporting `HD_OK`. Also fixed the malformed `%000000.6X` format string to `%06X` in both status messages.
- ✅ **Done — becker: throughput stats formula was wrong.** `ReadSpeed = 8*(bytes / (1000.0f - sinceCalc))` divided by the wrong term; fixed to `8.0f * bytes / sinceCalc` (sinceCalc in ms, so the 1000ms/1000-bits-per-kbit factors cancel and this yields kbps directly) (`becker/becker.c`).
- ✅ **Done — FD502/WD1793: write-protect checked after the write.** Both `WriteBytetoSector` and `WriteBytetoTrack` now check `Drive[CurrentDisk].WriteProtect` *before* calling `WriteSector`/`WriteTrack`, skipping the actual disk write entirely (and reporting `WRITEPROTECT | RECNOTFOUND`) instead of writing first and only setting the status bit afterward (`FD502/wd1793.c`). This also closes the related "`WriteSector` never checks `WriteProtect`" gap — since the caller now gates the call, the callee never runs against a protected image at all.
- ✅ **Done — SuperIDE: no mount-failure handling in `LoadConfig`, and the `Mounted` flag was shared between master and slave.** `LoadConfig` now clears the ini entry for a drive whose saved image file no longer mounts, instead of silently retrying the same failed mount forever (`SuperIDE/superide.c`). The shared `Mounted` flag (set/cleared by *either* drive, so ejecting one could make the other's status wrongly read "No Image!") is removed; `DiskStatus` now checks `hDiskFile[DiskSelect]` for the currently head-register-selected drive instead (`SuperIDE/idebus.c`).
- ✅ **Done — Cloud9 RTC (both copies): 12-hour conversion edge cases.** `TempHour>12` missed noon (read as AM) and never mapped midnight to 12; replaced with the standard `%12` + `>=12` AM/PM split in `ReadTime`'s encode path. Also fixed the symmetric decode bug in `SetTime` (setting the clock via a 12-hour value): the old code added 12 whenever the AM/PM bit was set regardless of the parsed hour, so 12 PM (noon) incorrectly became 24 and 12 AM (midnight) incorrectly stayed 12 instead of becoming 0 — both files (`HardDisk/cloud9.c`, `SuperIDE/cloud9.c`) now special-case 12 in both directions.
- ✅ **Done — FD502: `AG_Strlcpy(TempRomFileName, ..., sizeof(TempFileName))`** — now uses `sizeof(TempRomFileName)`, matching the actual destination buffer (`fd502.c`).

### 5.4 Unimplemented emulation (feature gaps)

- **WD1793 (`FD502/wd1793.c`):**
  - Read Track for JVC/VDK/OS9 images is a stub (`//STUB Write Me`, `wd1793.c:719`).
  - Multi-sector Read/Write (`READSECTORM`/`WRITESECTORM`): `MSectorFlag` is set but never used to continue to the next sector.
  - Type-1/2 flags decoded but unused: `TrackVerify`, `SideCompare`/`SideCompareEnable`, `Delay15ms`, `HeadLoad`; `HaltEnable` (CPU HALT line during transfers) is never asserted.
  - The documented "reading $FF48-$FF4F clears bit 7 of DSKREG" behavior is not implemented.
  - JVC images with 1-byte (or 5+-byte) headers fail to mount — `MountDisk`'s `HeaderSize` switch has no case for them (`wd1793.c:354`).
  - `READADDRESS` returns placeholder sector/CRC values for DMK (`wd1793.c:1107-1113`).
- **Dallas clock (`FD502/distortc.c`):** control registers 0xD/0xE/0xF are read as zero (IRQ/busy/hold, standby, 12/24-mode readback); clock is read-only — register writes other than the format bit are ignored.
- **SuperIDE (`SuperIDE/idebus.c`):** `SecCount` is ignored — every transfer is exactly one 512-byte sector; Format Track (0x50) and most command opcodes are no-ops that still latch `CurrentCommand`; CHS translation is not implemented (LBA-only, comment at `idebus.c:133`).
- **mpu (`mpu/mpu.c`):** `PackPortRead` is commented out, so `ExecuteStatus`/results cannot be polled from the CoCo side; no `ModuleReset` (queue/screens/textures survive an emulator reset) and no `ModuleStatus`. Still open — these are new API surface, not a mechanical fix.
  - ✅ Fixed the adjacent comma-operator bug in the non-queue `CMD_DrawLine` path (`DrawLine(...), Params[4];` silently dropped the 5th argument) — dead under `GPU_MODE_QUEUE` (always defined) but a landmine if that ever changes.
- ✅ **Done — mpu screens/textures (`mpu/gpuprimitives.c`, `gputextures.c`):** `NewScreen`/`NewTexture` now reject any `bpp`/`bitsperpixel` other than 1/2/4/8 (previously e.g. `bpp=3` silently corrupted the pitch/shift math for every later access to that screen/texture) and check both `malloc` calls for NULL. `NewTexture`'s `bitmapsize` (a `ushort` field) is now computed in a wider type and rejected if it would exceed 65535 instead of silently wrapping. `SetScreenPixel`'s bounds check used `pixaddr > ScreenEnd` where `ScreenEnd` is an *exclusive* upper bound, so `pixaddr == ScreenEnd` was a real one-byte overwrite past the screen into whatever CoCo memory follows it — fixed to `>=`. **Left as-is:** `pixaddr`'s 16-bit wraparound arithmetic, since CoCo addresses are natively a 64K space and "fixing" the wraparound isn't obviously more correct than matching real hardware's own address-space wraparound — noted rather than silently changed.
- ✅ **Done (partial) — mpi:** `FileID` now also recognizes the universal/fat Mach-O magic (`0xCAFEBABE`) as a loadable module on macOS, not just thin Mach-O — previously a universal-binary pak (the still-open §1.1 feature) would've been misidentified as a 16K ROM image. Banked-cart bank switching for ROM slots (`BankedCartOffset` unused, `mpi/mpi.c:277`) remains open — that's a missing feature (bank-switched ROM carts), not a mechanical bug.
- **FD502 config UI:** "Physical Disks" combo boxes and `FDRAWREAD` text are Windows-only leftovers, always disabled (`fd502.c:407-430`).

### 5.5 Memory churn and bloat

- **mpu GPU queue: one `malloc`+`free` per primitive.** Every `SetPixel`/`DrawLine`/etc. allocates a ~120-byte `QueueRequest` that the GPU thread frees (`mpu/gpu.c:112,198`); `RenderTexture` additionally mallocs a `Rect` per call, freed on the other thread (`gputextures.c:104`). A pixel-heavy CoCo program generates tens of thousands of allocations per frame. Still open — replacing this with a fixed-size ring buffer or freelist pool is a real (if contained) redesign of the queue's storage, not a drive-by fix.
- ✅ **Done — mpu GPU queue: unbounded depth.** Added `GPU_QUEUE_MAX_DEPTH` (65536 entries) with real backpressure: `QueueGPUrequest` now blocks the CPU thread on a new `GPUnotFullCond` once the queue reaches that depth, and `ProcessGPUqueue` signals it after every dequeue (`mpu/gpu.c`). Bounds worst-case memory growth if the consumer ever falls behind or stalls, without capping normal-depth operation.
- **FD502 DMK I/O churn:** every sector read/write re-reads the full track from disk (`wd1793.c:472, 552, 1210`), and DMK `WriteTrack` mallocs/frees a track buffer per call (`wd1793.c:643`). A one-track cache (the `DirtyDisk` flag already exists, but is only used by the RAW path) would remove most of this.
- ✅ **Done — Ramdisk:** 512K `RamBuffer` no longer leaks on unload; `ModuleConfig(0)` and the library destructor now both call `FreeMemBoard()` (see §5.1).
- 🟡 **Partial — ROM loaders read byte-at-a-time with `fgetc`.** Fixed in mpi (`mpi/mpi.c`, part of the §5.1 fix, using `fread` now). Still open in becker/FD502/HardDisk/orch90 — same pattern, lower severity there since none of those buffers are read with a wider mask than they're loaded with.
- Static footprints are otherwise small and fixed (16K transfer buffers, 8-16K ROM shadows); no other bloat concerns found.

---

## Resolved along the way (not originally in the backlog)

These macOS bugs were found and fixed while completing the P1/P2 work:

- **Static-linked AGAR broke plugin menus.** libagar was linked statically into both `ovcc` and every pak, so each had its own copy of AGAR's global state and plugins couldn't add to the host menus. Fixed by building libagar as a shared library (patch `0005` + `--enable-shared`) so there is one shared AGAR instance.
- **No plugin could load** (`Cartridge → Load Cart` did nothing). `InsertModule` prepended `./` to absolute paths, mangling them so `SDL_LoadObject` failed. Fixed in `CoCo/pakinterface.c`.
- **MPI config dialog corrupted on slot switch.** The polled status label resized without a layout pass and overflowed the controls below it; fixed by re-running `AG_WindowUpdate` on slot change (`mpi/mpi.c`).
- **Launcher name collision.** The `Ovcc` launcher collided case-insensitively with the `ovcc` binary; renamed to `ovcc-launch`.
