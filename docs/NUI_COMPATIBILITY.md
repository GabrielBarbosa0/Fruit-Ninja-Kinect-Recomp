# Webcam-to-NUI compatibility layer

## Implemented core

`fnk_nui` is independent of the camera and pose-estimation libraries. Its
detector-neutral boundary is `PoseFrame`, containing normalized image
coordinates, relative depth, confidence, timestamp, and tracking ID for the 20
Kinect v1 joints.

The current live pipeline is:

```text
Media Foundation webcam frame
        |
        v
MoveNet ONNX -- 17 COCO keypoints
        |
        v
MoveNetAdapter -- synthesizes Kinect torso centers, spine, hands and feet
        |
        v
GameSpaceCalibrator -- center + scale + floor + reference depth
        |
        v
SkeletonMapper -- calibrated metric projection + tracking + smoothing
        |
        +--> 20-joint metric SkeletonFrame
        |
        v
DepthSynthesizer -- capsule silhouette at 320x240
        |
        v
NuiFrameStore -- newest complete frame, no backlog
        |
        v
big-endian NUI_SKELETON_FRAME + 13-bit depth/player texture bytes
```

Depth is estimated from the current shoulder-to-hip length relative to the
calibrated neutral pose. The calibration profile maps the neutral pose to a
configurable reference depth and torso length.

The synthesized player mask deliberately uses capsules around bones. It is a
fallback for title boot and basic shadows, not a substitute for image
segmentation. A live provider should replace it with the pose engine's person
segmentation mask when available.

## Remaining integration boundaries

The project now supports both local recompilation paths:

- Old `tools/XenonRecomp`: `FlatGuestMemory` plus the generated-symbol hooks in
  `xenon_hooks.cpp`.
- Modern `tools/Xenon-Recomp`: `XenonAddressSpaceGuestMemory` plus
  `XenonNuiRegistryOverlay`. Install the overlay immediately after the
  generated module calls `bind_compiled_registry()`; it preserves the original
  lookup callback for every unrelated address and for non-synthetic textures.

The deterministic `fnk_xenon_memory_probe` exercises the modern path through a
real compact `AddressSpace`, including CPU V2 lookup interception, skeleton
marshalling, depth publication, texture locking, and big-endian readback.

Remaining boundaries:

1. Generate the modern game module and make its exported
   `Xenon_BindCompiledRegistry` create a per-thread overlay after binding the
   generated registry.
2. Attach the live webcam/pose producer lifetime to `xenon_runtime_host` so it
   continuously feeds the shared `NuiFrameStore` while guest code executes.
3. Verify the Xbox linear X8R8G8B8 channel order and publish webcam color frames.
4. The NUI identity and database containers must be mounted through the title
   VFS before NUI initialization.
5. Multiple detected people need stable tracking IDs and Kinect player indices
   from 1 through 6; gameplay prioritizes the first two skeletons.
