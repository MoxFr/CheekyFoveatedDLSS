# Third-party notices and provenance

## Cheeky Foveated DLSS

Original project: https://github.com/ClarkCheekyKent/CheekyFoveatedDLSS

Upstream base: `e016dc756383f1959fe0ce9257a73785cc18605f` (v0.5.5).
License: GNU GPL version 3, retained in LICENSE. Original notices and vendored dependency licenses are retained; see third_party/README.md. The MoxFr fork modifications are distributed under GPLv3 as well.

## F5 / ViT reuse

Source: https://github.com/janblade/OptiScaler-F5-DLSSNR-Multipass

Pinned commit: `fadc3bf67f6c8548e94914baec3f2e551ba8d8a4`.
Project license: GPLv3, retained verbatim in licenses/OptiScaler-F5-GPLv3.txt.

The following files under src/f5 are unchanged copies of `OptiScaler/dlssnr/` at that commit:

- DlssNrVitReuse.h — SHA-256 `5b60d681b4481b8fb9cdd73eb59f413d3d40b6af05f0653afeba7865f21e5496`.
- DlssNrVitCopyKernel.h — SHA-256 `1b691c8c49cb917ac62bdb1a7a79f5b7822ea81235bebcbf5fb620bd3384dbb8`.
- DlssNrVitCopy.ptx — preferred source for the embedded copy kernel. The upstream header describes CUDA toolchain commands used to generate its fatbinary.

Credits: janblade and the OptiScaler-F5 contributors; the project builds on wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass and its upstream OptiScaler/Neural Rendering contributors. The Cheeky-specific adapter is src/nr_f5.hpp; integration changes are distinct from the copied files. Inclusion does not imply upstream endorsement or verified performance.

## AMD FidelityFX FSR1

The peripheral_fsr1 shader sources include AMD FidelityFX Super Resolution code (Copyright 2021 Advanced Micro Devices, Inc.) and the embedded portability-header notice for Michal Drobot (2014). Their MIT notices are retained in the shader sources and in licenses/AMD-FSR1-MIT.txt.

## Reconstruction concept

The NR RGB-residual option is a local adaptation of the residual-reconstruction concept exposed by RenoDX. It is not a claim of matching RenoDX's shader mathematics; see uevr/README.md for its limitations.

## NVIDIA

No nvngx_dlssnr.dll, NVIDIA model files, driver DLLs or proprietary runtime distribution is included. The runtime loads compatible user-provided components. The F5 copy kernel above is accompanied by its PTX source; it is not a redistributed NVIDIA NR model.
