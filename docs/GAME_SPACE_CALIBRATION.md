# Game-space calibration

The calibration stage converts normalized camera landmarks into a stable
metric space suitable for Kinect gameplay. The player faces the camera and
holds a neutral pose while the system collects a short window of samples.
Medians make the result resistant to landmark jitter.

## Coordinate system

- `x = 0` is the calibrated hip center; positive x points toward image-right.
- `y = 0` is the floor; positive y points upward.
- `z` is distance from the camera in metres.
- The neutral pose defaults to `z = 2.0 m` with a `0.55 m` shoulder-to-hip
  torso length.

Current depth is estimated from the ratio between calibrated and observed
torso size. Horizontal scale accounts for the camera frame's aspect ratio.
`SkeletonMapper` applies Kinect-style tracking states and temporal smoothing
after this metric projection.

## DroidCam calibration

Device 0 was verified as `DroidCam Source 3` in the development environment.
Run:

```powershell
.\build\native-release\fnk_calibration_test.exe --device 0 --samples 30 `
    --confidence 0.30 --depth 1.0 --timeout 45 `
    --output .\artifacts\calibration\droidcam.toml
```

Set `--depth` to the measured distance between the player and camera during
calibration. A close, torso-only setup at roughly one metre should use
`--depth 1.0`; the accepted range is 0.5–4.0 m.

For best results:

1. Put the phone in landscape orientation and keep it fixed.
2. Show the complete body, including both ankles.
3. Face the camera with arms relaxed and hold still until collection finishes.
4. Do not move the phone after calibration.

If shoulders and hips are reliable but ankles are cropped or uncertain, the
profile is explicitly saved as `torso_only`. In that mode the floor is
estimated from a human-proportion fallback. A `full_body` profile uses the
observed ankle line and is preferred for foot and jump interactions. Ankle
confidence is gated more strictly than torso confidence to prevent a cropped
frame from being mislabeled as full-body.

## Persisted profile

The generated TOML-like profile records:

- calibration quality;
- normalized horizontal center and floor line;
- neutral normalized torso size;
- metric torso length and reference depth;
- source image aspect ratio and sample count.

`save_game_space_profile()` and `load_game_space_profile()` provide the native
persistence boundary. Files under `artifacts/` are ignored because they are
specific to the player's room and camera placement.
