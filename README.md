# SteamOSXX

SteamOS-style Linux for the Anbernic RG40XX-H (and, in time, other H700 handhelds).

Valve's native **arm64 Steam client in Big Picture mode** on a $70 handheld: Anbernic RG40XX-H, Allwinner
H700 (4x Cortex-A53, Mali-G31, 1 GB RAM, 640x480). A flashable SD-card OS built from a mainline kernel and a
Debian arm64 userspace, booting straight into a Steam session.

Started as a proof of concept on muOS ([steam-rg40xx](https://github.com/xXJSONDeruloXx/steam-rg40xx): the
client running in a chroot on the vendor 4.9 kernel). This project replaces muOS with mainline Linux, which
brings KMS, Panfrost, PipeWire, user namespaces and SysV IPC.

> **Status: early but working.** The image boots into Steam's Big Picture, and Windows games launch from
> it through Proton ARM64 + FEX. Games come to the front, the Steam button opens Steam's menu over them,
> and B returns, as on SteamOS. Expect it to be slow: Steam's UI is Chromium on four A53 cores; Direct3D 9
> renders on the Mali through WineD3D (Geometry Wars: ~8 FPS), Direct3D 10/11 on the CPU.

## What works

| Area | Status | Notes |
|---|---|---|
| Boot | yes | U-Boot with this unit's LPDDR4 timings, ROCKNIX's mainline 7.2 kernel, Debian trixie |
| Display | yes | DSI panel via sun4i-drm/KMS, Xorg modesetting + glamor on Panfrost |
| GPU | partial | Panfrost GL 3.1 / GLES 3.1 (Mesa 26.1); PanVK Vulkan 1.0 for native programs (`panvk-run`), not enough for DXVK |
| Steam | yes | `steamdeck_publicbeta` linuxarm64 client, `-gamepadui -steamos3`, LSE atomics emulated (ARMv8.0) |
| Games | yes, slow | Proton ARM64 (Valve's, or [proton-armv8](https://github.com/sunshineinabox/proton-armv8)) + FEX; Direct3D on the GPU through WineD3D/Panfrost (D3D10/11 need DXVK on llvmpipe, slow). See [docs/games.md](docs/games.md) |
| SteamOS session | partial | gamescope's focus protocol on Xorg (`sxx-focus`): games in front, Steam button menu over them; power menu; brightness slider; volume |
| Audio | yes | H616 codec through PipeWire in the session; speaker and headphone jack |
| Controls | yes | `H700 Gamepad` through Steam Input (in Big Picture and in games); volume keys, menu+volume brightness |
| Memory (1 GB) | managed | zstd zram at 2x RAM, earlyoom, MGLRU thrash guard, and a guard that ends a game on sustained memory stalls |
| Wi-Fi | yes | RTL8821CS (rtw88), NetworkManager |
| Sleep / wake | not yet | needs TF-A's H616 PSCI system-suspend support |
| gamescope | emulated | real gamescope needs `VK_EXT_robustness2`, which PanVK lacks on this GPU (Bifrost); Steam runs in its gamescope mode against Xorg + `sxx-focus` |
| Standalone boot from slot 1 | built, untested | development currently boots it from slot 2 through FEL |

## How it boots

```
Boot ROM -> U-Boot SPL (LPDDR4 timings from the unit's own boot0) -> TF-A -> U-Boot
        -> extlinux: ROCKNIX kernel 7.2 + RG40XX-H DTB + logging initramfs
        -> Debian trixie (systemd) -> steam-session.service (logind session, rootless Xorg, PipeWire)
        -> Steam -gamepadui
```

Card layout: U-Boot at 8 KiB, p1 FAT32 `STEAMBOOT` (kernel, DTBs, initramfs, `extlinux.conf`), p2 ext4
`steamroot` (grown to the card on first boot).

## Building

Needs Docker (with arm64 emulation) and bash. Run from the repository root.

```
tools/fetch-rocknix.sh        # ROCKNIX H700 release: kernel, DTBs, modules, firmware -> cache/
uboot/build.sh card           # U-Boot for booting from slot 1 -> out/uboot/card.bin
tools/build-rootfs.sh         # Debian arm64 userspace -> out/rootfs.tar
tools/build-image.sh          # -> out/steamos-rg40xx.img
```

Before `build-rootfs.sh`, put your own `secrets/authorized_keys` (SSH public key) and Wi-Fi profiles in
`secrets/nm/*.nmconnection`; they are copied into the image and never committed. If
`cache/steam-home.tar` exists (a Steam install from another device), it is seeded into the image;
`tools/fetch-steam.sh` makes a clean, SHA256-checked client seed instead (`STEAM_SEED=`).

GitHub Actions builds U-Boot, the rootfs (on an arm64 runner) and the image on every push; tagged builds
attach the image to a draft release.

**RAM timings differ between hardware revisions.** `rg40xxh_2025_lpddr4_defconfig` matches the unit this
was built on; read yours from its stock firmware first (`tools/boot0-dram.py`, see
[docs/lessons-learned.md](docs/lessons-learned.md)). Wrong timings give a silent, completely dead boot.

## Developing without touching the device

The development setup keeps muOS in slot 1 as a known-good recovery system and this OS in slot 2:

- `tools/fel-boot.sh` reboots muOS into FEL (USB boot mode) and loads a U-Boot that boots slot 2.
- Any reset returns to muOS. The hardware watchdog, panic-on-oops, a reachability guard and dev-mode
  power handling make sure every failure ends in a reset rather than a hang.
- `tools/rescue.sh` finds the device in any state and gets a shell back; `tools/dev.sh` runs commands and
  takes screenshots over adb or SSH; `tools/push-live.sh` applies the overlay to the running device.
- `tools/smoke.sh` checks every area in one go; on the device, `sxx-gametest <appid>` launches a game from
  Steam and checks it comes to the front, the Steam menu opens over it and closes, and quitting returns
  to Big Picture. `sxx eval` drives Steam's UI through DevTools; `sxx pad` presses buttons on a virtual pad.

See the recovery playbook in [docs/lessons-learned.md](docs/lessons-learned.md) and the running
[docs/findings.md](docs/findings.md).

## Credits and licenses

- Kernel, DTBs, modules and firmware: [ROCKNIX](https://github.com/ROCKNIX/distribution) H700 builds (GPL-2.0).
- U-Boot defconfigs and DRAM patch derived from ROCKNIX's H700 U-Boot package; U-Boot and TF-A upstream.
- `rootfs/src/lse_emu.c`: [SildurFX/LSE-Emu](https://github.com/SildurFX/LSE-Emu) (GPL-3.0), with fixes from
  DroidDeck's `feat/lse-emu`.
- Steam is Valve's; this project downloads the client from Valve's CDN and ships none of it.

Everything else here is GPL-3.0-or-later (see `LICENSE`).
