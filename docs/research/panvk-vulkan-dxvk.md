# Bifrost PanVK, Vulkan 1.3 and hardware DXVK (research plan)

Source: a research note shared by the project owner (gist `steamosxx-panvk-research.md`, snapshot 2026-11-26). Proposed
work, not tested here; every claim about other projects is theirs, not an H700 result.

## Where we stand (verified in this repo)
- G31 MC1 (GPU ID 0x7093) on the H700. Mesa 26.1.x from trixie-backports: Panfrost GL 3.1 / GLES 3.1, and PanVK Vulkan 1.0
  behind `PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1` (`panvk-run`). vkcube ~55 FPS (vsync-limited): kernel submission, rendering and
  XCB WSI work; that says nothing about conformance or game speed.
- Direct3D today: WineD3D on Panfrost GL (Geometry Wars 6-8.6 FPS), DXVK only on llvmpipe (1.4-3.8 FPS).
- Gamescope only on a software Vulkan with a shim (`docs/findings.md`).

## Stage 1: Mesa 26.3 on the G31
Mesa 26.3.0 (Nov 2026) adds Vulkan 1.3 on Mali v6/v7 (Bifrost, G31 and G52); start from 26.3.1 or later.
- Build Mesa for arm64 reproducibly (pinned release, source checksum, build inputs): PanVK plus matching Panfrost/Gallium,
  EGL/GBM/GLX, keeping llvmpipe/lavapipe as the fallback. Put it in an isolated prefix or an opt-in test image and select it
  with `VK_DRIVER_FILES`; never mix Mesa libraries or replace the system ICD. Include the new inputs in the base-image cache
  hash (`tools/build-rootfs.sh`). Do not use `MESA_VK_VERSION_OVERRIDE` (it advertises, it does not implement).
- On the device record `vulkaninfo`, the full feature/extension/limit dump, driver version, vkcube, kernel log, memory use,
  and regress Xorg, Steam and WineD3D.

## Stage 2: API version is not the question
Check the physical device after the upgrade instead of inferring from the version number: `robustness2`/`nullDescriptor`,
transform feedback, descriptor indexing, geometry shaders. Bifrost has no hardware geometry stage, so geometry needs real
emulation.
- DXVK: pick a version and audit its requirements. 1.10.3 (Vulkan 1.1 baseline) is a diagnostic candidate; 2.x needs
  Vulkan 1.3 plus extensions; 3.x raises the baseline further. An older release is not a guaranteed workaround. Order: D3D9
  first, then D3D10/11 (geometry, transform feedback, descriptors). Check the DXVK bundled in the Proton build we ship.
- Gamescope is a separate audit: nullDescriptor, DRM format modifiers, external memory and dma-buf sync, KMS scanout, the H700
  display engine's DMA limits and the existing software compositor crashes. DXVK working does not imply Gamescope working.

## Stage 3: community code worth reading
- `JimVulkan/mali-panvk`: a Mesa-derived Android port (Mesa 27.0.0-devel base) claiming Vulkan 1.4 (1.3 on the smallest Bifrost
  parts), robustness2/null descriptors, compute-based geometry, transform feedback, tessellation and DXVK on some devices. It
  talks to Android `mali_kbase`, not mainline DRM Panfrost, so it is a source of NIR/shader lowering and feature code, not a
  drop-in. Port plan: pin a fork revision, check licences, diff against its Mesa base, keep the Linux Panfrost kmod
  submission/allocation/sync and X11/Wayland WSI, audit every kbase/gralloc dependency. Get robustness/null descriptors working
  first, then geometry and transform feedback; advertise only what is tested; no conformance claims.
- `zenithblue-oss/panvk-kbase-android` worklog (`BIFROST_V6_V7_RESEARCH.md`) helps locate compiler and job-manager code.
- Test gates: native feature microtests and applicable dEQP/VK-GL-CTS cases, then a pinned legacy DXVK with a light D3D9 title
  (logs must name the Mali, not llvmpipe), then FPS/correctness/CPU/RAM/thermals; D3D11 and Gamescope only after that.

## Hardware expansion
RK3326 (A35/G31) and RK3566 (A55/G52) share Bifrost, so userspace GPU work carries over (core counts and bandwidth differ).
RK3566 has native LSE atomics (some units 2 GB RAM). Make it the second performance target without blocking H700 research;
test the Steam/Proton path separately since LSE does not cover every binary CPU requirement.

## Links
Mesa 26.3.0 and 26.3.1 release notes and the Panfrost page (docs.mesa3d.org); DXVK wiki "Driver support"; github.com/JimVulkan/mali-panvk;
github.com/zenithblue-oss/panvk-kbase-android.
