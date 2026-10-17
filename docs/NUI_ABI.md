# Fruit Ninja Kinect: NUI guest ABI

This document records the ABI actually consumed by the title, rather than
assuming that the desktop Kinect SDK is binary-compatible. The analyzed image
uses the statically linked Xbox NUI library version `2.0.20500.0`. Guest scalar
and floating-point fields are big-endian; guest pointers and handles are 32-bit.
PowerPC arguments start in `r3`, return values use `r3`, and structures containing
`Vector4` are 16-byte aligned.

## Title call sequence

The title-side initializer at `0x8266D848` performs these calls:

```text
NuiInitialize(enable_color ? 0x00000C0B : 0x00000C09, 0xFFFFFFFF)
NuiSkeletonTrackingEnable(skeleton_event, 2) // title selects the tracked skeletons

if (enable_color):
  NuiImageStreamOpen(1, 2, 0, 1, color_event, &color_stream)

NuiImageStreamOpen(0, 1, 0, 1, depth_event, &depth_stream)
```

The observed enum values are therefore:

| Value | Meaning |
|---:|---|
| image type `0` | depth plus player index |
| image type `1` | color |
| resolution `1` | 320 x 240 |
| resolution `2` | 640 x 480 |

The standard bits in the initialization mask are depth/player (`0x1`), color
(`0x2`) and skeleton (`0x8`). Bits `0x400` and `0x800` are private Xbox NUI
extensions. Their numeric values are certain, but no public symbol names were
found, so the compatibility layer deliberately leaves them unnamed.

## Functions used by the title

The addresses below belong to this exact executable and are evidence anchors,
not stable API addresses:

| Address | Recovered signature |
|---:|---|
| `0x821C2918` | `HRESULT NuiInitialize(DWORD flags, DWORD hardwareThread)` |
| `0x821B3720` | `HRESULT NuiSkeletonTrackingEnable(HANDLE event, DWORD flags)` |
| `0x821B3870` | `HRESULT NuiSkeletonGetNextFrame(DWORD waitMs, NUI_SKELETON_FRAME* out)` |
| `0x821C3CC0` | `HRESULT NuiImageStreamOpen(type, resolution, flags, frameLimit, event, HANDLE* out)` |
| `0x821C3120` | `HRESULT NuiImageStreamGetNextFrame(HANDLE stream, DWORD waitMs, NUI_IMAGE_FRAME** out)` |
| `0x821C3448` | `HRESULT NuiImageStreamReleaseFrame(HANDLE stream, NUI_IMAGE_FRAME* frame)` |

`NuiSkeletonGetNextFrame` copies exactly `0xAB0` bytes to its second argument at
`0x821B396C`-`0x821B3978`. The skeleton worker at `0x8266CA80` passes `waitMs=0`
and alternates between two buffers with stride `0xAB0`.

The depth worker at `0x8266D1A0` and color worker at `0x8266D348` call
`NuiImageStreamGetNextFrame(stream, 0, &frame)`, process the returned frame, then
release the same pointer. The open routine validates a non-null sixth argument,
a frame limit from 1 through 48, and image type values below 9.

## `NUI_SKELETON_FRAME`

Total size `0xAB0`, alignment 16:

| Offset | Size | Field |
|---:|---:|---|
| `0x000` | 8 | timestamp |
| `0x008` | 4 | frame number |
| `0x00C` | 4 | flags |
| `0x010` | 16 | floor clip plane (`Vector4`) |
| `0x020` | 16 | normal to gravity (`Vector4`) |
| `0x030` | `6 * 0x1C0` | six skeleton records |

Each `NUI_SKELETON_DATA` record has size/stride `0x1C0`:

| Offset | Size | Field |
|---:|---:|---|
| `0x000` | 4 | tracking state |
| `0x004` | 4 | tracking ID |
| `0x008` | 4 | enrollment index |
| `0x00C` | 4 | user index |
| `0x010` | 16 | overall position |
| `0x020` | `20 * 16` | twenty joint `Vector4` values |
| `0x160` | `20 * 4` | twenty joint tracking states |
| `0x1B0` | 4 | quality flags |
| `0x1B4` | 12 | tail padding/reserved |

The game loop at `0x8266C2F0` iterates exactly six records, advances by `0x1C0`,
and reads twenty joints from `0x20`. Player selection at `0x8266C960` reads the
tracking ID at record offset `0x04`.

## `NUI_IMAGE_FRAME`

Total size `0x28`:

| Offset | Size | Field |
|---:|---:|---|
| `0x00` | 8 | timestamp |
| `0x08` | 4 | frame number |
| `0x0C` | 4 | image type |
| `0x10` | 4 | resolution |
| `0x14` | 4 | guest pointer to frame texture |
| `0x18` | 4 | frame flags |
| `0x1C` | 12 | view area: digital zoom, center X, center Y |

The title's copy routine at `0x8266CF18` reads every field through `0x24`.
Texture locking returns an 8-byte `D3DLOCKED_RECT`-compatible pair containing
pitch and a 32-bit guest pointer to pixels. Depth pixels are 16-bit big-endian,
with 13 depth bits and a 3-bit player index, at 320 x 240.

The executable representation of these layouts lives in
`include/fnk/nui/guest_abi.hpp`; compile-time assertions prevent accidental ABI
drift.

## Native dispatcher and XenonRecomp hooks

`NuiDispatcher` implements the recovered entry points against an abstract
`GuestMemory` mapping. `FlatGuestMemory` adapts XenonRecomp's `base + guestAddress`
model and allocates image frames, synthetic texture tokens, and pixel storage
from a runtime-reserved scratch arena.

`src/nui/xenon_hooks.cpp` overrides these weak XenonRecomp symbols:

| Guest address | Override |
|---:|---|
| `0x821C2918` | initialize |
| `0x821B3720` | enable skeleton tracking |
| `0x821B3870` | get skeleton frame |
| `0x821C3CC0` | open image stream |
| `0x821C3120` | get image frame |
| `0x821C3448` | release image frame |
| `0x8220FFD0` | lock synthetic texture rectangle |
| `0x8220E580` | unlock synthetic texture rectangle |

The texture overrides first check whether the pointer belongs to the NUI
dispatcher. All other textures are forwarded to XenonRecomp's retained
`__imp__sub_*` implementation, so ordinary game and renderer textures are not
affected.

For the modern CPU V2 runtime, `xenon_registry_overlay.cpp` wraps the generated
`CompiledLookupCallback` after module binding. It substitutes native entries at
the same eight recovered addresses and delegates all other lookups to the
original registry. `LockRect` and `UnlockRect` also delegate at call time when
the texture is not owned by `NuiDispatcher`, preserving the title's normal D3D
texture behavior.

Skeleton and depth/player streams are operational. Color stream lifecycle is
recognized, but frame publication intentionally returns `E_NUI_FRAME_NO_DATA`
until the Xbox linear X8R8G8B8 byte order is verified. This avoids silently
feeding channel-swapped images to the title.
