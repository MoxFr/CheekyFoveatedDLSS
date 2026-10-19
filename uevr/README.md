# Cheeky Foveated DLSS for UEVR

Runs Cheeky's DLSS processing with controls in UEVR. Remove ReShade from the game before using the plugin.

## Installation

1. Close the game. Use UEVR with plugin API **2.39.0 or compatible newer 2.x**.
2. Uninstall ReShade from the game using the ReShade installer, and remove `CheekyFoveatedDLSS.addon64` if present.
3. Open the game's configuration directory from UEVR—normally `%APPDATA%\UnrealVRMod\<game-executable-name>`—and extract the UEVR ZIP there, preserving this structure:

```text
plugins/
  CheekyFoveatedDLSS.dll
  CheekyFoveatedDLSS/
    CheekyFoveatedDLSSRuntime.dll
scripts/
  cheeky_foveated_dlss.lua
```

4. **For OpenXR gaze, or ordinary stereo alignment/calibration, run the matching `OpenXR/CheekyOpenXRSetup.exe` included in the UEVR ZIP before starting the game.** AFW fixed/automatic coverage needs no OpenXR layer. Native OpenVR uses the built-in adapter, including when its compositor initialized before the plugin.
5. Start the game with DLSS enabled, inject UEVR and open **LuaLoader → Cheeky Foveated DLSS**. Check **Enable Cheeky for this game**. New profiles start disabled; the enable choice is saved per game.

For a global installation, extract the same `plugins/` and `scripts/` directories
under `%APPDATA%\UnrealVRMod\UEVR`. Avoid also installing a second copy in the
game's profile. The thin adapter stays dormant until enabled for that game, then
loads the processing runtime. The Lua enable choice is stored in `cheeky.txt`
under that game's UEVR Lua data directory.

Unchecking **Enable Cheeky for this game** detaches processing on the next host
frame. The hooked runtime remains resident until the application exits; restart
the application to remove it from memory. Re-enabling reconnects without a
restart and preserves processing settings. Replace the Lua script and DLLs
together when updating.

The OpenXR installation is shared across games. Install all plugin files and the layer from the same release when updating.

## First-use check

For AFW, follow the dedicated instructions below. Per-eye mode uses the usual eye calibration setup.

Select **Fixed**, enable **Automatic stereo alignment**, and use **Auto** as the mapping method. Use the red alignment border to check both eyes, then open **Stereo and gaze → Stereo Eye Mapping**. Check that calibration becomes **Active** and both regions stay aligned through loading/menu transitions.

In **Stereo Eye Mapping**, **Mapping method** offers Auto, Standard corners,
Timing tolerant corners, Full crop search and Manual. Manual disables automatic
marker calibration and enables **First DLSS view is right eye** (off means left
first). It assumes full-eye images with matching XR coordinates; use the red
border to verify both eyes and edges. Returning to an automatic method restarts
calibration; there is no separate calibration enable switch. Existing verified mapping routes still take precedence over the
manual fallback.

**Height offset** moves fixed, runtime-gaze and simulated-gaze placement, including
AFW and tracking-loss fallback. Negative moves up; positive moves down. Zero
preserves the detected center.

Manual stereo offsets are a troubleshooting fallback, not a replacement for calibration: scene changes can swap eye assignments. If calibration does not become active, check the layer installation and collect a report.

Sliders apply once on release; checkboxes and dropdowns apply immediately. Settings save automatically. Start with **DLSS-NR off**.

Press **Alt+Shift+>** (the period key with Alt and Shift held) to toggle the
entire DLSS-NR feature. Its foveation setting is preserved, and the change saves
automatically. Holding the shortcut toggles only once.

NR **Style** offers Standard, Natural and Cinematic. **Intensity** runs from 0
(no model edit) to 1 (full edit). **Skin structure** appears with Automatic mask
enabled; **Paper white** appears for HDR NR input.

With NR and Foveated NR enabled, **Second DLSS 5 pass** and then **Third DLSS 5
pass** enable the additional stages. Each has a separate settings tree for
working scale, preset, style, intensity and advanced controls. Disabling pass 2
also disables pass 3; disabling NR leaves the per-pass tuning saved. Extra
passes increase GPU work. Controls are hidden if the connected runtime does
not expose the corresponding settings. Existing profiles are not auto-enabled.

