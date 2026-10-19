# MoxFr experimental UEVR & Standalone build

Based on Cheeky Foveated DLSS v0.5.5 by ClarkCheekyKent and upstream contributors. This is an independent community fork, not an official upstream release.

## Added and adapted

- Independent second and third DLSS-NR passes with separately persisted parameters.
- Experimental RGB-residual reconstruction, area/bilinear reduction and bicubic/bilinear reconstruction options.
- Additional NR color, signal, exposure and working-resolution controls.
- Experimental per-eye, per-pass correction reprojection/cache and a soft GPU-budget controller.
- Opt-in peripheral SR and AMD FSR1 reconstruction paths.
- Standalone NR comparison tooling that restores the saved configuration when a comparison finishes or times out.
- Additional UEVR serialization and Lua controls for the new periphery and NR options.
- Optional F5/ViT reuse adapter, using the GPLv3 OptiScaler-derived source documented in THIRD-PARTY-NOTICES.md. Disabled by default; the integration currently admits only NVIDIA NR runtime 310.9.16.0 on its D3D12 path. Restart after changing this option. No NVIDIA model DLLs are bundled.

## Status and limits

These features are experimental. Additional NR passes can increase GPU cost. The cache budget is a soft controller, not a guaranteed frame-time cap. F5 runtime interception, image stability, stereo behavior and timing need game/headset validation. A successful build or mock test does not establish compatibility with every game or VR runtime. SteamVR support remains subject to the underlying Cheeky/OpenVR path and further testing.

The UEVR and Standalone ZIPs must be installed as complete matching packages. Do not mix runtime DLLs across builds. Keep a backup and restart the game after updating. The underlying OpenXR layer source is unchanged from upstream v0.5.5; the packages retain the matching upstream OpenXR setup utility with its source and provenance recorded.

## Build and source

Run `scripts/build.ps1 -Configuration Release`, then the package scripts. Full GPLv3 source, shader sources, generated shader headers, the F5 PTX source and dependency notices are available in this repository. NVIDIA model/runtime binaries must be obtained separately under their own terms.

## Validation for this preview

Release compilation and focused tests cover the UEVR ABI/settings/persistence, D3D12 NR processing, correction reuse with three passes, history/reset handling, Standalone processing/presentation/resize, OpenXR input bindings, and Lua menu behavior under LuaJIT 2.1 and Lua 5.4. These use fixtures and do not prove real NVIDIA F5 model performance or headset compatibility.

Known test failure: the complete upstream build/test command stops in `CheekyNrObserverTests --opaque-vr` with Windows FailFast (`0xC0000409`) on the build machine. This is unresolved; the full suite is not reported as passing. Use this prerelease only for experimental testing and retain a rollback package.
