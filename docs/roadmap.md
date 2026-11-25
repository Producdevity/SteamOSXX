# SteamOSXX roadmap

Goal: an SD-card OS for the RG40XX-H (and later other H700 handhelds) that feels like SteamOS: boots into
Steam's gamepad UI, games launch from it, and display, audio, controls, power and sleep behave like a Deck.
Mirror upstream SteamOS wherever the hardware allows.

Status keys: [x] done, [~] partial, [ ] open. Effort/risk: S/M/L.

## Done (2026-11-24)
- [x] Boots mainline (ROCKNIX 7.2 kernel) with U-Boot using this unit's LPDDR4 timings.
- [x] Remote dev loop with no physical steps: muOS in slot 1 as recovery, our OS in slot 2 through FEL;
      watchdog, panic/oops reboot, reach-guard, dev-mode power handling, `tools/rescue.sh`.
- [x] Steam arm64 client in a logind session: rootless Xorg, PipeWire, `-gamepadui -steamos3 -steampal -steamdeck`.
- [x] Display (KMS, Panfrost GL), audio (speaker), Wi-Fi, battery (upower), built-in pad, on-screen keyboard,
      volume/brightness keys, cursor hidden until motion.
- [x] Automation: `sxx` (Steam DevTools + virtual test pad), `tools/smoke.sh`, screenshots, launch tracing.
- [x] First game: Geometry Wars through Proton Experimental (ARM64) + FEX + LSE-Emu (DXVK on llvmpipe).

## 1. Reproducibility (do first; many fixes were applied live)
- [ ] Diff the running device against `overlay/` + `rootfs/` and fold in anything missing. S
- [ ] Rebuild rootfs and image from scratch; flash slot 2 from muOS (`dd` to `/dev/mmcblk1`), FEL-boot, run
      `tools/smoke.sh`. Automate as `tools/reflash-slot2.sh`. M
- [~] Pin the Steam bootstrap: `tools/fetch-steam.sh` installs the arm64 client from the CDN manifest
      (each package SHA256-checked) into a clean seed tar; `STEAM_SEED=` puts it in the image. Still to do:
      pin a manifest version, and check that the first start does not re-download. M
- [ ] CI: GitHub Actions builds U-Boot, rootfs and image (arm64 runners or QEMU). M

## 2. Games
- [x] Renderer choice per API on this GPU (D3D8/9 WineD3D on Panfrost, D3D10/11 DXVK): compare DXVK-on-llvmpipe vs `PROTON_USE_WINED3D=1` (Panfrost GL 3.1)
      on Geometry Wars (FPS, CPU); pick a default. S
- [~] Install sunshineinabox/proton-armv8 (ARMv8.0 rebuilds of Proton 11/GE, no LSE) into
      `compatibilitytools.d` and make it the default; keeps LSE-Emu for the client only. S
- [ ] fex-armv8 for native x86 Linux games. S/M
- [~] Never livelock on memory: earlyoom (games first), MGLRU `min_ttl_ms`, PSI game kill in the
      memory-locked `reach-guard` (plus a real sshd probe). Verify with a deliberate memory hog. S
- [x] Game windows on top, Steam button menu over games and back: `GAMESCOPE_WAYLAND_DISPLAY` turns on
      Steam's gamescope logic, `sxx-focus` plays gamescope's side (base layer, overlay plane, focus). S
- [ ] Faster Steam menu in game (10-20 s now: web helper comes back from zram). M
- [ ] Game mode for 1 GB: suspend or unload steamwebhelper while a game runs, per-game zram/swap
      (Switchdeck's `SD_GAMEMODE`). Measure free RAM in game before/after. M
- [ ] Steam in-game overlay back in the container: LSE-Emu must initialise before the overlay there too
      (container-side wrapper or LD_AUDIT-based early handler). M
- [ ] Controller inside games (Steam Input virtual pad through the container); Steam button / QAM in game. S
- [ ] A small compatibility list: which titles run, at what FPS (`docs/games.md`). ongoing

## 3. SteamOS session parity
- [ ] steamos-manager (with Armada's patch notes: `CpuScaling1`, `LowPowerMode1`, `BatteryChargeLimit1`,
      brightness) so Steam's own settings (brightness, performance, power) work. M/L
- [ ] InputPlumber (or HHD) for buttons/power key handling like SteamOS; replace `handheld-keys`. M
- [ ] Power menu: Restart / Shut down from Steam through the sentinels (dev mode keeps poweroff = reboot). S
- [ ] First-boot experience: Steam's OOBE (Wi-Fi setup via NetworkManager, sign-in QR) on a fresh image. M
- [ ] Boot splash (Plymouth, SteamOS-style) and boot-time budget. S
- [ ] Steam UI speed: CEF on the GPU (`CEF_GPU=1`, Panfrost GLES) vs software; webhelper memory. M
- [ ] Desktop mode via `steamos-session-select desktop` (optional, lightweight). M

## 4. Hardware
- [ ] Sleep/wake: ROCKNIX PR #3316 approach (TF-A `SYSTEM_SUSPEND` + SRAM stub built for this unit's DRAM
      type and timings, `opp-suspend`, axp20x-pek wake, phy/mmc fixes). Test only with `rtcwake` alarms. M/L
- [x] Brightness from Steam's slider (Steam writes the backlight; udev rule for access).
- [ ] stick RGB LEDs, rumble (FF on the pad),
      headphone-jack switching, HDMI out, Bluetooth (RTL8821CS), charge limit. S each
- [ ] CPU/GPU governors (schedutil, Panfrost devfreq), thermals, battery life measurement. S/M
- [ ] Standalone boot from slot 1 (`uboot/build.sh card`), and the slot-2 card as a Steam library. M
- [ ] Detect the DRAM revision at install (read the stock boot0 before overwriting) or ship per-revision
      U-Boots. M

## 5. GPU / gamescope (long shot)
- [ ] Mesa 26 PanVK on Bifrost v7 (`PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1`): does DXVK run on it? L
- [ ] gamescope: make `VK_EXT_robustness2` nullDescriptor optional, handle the split display/GPU devices
      (gamescope #2070), synthesize an EDID for the DSI panel (Armada patches 0002/0003). L, high risk
- [ ] Until then keep Xorg + openbox (works, low overhead).

## 6. Release
- [ ] Release images on GitHub with an install guide (flash, first boot, recovery).
- [ ] Other H700 devices (RG35XX family, RG40XX-V): DTB and DRAM per device.

## References
- Prior-art survey: Armada (armada-os/armada), Valve Holo Core aarch64, Steam Frame userspace ports,
  SildurFX/Switchdeck + LSE-Emu, sunshineinabox/proton-armv8, ChimeraOS gamescope-session-steam,
  ROCKNIX H700 (suspend PR #3316). Details in `findings.md`.