## Reports and details

### Experimental NR reconstruction

Each NR pass offers **Legacy** (default) and **RGB residual (experimental)**.
Residual applies `original + (NR output - NR input proxy)` before the existing
strength/foveation blend, retaining source detail when working scale is reduced.
HDR differences are computed after decoding the proxy/output to linear RGB.
Alpha and pixels outside the foveated region remain owned by the original image.
Changing mode resets NR temporal history. Old profiles stay on Legacy.

This is a local adaptation of RenoDX's exposed residual-reconstruction concept,
not a verified copy of its shader math. It does not add an NR evaluation or
texture allocation, but actual GPU cost and visual quality need measurement.
It may retain source shimmer/noise as well as useful detail; it is not an
antialiasing fix. Compare at the same working scale, with one NR pass first.

**Report an issue...** creates a support ZIP and opens a GitHub issue. Add your description and attach the ZIP; nothing is uploaded automatically. ZIPs are saved in `support/` under the game's UEVR configuration directory.

[Settings reference](../USAGE.md) · [Eye calibration](../EYE-CALIBRATION.md)

## Compatibility

Use Native Stereo first. Other stereo modes need game-specific testing. D3D11 uses direct processing by default. To use DLSS-NR on D3D11, enable **DX11 -> DX12 transport** under **DLSS-NR (experimental)**, then enable NR. Transport also supports SR without NR. This path still needs in-game UEVR/headset validation.

For DLSS-NR (native D3D12 or D3D11 transport), place a compatible `nvngx_dlssnr.dll` beside the nested runtime DLL or the actual game executable. NVIDIA binaries are not included. Use **Reset NR history / retry** after adding it.

Updating either DLL requires a full game restart. Reloading the adapter does not unload the resident runtime.

## AFW

AFW integration works with the public, unmodified AFW UEVR release on **DX12**. It does not require a separate Cheeky build of UEVR. AFW UEVR itself supports DX12 only; selecting it in a DX11 game falls back to AFR.

Use the same installation steps above and replace both Cheeky DLLs and the Lua script together when updating. Fixed AFW coverage uses UEVR's public stereo projections and needs no Cheeky OpenXR layer. Runtime OpenXR gaze requires the matching layer; native OpenVR uses the built-in adapter.

### Setup and controls

1. Select AFW in UEVR and enable DLSS in the game.
2. Open **Stereo and gaze**, leave **Foveation center** on Fixed, and select **Automatic** under **AFW stereo coverage**.
3. Adjust width and height under **DLSS-SR**. The minimum is **0.2 (20%)** for SR and independent NR regions. Older saved values below 0.2 are clamped when loaded.
4. Use the red alignment border to inspect both eyes. Disable the border when finished.

The coverage selector offers:

- **Automatic:** aligns the requested regions using both UEVR eye projections. Height offset adjusts their vertical position. Missing or mismatched projections use centered coverage.
- **Manual:** uses mirrored horizontal offsets and a shared height offset.
- **Per-eye (calibrated):** preserves the requested width, height and offsets and uses the calibrated eye mapping. This is the default when both AFW coverage flags are off.

**Advanced AFW → Extra margin per edge** adds a fixed allowance around the requested regions. It is a fraction of the full image, so 0.05 adds 5% on each edge. Stereo coverage and this margin can enlarge the visible region beyond the requested width and height. Automatic depth adjustment is removed. Existing coverage preferences and manual margin values are preserved.

The same controls apply to foveated NR with SR disabled. NR supports Before/After upscaling, full-frame or foveated processing, working scale, and independent or SR-linked size and shape. Supply a compatible `nvngx_dlssnr.dll` as described above.

### Reference-derived NR color bridge

Each pass now offers **NR color pipeline**: Legacy, Linear BT.709 bridge, or
simple sRGB bridge. Linear BT.709 matches RenoDX's native DLSS input assumption;
the bridge compresses HDR smoothly and restores the original pixel's scale.
Signal encoding and scaling domain are independent (sRGB/PQ/linear). Residual
is applied inside the compressed bridge before restoring HDR, not to raw RGB.
With bridge modes, the Legacy reconstruction choice means processed-image output.

