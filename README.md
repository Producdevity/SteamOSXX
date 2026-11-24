# SteamOS-style Linux for the Anbernic RG40XX-H

Valve's native **arm64 Steam client in Big Picture mode** on a $70 handheld: Anbernic RG40XX-H, Allwinner
H700 (4x Cortex-A53, Mali-G31, 1 GB RAM, 640x480). A flashable SD-card OS built from a mainline kernel and a
Debian arm64 userspace, booting straight into a Steam session.

Started as a proof of concept on muOS ([steam-rg40xx](https://github.com/xXJSONDeruloXx/steam-rg40xx): the
client running in a chroot on the vendor 4.9 kernel). This project replaces muOS with mainline Linux, which
brings KMS, Panfrost, PipeWire, user namespaces and SysV IPC.

> **Status: early but working.** The image boots, joins Wi-Fi, and runs the Steam client in Big Picture
> on the panel with audio. Expect it to be slow: everything Steam draws is a Chromium web UI on four A53
> cores.

## What works

| Area | Status | Notes |
|---|---|---|
| Boot | yes | U-Boot with this unit's LPDDR4 timings, ROCKNIX's mainline 7.2 kernel, Debian trixie |
| Display | yes | DSI panel via sun4i-drm/KMS, Xorg modesetting + glamor on Panfrost |
| GPU | partial | Panfrost GL 3.1 / GLES 3.1; Vulkan is software only (Debian Mesa has no PanVK for Bifrost) |
| Steam | yes | `steamdeck_publicbeta` linuxarm64 client, `-gamepadui`, LSE atomics emulated (ARMv8.0) |
| Audio | yes | H616 codec through PipeWire in the session; speaker and headphone jack |
| Controls | yes (kernel) | `H700 Gamepad` with standard evdev codes; volume keys and menu+volume brightness |
| Wi-Fi | yes | RTL8821CS (rtw88), NetworkManager |
| Sleep / wake | not yet | needs TF-A's H616 PSCI system-suspend support |
| gamescope | not yet | needs a Vulkan driver with `robustness2`; PanVK does not offer it on this GPU |
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
`cache/steam-home.tar` exists (a Steam install from another device), it is seeded into the image.

**RAM timings differ between hardware revisions.** `rg40xxh_2025_lpddr4_defconfig` matches the unit this
was built on; read yours from its stock firmware first (`tools/boot0-dram.py`, see
[docs/lessons-learned.md](docs/lessons-learned.md)). Wrong timings give a silent, completely dead boot.

## Developing without touching the device

The development setup keeps muOS in slot 1 as a known-good recovery system and this OS in slot 2:

- `tools/fel-boot.sh` reboots muOS into FEL (USB boot mode) and loads a U-Boot that boots slot 2.
- Any reset returns to muOS. The hardware watchdog, panic-on-oops, a reachability guard and dev-mode
  power handling make sure every failure ends in a reset rather than a hang.
- `tools/rescue.sh` finds the device in any state and gets a shell back; `tools/dev.sh` runs commands and
  takes screenshots over adb or SSH.

See the recovery playbook in [docs/lessons-learned.md](docs/lessons-learned.md) and the running
[docs/findings.md](docs/findings.md).

## Credits and licenses

- Kernel, DTBs, modules and firmware: [ROCKNIX](https://github.com/ROCKNIX/distribution) H700 builds (GPL-2.0).
- U-Boot defconfigs and DRAM patch derived from ROCKNIX's H700 U-Boot package; U-Boot and TF-A upstream.
- `rootfs/src/lse_emu.c`: [SildurFX/LSE-Emu](https://github.com/SildurFX/LSE-Emu) (GPL-3.0), with fixes from
  DroidDeck's `feat/lse-emu`.
- Steam is Valve's; this project downloads the client from Valve's CDN and ships none of it.

Everything else here is GPL-3.0-or-later (see `LICENSE`).
