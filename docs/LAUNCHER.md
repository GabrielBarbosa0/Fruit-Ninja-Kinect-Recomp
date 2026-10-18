# Graphical launcher

`Jogar-Fruit-Ninja.bat` opens the Windows Forms launcher in
`scripts/FruitNinjaLauncher.ps1`.

## Camera controls

The camera list comes from the same Media Foundation backend used by the game.
The selected device index, resolution, and frame rate are passed to ReXGlue as
`FNK_CAMERA_DEVICE`, `FNK_CAMERA_WIDTH`, and `FNK_CAMERA_HEIGHT`/
`FNK_CAMERA_FPS`. The launcher also supplies the absolute MoveNet model path in
`FNK_POSE_MODEL`, so starting the game does not depend on the current directory.

`Ver captura e esqueleto` starts `fnk_pose_preview.exe`. Its native preview
shows the camera frame, green tracked bones, an orange left-hand point, and a
red right-hand point. Close the preview with Escape or the window close button
before starting another camera operation.

## Distance and calibration

The physical distance is the measured space between player and camera. The
virtual distance is the Kinect-space depth exposed to Fruit Ninja; 2.0 metres
is the recommended default for a close webcam setup.

Changing the physical distance does not rescale an old pose automatically.
Use `Calibrar nesta distância` after moving the player or camera. The launcher
runs the existing calibration collector and writes
`artifacts/calibration/droidcam.toml`. It warns before launch when the selected
distance differs from the saved profile.

User selections are persisted in `state/launcher-settings.json`, which is
ignored by Git.
