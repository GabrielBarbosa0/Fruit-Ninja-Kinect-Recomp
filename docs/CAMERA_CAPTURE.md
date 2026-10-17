# Real webcam capture

The `fnk_camera` target provides a small Windows Media Foundation capture
backend. It deliberately has no dependency on an emulator or pose-estimation
framework, so frame acquisition can be tested before the rest of the Kinect
compatibility pipeline exists.

## API contract

`fnk::camera::MediaFoundationCamera` provides:

- device enumeration with stable Media Foundation symbolic IDs;
- selection by the enumerated device index;
- best-match native resolution and frame-rate selection;
- conversion to tightly packed, top-to-bottom BGRA8 frames;
- capture on a `std::jthread`, with a latest-frame snapshot that does not make
  the game thread wait for the camera;
- measured capture statistics and HRESULT-based error reporting;
- deterministic shutdown through `stop()` or the destructor.

The producer owns its frame buffer. `latest_frame()` returns a copy so a pose
engine can process it after the capture worker has already advanced to the next
frame.

## Diagnostic program

After running `scripts/Build-Native.ps1`, list the available devices:

```powershell
.\build\native-debug\fnk_camera_test.exe --list
```

Capture 60 frames from device 0 and save the newest frame as binary PPM:

```powershell
.\build\native-debug\fnk_camera_test.exe --device 0 --width 640 --height 480 `
    --fps 30 --frames 60 --output .\artifacts\camera\latest.ppm
```

`--width`, `--height`, and `--fps` are requests. Media Foundation selects the
closest native camera mode, and the program reports the negotiated dimensions
and measured rate. The LLVM-MinGW runtime directory must be on `PATH`; the
build script already arranges this while compiling and testing.

## Pose integration

The `fnk_pose` target now consumes `CameraFrame::bgra`, runs MoveNet through
ONNX Runtime, and emits the existing detector-neutral `PoseFrame`. Camera
acquisition, model inference, Kinect joint mapping, and synthetic depth remain
independently testable. See `POSE_INFERENCE.md` for usage.
