# Fruit Ninja Kinect Recomp

Research project for a native PC recompilation of the Xbox 360 edition of
Fruit Ninja Kinect. The intended input device is a regular computer camera;
pose estimation will provide the skeleton, color, and synthesized depth data
expected by the original game.

## Legal scope

This repository contains only original compatibility code, documentation, and
build scripts. It does not contain the game executable, assets, Xbox SDK files,
keys, or firmware. Each user must provide files dumped from a copy they own.
The `files/`, `artifacts/`, and `tools/` directories are intentionally ignored
by Git.

## Current status

- The original `LIVE` STFS package is detected as Fruit Ninja Kinect. Its
  content directory is `584109A6`, while the XEX itself reports Title ID
  `58410B79`.
- Its 400 entries can be extracted locally, including `default.xex`, assets,
  shaders, XAudio banks, and configuration data.
- The input configuration explicitly requests `X360_Kinect_Skeleton`,
  `X360_Kinect_Depth`, and `X360_Kinect_Color`.
- REA, XenonRecomp, XenosRecomp, a shared Xenon runtime reference, and Xenia
  Canary are kept as local tools under `tools/`.
- A tested native NUI core now maps detector-neutral poses to Kinect v1's 20
  joints, adapts MediaPipe's 33 landmarks, applies temporal smoothing, and
  synthesizes a packed 320x240 depth/player-index silhouette.
- The exact big-endian NUI layouts, function signatures, and stream parameters
  used by the title have been recovered and encoded as compile-time-checked types.
- Native overrides now dispatch skeleton and depth/player frames through the
  recovered calls and synthetic `LockRect` textures into Xenon guest memory.
- The modern `tools/Xenon-Recomp` path now has an `AddressSpace` adapter and a
  CPU V2 compiled-registry overlay for all eight recovered NUI/texture entry
  points. Unrelated compiled functions and real textures still fall through to
  the generated game registry.
- The owned XEX can now be materialized into the uncompressed form accepted by
  the modern loader, analyzed into 17,000 native functions/133 C++ shards, and
  built as a 64-bit `xenon_game_module` DLL with the NUI overlay injected.
- A Windows Media Foundation backend now enumerates real cameras and captures
  BGRA8 frames on a worker thread. It has been exercised with a physical USB
  webcam at 640x480.
- A native ONNX Runtime backend now runs MoveNet SinglePose Lightning on live
  camera frames and maps its 17 COCO keypoints into Kinect v1's 20-joint pose.
- Automatic game-space calibration now derives the player center, metric body
  scale, reference depth, and floor line, with a torso-only fallback for
  cropped phone-camera framing.
- Loading and executing the complete recompiled title is not yet implemented.

See [docs/NUI_ABI.md](docs/NUI_ABI.md) for the recovered guest ABI,
[docs/ROADMAP.md](docs/ROADMAP.md) for the architecture and milestones,
[docs/INITIAL_ANALYSIS.md](docs/INITIAL_ANALYSIS.md) for the first XEX report,
and [docs/TOOLING.md](docs/TOOLING.md) for pinned local tool versions.

## Extract a local dump

After cloning `jasonulbright/Extract-STFS` to `tools/Extract-STFS`, run:

```powershell
.\scripts\Extract-Game.ps1 -PackagePath "C:\path\to\your\owned\package"
```

The extracted files are written to `artifacts/extracted/fruit-ninja-kinect`
and remain untracked.

## Build the native compatibility layer

With the portable toolchain described in `docs/TOOLING.md` present:

```powershell
.\scripts\Build-Native.ps1
```

This builds `fnk_nui` and runs the deterministic NUI tests. Use
`-Configuration Release` for an optimized build.

To validate the compatibility layer against the modern Xenon guest-memory and
CPU V2 contracts, configure the optional bridge and run its probe:

```powershell
cmake -S . -B build/native-xenon -G Ninja `
    -DFNK_BUILD_CAMERA=OFF -DFNK_BUILD_POSE=OFF `
    -DFNK_BUILD_XENON_RUNTIME_BRIDGE=ON
cmake --build build/native-xenon --target fnk_xenon_memory_probe
.\build\native-xenon\fnk_xenon_memory_probe.exe
```

## Generate the modern recompiled module

With the extracted owned `default.xex` in its normal artifact directory, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\Generate-ModernModule.ps1 `
    -Jobs 8 -BuildModule
```

The script applies the tracked Xenon-Recomp compatibility patches, creates an
ignored `default.uncompressed.xex` without modifying the original dump,
generates the partial AOT project, injects the webcam-to-NUI registry overlay,
and builds `artifacts/generated-modern/build/libxenon_game_module.dll`.
Unresolved indirect sites remain available to Xenon's runtime fallback; the
full host/VFS boot path is still the next integration milestone.

### First runtime boot

Build the ABI-compatible Xenon host and run a bounded headless boot probe:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\Build-RuntimeHost.ps1 -Jobs 8
powershell -ExecutionPolicy Bypass -File .\scripts\Start-FruitNinja.ps1 -ObserveSeconds 15
```

The launcher passes `default.uncompressed.xex` as a loose executable, mounts
the surrounding extracted directory as `game:/`, binds
`libxenon_game_module.dll`, and starts the recovered entry point. Each run
writes inspectable `status.json`, `log.txt`, and diagnostic reports beneath
`artifacts/runtime-sessions/`. Use `-KeepRunning` only after a boot survives
the observation window; the default sends `stop.signal` so probes do not
leave a background runtime behind.

## Test a real webcam

The Windows build also produces `fnk_camera_test.exe`. Keep the portable
LLVM-MinGW `bin` directory on `PATH`, then enumerate cameras and capture a
diagnostic frame:

```powershell
$clang = Get-ChildItem .\tools\toolchain\llvm-mingw -Filter clang++.exe -File -Recurse |
    Select-Object -First 1
$env:Path = "$($clang.Directory.FullName);$env:Path"

.\build\native-debug\fnk_camera_test.exe --list
.\build\native-debug\fnk_camera_test.exe --device 0 --width 640 --height 480 `
    --fps 30 --frames 60 --output .\artifacts\camera\latest.ppm
```

The camera runs only for the duration of the command. Diagnostic images stay
under the ignored `artifacts/` directory. See
[docs/CAMERA_CAPTURE.md](docs/CAMERA_CAPTURE.md) for the API contract and the
next integration step.

## Run live pose inference

Install the pinned ONNX Runtime and MoveNet model, rebuild, and run the pose
diagnostic against a camera index reported by `fnk_camera_test --list`:

```powershell
.\scripts\Setup-Pose.ps1
.\scripts\Build-Native.ps1 -Configuration Release

.\build\native-release\fnk_pose_test.exe --device 0 --frames 60 `
    --threshold 0.35 --output .\artifacts\pose\latest.ppm
```

The output is a camera frame with the accepted skeleton segments drawn in
green. See [docs/POSE_INFERENCE.md](docs/POSE_INFERENCE.md) for model details,
performance notes, and limitations.

## Calibrate the play space with DroidCam

Place the phone far enough away to show the player facing the camera. Full-body
framing produces the best floor estimate, but torso-only calibration is also
supported:

```powershell
.\build\native-release\fnk_calibration_test.exe --device 0 --samples 30 `
    --confidence 0.30 --output .\artifacts\calibration\droidcam.toml
```

The ignored profile can be loaded by `SkeletonMapper` to express joints in
metres relative to the calibrated horizontal center and floor. See
[docs/GAME_SPACE_CALIBRATION.md](docs/GAME_SPACE_CALIBRATION.md).
