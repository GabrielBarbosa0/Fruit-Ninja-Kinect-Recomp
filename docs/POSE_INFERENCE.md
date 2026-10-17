# Live pose inference

The native pose backend uses MoveNet SinglePose Lightning through ONNX Runtime.
MoveNet accepts a 192x192 RGB tensor and estimates one person's 17 COCO body
keypoints. The model is fast and small enough to keep pose latency below the
camera frame interval on typical desktop CPUs.

## Pipeline

```text
Media Foundation BGRA frame
        |
        v
aspect-preserving 192x192 RGB letterbox
        |
        v
MoveNet / ONNX Runtime CPU inference
        |
        v
17 normalized COCO keypoints + confidence
        |
        v
MoveNetAdapter -> detector-neutral Kinect 20-joint PoseFrame
```

MoveNet has no hand, foot, spine, or torso-center keypoints. The adapter uses
wrists as hand fallbacks, ankles as foot fallbacks, calculates shoulder and hip
centers, and places the spine halfway between those centers. It does not invent
per-joint depth; metric depth remains the responsibility of `SkeletonMapper`.

## Setup and validation

The setup script downloads pinned, SHA-256-verified artifacts into the ignored
`tools/pose` directory:

```powershell
.\scripts\Setup-Pose.ps1
.\scripts\Build-Native.ps1 -Configuration Release
```

Run inference over 60 distinct frames and save the newest annotated frame:

```powershell
.\build\native-release\fnk_pose_test.exe --device 0 --frames 60 `
    --threshold 0.35 --output .\artifacts\pose\latest.ppm
```

The threshold controls visualization only; every landmark retains its raw
model confidence for Kinect tracking-state decisions. Values around 0.35 are a
useful starting point. Lower values display more joints but can draw false
limbs on furniture when no person is visible.

## Current measured result

On the development machine, the physical `ACER HD User Facing` camera supplied
640x480 frames and the CPU backend averaged about 5.7 ms per MoveNet inference,
or roughly 176 inference frames per second. Camera delivery, not inference, was
the limiting rate.

## Limitations and next work

- SinglePose follows only one person.
- Detection quality requires most of the body to be inside the frame.
- The current preprocessor uses nearest-neighbor resizing; bilinear or GPU
  preprocessing can improve quality later without changing the API.
- Game-space center, scale, depth, and floor calibration is implemented; see
  `GAME_SPACE_CALIBRATION.md`.
- A presence gate and reacquisition state machine are still needed to suppress
  false positives when nobody is in view.
- MoveNet does not estimate depth or detailed hands. The compatibility layer
  currently derives depth from apparent torso size and uses wrist positions
  for slashing.
