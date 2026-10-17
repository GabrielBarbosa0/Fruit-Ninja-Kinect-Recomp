# Recompilation roadmap

## Architecture

```text
Owned LIVE package
       |
       v
STFS extraction --> default.xex + assets
       |                  |
       |                  +--> XenonRecomp --> native PowerPC translation
       |                  +--> XenosRecomp --> translated shaders
       |                  +--> REA/Ghidra --> symbols, call graph, NUI behavior
       v
Native runtime (kernel/XAM, graphics, audio, filesystem)
       ^
       |
Webcam --> pose tracker --> Kinect/NUI compatibility layer
             |             skeleton + color + synthesized depth
             +--> calibration, smoothing, player segmentation
```

Xenia Canary is useful as a behavioral reference and for logs, filesystem
access, and non-Kinect system calls. It cannot be the final input solution
because current Xenia builds do not implement Kinect.

## Milestones

1. **Inventory and reproducibility**
   - Record hashes and package metadata without committing copyrighted files.
   - Extract the STFS container and classify executable, shader, audio, model,
     texture, menu, and configuration formats.
   - Capture a baseline Xenia log to enumerate unsupported imports and the
     earliest boot failure.

2. **Static analysis**
   - Build XenonAnalyse/XenonRecomp with Clang 18+ and CMake 3.20+.
   - Patch/decompress the XEX and generate the initial jump-table metadata.
   - Load the patched executable into a PowerPC-aware analyzer.
   - Identify NUI initialization, skeleton polling, coordinate transforms,
     calibration state, player selection, and slash generation.

3. **Minimal native runtime**
   - Start from the shared Xenon/ReXGlue-style runtime architecture.
   - Implement only the kernel, XAM, filesystem, threading, timing, and memory
     surfaces reached during boot.
   - Reach title screen first with a null Kinect provider.

4. **Graphics and media**
   - Translate Xenos shaders and implement the D3D9/Xenos behavior used by this
     title on D3D12 or Vulkan.
   - Map XAudio/XACT banks to a native audio backend.
   - Add WMV playback or a compatible replacement path.

5. **Webcam Kinect provider**
   - [Implemented] Enumerate and capture webcam frames through Windows Media
     Foundation without blocking the game thread.
   - [Implemented] Infer 17 body landmarks from live frames with MoveNet
     SinglePose Lightning through a replaceable ONNX backend.
   - [Implemented] Convert detector-neutral and MediaPipe landmarks into the
     20 Kinect v1 joints and tracking states.
   - [Implemented] Synthesize a packed player-index/depth silhouette for the
     shadow renderer's initial fallback.
   - [Implemented] Apply Kinect-style jitter, correction, prediction, and
     maximum-deviation smoothing.
   - [Implemented] Calibrate player center, body scale, reference depth, and
     floor into a persistent metric game-space profile.
   - [Implemented] Marshal skeleton/image metadata and packed depth pixels into
     the recovered big-endian title ABI, publishing only the newest frame.
   - Add loss/reacquisition, neural player segmentation, and two-player
     separation.

6. **Validation**
   - Compare menus, timing, collision paths, scoring, rendering, and audio
     against recordings from original hardware.
   - Add deterministic tests for coordinate conversion and slash collision.
   - Package no copyrighted assets; require a local owned dump at install time.

## First technical checkpoint

The next concrete target is a report containing:

- XEX image base, entry point, sections, and imported modules/functions;
- the first unsupported Xenia call;
- addresses of the NUI initialization and skeleton update paths;
- a generated XenonRecomp configuration and switch-table file.
