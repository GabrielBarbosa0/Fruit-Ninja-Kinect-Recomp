# Initial binary analysis

This report records metadata only. The executable and extracted assets are not
part of the repository.

## Source artifact

- STFS type: `LIVE`
- STFS display name: `Fruit Ninja Kinect`
- Package content directory: `584109A6` (container-side identifier)
- Extracted entries: 400
- Extracted regular files: 379
- `default.xex` size: 3,661,824 bytes
- `default.xex` SHA-256:
  `5A28F7EF1FEB9F19AFFA041CB22B03DCE4881B1B4AEED27DDC0C76BDB1696FAB`

The executable's own title identifier is `58410B79`; it differs from the
container directory name and is the identifier that runtime work should use.

## XEX image

- Original PE name: `FNK.exe`
- XEX load/image base: `0x82000000`
- Image size: `0x00900000` (9 MiB)
- Entry point: `0x821AE500`
- Code range: `0x82160000` through `0x827C0000`
- Read/write data range: `0x827C0000` through `0x828C0000`
- Media ID: `5DF68240`
- Title/savegame ID: `58410B79`
- Build timestamp recorded in the XEX: 2011-07-19 01:06:58
- Compression: normal/LZX-style XEX compression
- Encryption: normal retail XEX encryption
- Default stack: 262,144 bytes

The executable sets `XEX_SYSTEM_SKELETAL_TRACKING_REQUIRED`.

## Imports and linked Xbox libraries

The XEX header records 212 `xam.xex` import records and 320 `xboxkrnl.exe`
import records. Xenia resolves these into 106 XAM functions and 160 kernel
functions. The binary statically identifies these relevant SDK libraries:

- `NUI`, `D3D9`, `D3DX9`, `XACT3`, `XAUDIO2`, `X3DAUD`;
- `XAPILIB`, `XBOXKRNL`, `XONLINE`, `XNET`, and `XMCORE`.

The NUI library is statically linked, so the skeleton/depth implementation is
mostly game-side code rather than a simple imported function boundary.

## Runtime baseline

Xenia Canary build `976c3578b` loaded the title headlessly with null graphics,
audio, and controller backends. During a 20-second capture it:

- recognized the title as Fruit Ninja Kinect;
- reached normal guest execution without an immediate CPU fault;
- created `DepthUpdateThread` and `SkeletonTrackingUpdateThread`;
- issued rendering commands continuously;
- encountered expected null-renderer failures and some VFS lookups;
- remained alive until the diagnostic process was stopped.

Xenia's imported XAM surface includes Kinect UI, identity, user enrollment, and
camera tilt calls, but current Xenia builds do not implement Kinect sensor data.

## XenonAnalyse result

The locally built XenonAnalyse accepted this XEX and exited successfully. Its
current heuristic found no jump tables. That is not evidence that the program
contains none; the tool documentation warns that patterns vary with compiler
versions. Function discovery and indirect branches therefore require manual
verification before trusting generated C++.
