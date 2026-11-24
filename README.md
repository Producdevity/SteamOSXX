# SteamOS-style image for the Anbernic RG40XX-H

A flashable SD card image that boots straight into Valve's native arm64 Steam client (Big Picture) on the
RG40XX-H (Allwinner H700: 4x Cortex-A53, Mali-G31, 1 GB RAM, 640x480 panel).

Proven first in [steam-rg40xx](https://github.com/xXJSONDeruloXx/steam-rg40xx) as a chroot on muOS's 4.9
vendor kernel. This project replaces muOS with a mainline kernel, which brings KMS, Panfrost, user namespaces
and SysV IPC, so most of that project's workarounds go away.

## What's in the image

| Piece | Source |
|---|---|
| U-Boot (raw at 8 KiB) | ROCKNIX H700 DDR4 release image (`tools/fetch-rocknix.sh`) |
| Kernel 7.2.0 + DTBs + modules + Wi-Fi firmware | same ROCKNIX release (mainline + ROCKNIX's H700 patches) |
| Userspace | Debian trixie arm64 with systemd (`rootfs/Dockerfile`) |
| Steam | Valve's `steamdeck_publicbeta` linuxarm64 client + LSE-Emu (ARMv8.0 has no LSE atomics) |

ROCKNIX's kernel has its own initramfs built in. `rdinit=/no-initramfs` makes Linux skip it (the file does not
exist) and mount `root=PARTUUID=53544d52-02` directly.

Card layout: MBR (disk id `53544d52`), U-Boot at 8 KiB, p1 FAT32 `STEAMBOOT` (KERNEL, `dtbs/`,
`extlinux/extlinux.conf`, mounted at `/boot/firmware`), p2 ext4 `steamroot` (grown to the card on first boot).

## Staying reachable without touching the device

- **adb over USB**: configfs gadget with the same VID/PID (1d6b:0105) as muOS's, adbd from Debian, started
  before `sysinit.target` and restarted if it dies.
- **SSH over Wi-Fi**: NetworkManager profile converted from the muOS Wi-Fi config (kept out of git in
  `secrets/`), key-only SSH, `rg40xx.local` via avahi.
- **Hardware watchdog**: systemd pets the sunxi watchdog (`RuntimeWatchdogSec=30s`), so a hung kernel or PID 1
  reboots. `panic=10` reboots on kernel panics.
- **Persistent journal**: `journalctl -b -1` shows the previous boot after a crash.
- **Wi-Fi rescue**: `wifi-kick` rebinds the SDIO Wi-Fi if it probed before its firmware was on disk.
- The Steam session is a service (`steam-session.service`), so it can be restarted without rebooting.

## Build and flash

```
tools/fetch-rocknix.sh     # ROCKNIX U-Boot, kernel, DTBs, modules, firmware -> cache/
tools/build-rootfs.sh      # Debian arm64 userspace -> out/rootfs.tar (Docker, arm64 emulation)
tools/build-image.sh       # -> out/steamos-rg40xx.img (also seeds cache/steam-home.tar if present)
tools/flash.sh             # writes it to the SD card reader through the elevated helper
```

`secrets/` must hold `authorized_keys` and `nm/*.nmconnection` before `build-rootfs.sh`.

## Session settings

`/etc/steam-session.conf`: `STEAM_UI` (gamepadui/desktop), `PADMAP`, `CEF_GPU`, `STEAM_EXTRA_ARGS`.
Then `systemctl restart steam-session`.
