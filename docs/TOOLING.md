# Local tooling snapshot

The tools below are downloaded under the ignored `tools/` directory. Commit
IDs and archive hashes record the initial reproducible workspace state; they do
not force contributors to use stale versions later.

## Source tools

| Tool | Commit |
| --- | --- |
| `morluto/rea` | `926e18b3fcb334c21d60685546ba8145d35d344e` |
| `jasonulbright/Extract-STFS` | `3483c68e6edd35dc94636429deff3c45efdfad70` |
| `hedge-dev/XenonRecomp` | `ddd128bcca99fe8bfbb99bea583c972351fa6ace` |
| `hedge-dev/XenosRecomp` | `990d03b28a27b50277ee5d8d942e1c5f873869d1` |
| `nimauria/Xenon-Recomp` | `f674eefd9e394a872be6f7f6764fccecde3d1547` |

The REA `unblob` submodule contains test filenames that cannot be represented
on NTFS. Its latest checked-out revision is usable for the Windows-hosted
workflow, but it cannot be normalized to REA's pinned revision on this
filesystem. Firmware extraction is not part of the current Xbox workflow.

## Binary tools

| Archive | Version | SHA-256 |
| --- | --- | --- |
| Xenia Canary Windows | `976c3578b`, 2026-10-17 | `54D15B48122FB711760885DB3E2DE0C5AFB49EB2136436A5E1015FA1D97CC01F` |
| LLVM-MinGW UCRT x86-64 | `20261006` | `317492C456AA27EE607A5919F1D2D38DCDC1112516A24D0BF4B00D078F52D17A` |
| CMake Windows x86-64 | `4.4.4` | `BACE36E94B31C68AB6FA295F26DFA11219E0701CF7C94B0284A7D1CB13DAC536` |
| Ninja Windows | `1.13.2` | `07FC8261B42B20E71D1720B39068C2E14FFCEE6396B76FB7A795FB460B78DC65` |
| ONNX Runtime Windows x64 | `1.23.2` | `0B38DF9AF21834E41E73D602D90DB5CB06DBD1CA618948B8F1D66D607AC9F3CD` |
| MoveNet SinglePose Lightning ONNX | Xenova conversion, `model.onnx` | `1AD4F8D6C2F776A9967DB3993C9CA740BC350104F9D37C151DC183FC29A464AD` |

XenonRecomp and XenonAnalyse were successfully built locally with Clang
23.1.3. With the current LLVM-MinGW headers, configuration needs
`-DCMAKE_CXX_FLAGS=-include cstdlib` for bundled fmt 11.

Run `scripts/Setup-Pose.ps1` to download and verify the last two artifacts.
They remain under the ignored `tools/pose` directory.