**NR exposure texture** can multiply or divide the game's validated1x1 exposure
texture on D3D12; invalid/missing textures use1. The manual multiplier defaults
to1, and Paper white scale1 represents100nits. Vulkan and D3D11 transport do not
yet carry exposure textures. No pre-exposure scalar is blindly applied.

Legacy remains the default for old configurations. HDR10/scRGB gamut parity,
exact reference cubic filtering and guide-jitter equivalence are not claimed.
This path can cost more shader work; compare GPU timings and actual appearance.

### NR resampling controls

Each NR pass has **NR downsample filter** (Bilinear or Area) and **NR upsample
filter** (Bilinear or Bicubic clamped). Bilinear remains the default for existing
configurations. Area integrates the input pixel footprint; bicubic reconstructs
the low-resolution output with a local clamp to limit ringing. They work with
Legacy or RGB-residual reconstruction and reset NR history when changed.

These filters add sampling work, not extra NR evaluations. They are not an SR
preset or peripheral DLAA fix. At exactly half scale, area can match bilinear;
compare at another fractional scale as well. Keep other settings fixed and
measure GPU time before assuming any quality/performance benefit.

### Gaze and transitions

Runtime gaze and simulated gaze use a fixed crop budget derived from the fovea dimensions, warp margin and stereo projection. Fresh gaze takes priority when smoothing trails outside that budget; widely separated eye samples or masks can be clipped to the budget. In gaze modes, **AFW tracking-loss fallback** selects the fixed coverage used when tracking is unavailable. Each eye must provide a valid sample and matching projection. Brief tracking loss holds the last position before returning to fixed coverage; loss of focus stops live gaze immediately.

Gaze movement, smoothing and tracking loss change placement without resizing the allocation. Tracking loss moves the fixed gaze allocation toward the configured fallback position. Changing fovea dimensions or stereo projection can resize it. Small gaze movements retain temporal history. Jump simulations support **Show next jump target** without changing current coverage.

Marker calibration remains available with AFW. Switching UEVR away from AFW restores ordinary stereo controls; explicitly selected AFW coverage returns when AFW is selected again. The resolution-protecting hooks remain installed while the AFW runtime is loaded.

### Diagnostics and compatibility

The overview reports whether UEVR has selected AFW. This does not mean AFW is actively warping during loading screens or other temporary suspensions. **Performance** contains frame-rate comparisons, DLSS GPU timings and processing resolutions. Warp activity, source-eye identification, projection availability and routing details are collected in support ZIPs under **Support**.

Cheeky keeps the game's original full-size evaluation at AFW's outer hook. Private center/periphery SR and NR run inside the nested DLSS call, keeping those dimensions out of AFW's resolution-change detector. Native exports, `_C` exports, Streamline's nested route and NVIDIA OTA SR runtimes are supported. Genuine game-resolution changes still invoke AFW's own suspension behavior.

Source-eye identification uses the verified AFW beta 6 warp metadata and full depth-buffer copies already made by AFW. It does not sample depth or guess eyes from frame order. Coverage maps both eyes' requested regions into the current source projection. Matching pixel dimensions preserve temporal history across alternate-eye crop origins; size changes, game resets and skipped/failed private passes invalidate the affected history.

Missing or ambiguous nested DLSS routes pass through to ordinary DLSS. Unknown warp binaries retain routing and conservative coverage but cannot provide verified source-eye metadata. Other NGX proxies can affect compatibility.

Hogwarts Legacy compatibility and overlapping fixed coverage have been tested in a headset. Automated tests cover native/Streamline/OTA routing, SR/NR histories, real copy hooks and GPU composites, gaze/fallback transitions and the Lua menu. Those tests substitute NVIDIA inference; additional games and real eye tracking still need headset testing.

For final testing, compare SR off/on with peripheral DLAA, check both eyes during head and object motion, then exercise NR Before/After and gaze modes. Include loading screens, game-resolution changes and switching AFW off/on. If something fails, use **Report an issue...** to collect a support ZIP. Nothing is uploaded automatically. Restore a previous complete Cheeky package and restart the game to roll back.
