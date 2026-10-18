# Fruit Ninja Kinect — ReXGlue experiment

This experiment recompiles `default.uncompressed.xex` into a native Windows
executable backed by the ReXGlue runtime and its Xenos/Vulkan GPU plugin.

## Current state

- The native executable initializes input, audio and Vulkan.
- The title XEX and 21,000+ recompiled PowerPC functions are registered.
- Relative title paths are rooted at `game:\`, allowing the original assets to
  load from `artifacts/extracted/fruit-ninja-kinect`.
- The latest controlled run presented 422 frames and stayed alive for the full
  20-second probe. A game thread then reported the next undiscovered function,
  `0x8240A700`, which is already recorded in the config for the next codegen.
- ReXGlue's generated translation units use stable guest-address buckets so a
  newly discovered function does not renumber every generated source file.

The initial menu is not considered reached until it is visually verified. The
current milestone proves that the native title is executing its render loop and
loading real game assets, but more missed function entry points may still need
to be declared.

## Build and probe

From the repository root:

```powershell
& .\experiments\rexglue\scripts\build-and-probe.ps1
```

The script regenerates code, builds the native executable, runs it in a hidden
window for 20 seconds, stops only that process if it is still alive, and reports
the newest fatal guest address and log path. Use `-ProbeSeconds` to change the
bounded run duration.

## Local SDK changes

The source checkout at `tools/rexglue-sdk` currently contains MinGW portability
fixes, Windows runtime DLL staging, import lookup bridging, game-relative VFS
resolution, unchanged-file codegen writes, and stable address-bucket output.
That directory is intentionally ignored by the parent repository. The exact
local changes are captured in `patches/rexglue-sdk.patch`, with the nested
FFmpeg adjustment in `patches/ffmpeg-mingw.patch`.

Apply them from the repository root after cloning ReXGlue v0.9.0 into
`tools/rexglue-sdk`:

```powershell
git -C tools/rexglue-sdk apply ..\..\experiments\rexglue\patches\rexglue-sdk.patch
git -C tools/rexglue-sdk\thirdparty\FFmpeg apply ..\..\..\..\experiments\rexglue\patches\ffmpeg-mingw.patch
```
