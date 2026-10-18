# Fruit Ninja Kinect — ReXGlue experiment

This experiment recompiles `default.uncompressed.xex` into a native Windows
executable backed by the ReXGlue runtime and its Xenos/Vulkan GPU plugin.

## Current state

- The native executable initializes input, audio and Vulkan.
- The title XEX and 21,000+ recompiled PowerPC functions are registered.
- Relative title paths are rooted at `game:\`, allowing the original assets to
  load from `artifacts/extracted/fruit-ninja-kinect`.
- The runtime-discovered entry points through `0x8240A6E0` are included; the
  latest 30-second probes present roughly 970 frames without a fatal error.
- A native ReXGlue NUI bridge overrides the six statically linked Kinect entry
  points and the two synthetic depth-texture methods used by this title.
- A temporary tracked skeleton is published at 30 Hz until the DroidCam pose
  provider is connected to the same frame store.
- The complete initial menu has been visually verified: Fruit Ninja Kinect
  logo, Sensei, `SLICE FRUIT TO BEGIN`, the `BEGIN` watermelon, and Kinect
  Tuner are rendered correctly.
- ReXGlue's generated translation units use stable guest-address buckets so a
  newly discovered function does not renumber every generated source file.

The initial-menu milestone is complete. The next milestone is interactive menu
control using the existing webcam/DroidCam capture, pose inference and game-space
calibration pipeline instead of the temporary skeleton publisher.

## Build and probe

From the repository root:

```powershell
& .\experiments\rexglue\scripts\build-and-probe.ps1
```

The script regenerates code, builds the native executable, runs it in a hidden
window for 20 seconds, stops only that process if it is still alive, and reports
the newest fatal guest address and log path. Use `-ProbeSeconds` to change the
bounded run duration.

When the only config change is a runtime-discovered indirect function hint, use
`-IncrementalRuntimeHint`. This preserves the shared declaration header's
timestamp and rebuilds only the affected address bucket and registration data:

```powershell
& .\experiments\rexglue\scripts\build-and-probe.ps1 -IncrementalRuntimeHint -ProbeSeconds 30
```

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
